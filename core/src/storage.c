#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sqlite3.h>

#include "category.h"
#include "storage.h"
#include "util.h"

/* creates all components of path, like mkdir -p */
static int mkdirp(const char *path) {
    char tmp[1024];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(tmp)) return -1;
    memcpy(tmp, path, len + 1);

    for (size_t i = 1; i <= len; ++i) {
        if (tmp[i] == '/' || tmp[i] == '\0') {
            char c = tmp[i];
            tmp[i] = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
            tmp[i] = c;
        }
    }
    return 0;
}

/* open the SQLite database and create the schema if it does not exist yet;
 * all three tables live in the same file — items, categories, subcategories */
static int db_open(const char *path, sqlite3 **db) {
    if (sqlite3_open(path, db) != SQLITE_OK) {
        sqlite3_close(*db);
        return -1;
    }

    const char *schema =
        "CREATE TABLE IF NOT EXISTS items ("
        "  id          INTEGER PRIMARY KEY,"
        "  status      TEXT    NOT NULL DEFAULT 'open',"
        "  priority    TEXT    NOT NULL DEFAULT 'normal',"
        "  created_at  TEXT    NOT NULL,"
        "  category_id INTEGER NOT NULL DEFAULT -1,"
        "  subcat_id   INTEGER NOT NULL DEFAULT -1,"
        "  text        TEXT    NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS categories ("
        "  id   INTEGER PRIMARY KEY,"
        "  name TEXT NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS subcategories ("
        "  id          INTEGER PRIMARY KEY,"
        "  category_id INTEGER NOT NULL,"
        "  name        TEXT    NOT NULL"
        ");";

    if (sqlite3_exec(*db, schema, NULL, NULL, NULL) != SQLITE_OK) {
        sqlite3_close(*db);
        return -1;
    }

    return 0;
}

/* both items and categories now live in the same .db file */
int storage_default_path(char *buf, size_t buf_size) {
    char dir[1024];

    const char *xdg = getenv("XDG_DATA_HOME");
    if (xdg) {
        int n = snprintf(dir, sizeof(dir), "%s/plan", xdg);
        if (n < 0 || (size_t)n >= sizeof(dir)) return -1;
    } else {
        const char *home = getenv("HOME");
        if (!home) return -1;
        int n = snprintf(dir, sizeof(dir), "%s/.local/share/plan", home);
        if (n < 0 || (size_t)n >= sizeof(dir)) return -1;
    }

    if (mkdirp(dir) != 0) return -1;

    int n = snprintf(buf, buf_size, "%s/planc.db", dir);
    if (n < 0 || (size_t)n >= buf_size) return -1;

    return 0;
}

/* categories live in the same database — return the same path */
int storage_cat_path(char *buf, size_t buf_size) {
    return storage_default_path(buf, buf_size);
}

int storage_load(const char *path, PlanList *list) {
    sqlite3 *db;
    if (db_open(path, &db) != 0) return -1;

    sqlite3_stmt *stmt;
    const char *sql =
        "SELECT id, status, created_at, category_id, subcat_id, priority, text "
        "FROM items ORDER BY id";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return -1;
    }

    int rc = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PlanItem item;
        memset(&item, 0, sizeof(item));

        /* column 0 — id */
        item.id = sqlite3_column_int(stmt, 0);

        /* column 1 — status string → enum */
        const char *status_s = (const char *)sqlite3_column_text(stmt, 1);
        if (!status_s || plan_status_from_string(status_s, &item.status) != 0) {
            rc = -1; break;
        }

        /* column 2 — created_at: copy into fixed-size array */
        const char *ts = (const char *)sqlite3_column_text(stmt, 2);
        if (!ts || strlen(ts) != 20) { rc = -1; break; }
        memcpy(item.created_at, ts, 21);

        /* column 3, 4 — category_id and subcat_id (-1 = not assigned) */
        item.category_id = sqlite3_column_int(stmt, 3);
        item.subcat_id   = sqlite3_column_int(stmt, 4);

        /* column 5 — priority string → enum */
        const char *priority_s = (const char *)sqlite3_column_text(stmt, 5);
        if (!priority_s || plan_priority_from_string(priority_s, &item.priority) != 0) {
            rc = -1; break;
        }

        /* column 6 — text: heap-allocated copy, caller owns it */
        const char *text = (const char *)sqlite3_column_text(stmt, 6);
        if (!text) { rc = -1; break; }
        item.text = xstrdup(text);
        if (!item.text) { rc = -1; break; }

        if (plan_list_append(list, item) != 0) {
            free(item.text);
            rc = -1; break;
        }
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return rc;
}

int storage_save(const char *path, const PlanList *list) {
    sqlite3 *db;
    if (db_open(path, &db) != 0) return -1;

    /* transaction: either all rows are written or none — same guarantee
     * as the old tmp+rename trick but handled by SQLite natively */
    if (sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL) != SQLITE_OK) {
        sqlite3_close(db); return -1;
    }

    if (sqlite3_exec(db, "DELETE FROM items", NULL, NULL, NULL) != SQLITE_OK) {
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        sqlite3_close(db); return -1;
    }

    sqlite3_stmt *stmt;
    const char *sql =
        "INSERT INTO items "
        "(id, status, created_at, category_id, subcat_id, priority, text) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        sqlite3_close(db); return -1;
    }

    for (size_t i = 0; i < list->len; ++i) {
        const PlanItem *item = &list->items[i];

        /* bind each ? placeholder in order — type must match exactly */
        sqlite3_bind_int (stmt, 1, item->id);
        sqlite3_bind_text(stmt, 2, plan_status_to_string(item->status),    -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, item->created_at,                       -1, SQLITE_STATIC);
        sqlite3_bind_int (stmt, 4, item->category_id);
        sqlite3_bind_int (stmt, 5, item->subcat_id);
        sqlite3_bind_text(stmt, 6, plan_priority_to_string(item->priority), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 7, item->text,                             -1, SQLITE_STATIC);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            sqlite3_finalize(stmt);
            sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
            sqlite3_close(db); return -1;
        }

        /* reset the statement so it can be reused for the next row */
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
    }

    sqlite3_finalize(stmt);

    if (sqlite3_exec(db, "COMMIT", NULL, NULL, NULL) != SQLITE_OK) {
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        sqlite3_close(db); return -1;
    }

    sqlite3_close(db);
    return 0;
}

int storage_cat_load(const char *path, CategoryList *cats, SubcategoryList *subcats) {
    sqlite3 *db;
    if (db_open(path, &db) != 0) return -1;

    sqlite3_stmt *stmt;
    int rc = 0;

    /* load categories first */
    if (sqlite3_prepare_v2(db,
            "SELECT id, name FROM categories ORDER BY id",
            -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_close(db); return -1;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Category cat;
        cat.id = sqlite3_column_int(stmt, 0);
        const char *name = (const char *)sqlite3_column_text(stmt, 1);
        if (!name) { rc = -1; break; }
        cat.name = xstrdup(name);
        if (!cat.name) { rc = -1; break; }

        if (cats->len == cats->cap) {
            size_t new_cap = (cats->cap == 0) ? 8 : cats->cap * 2;
            Category *tmp = realloc(cats->items, new_cap * sizeof(*tmp));
            if (!tmp) { free(cat.name); rc = -1; break; }
            cats->items = tmp;
            cats->cap   = new_cap;
        }
        cats->items[cats->len++] = cat;
    }
    sqlite3_finalize(stmt);
    if (rc != 0) { sqlite3_close(db); return rc; }

    /* then load subcategories */
    if (sqlite3_prepare_v2(db,
            "SELECT id, category_id, name FROM subcategories ORDER BY id",
            -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_close(db); return -1;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Subcategory sub;
        sub.id          = sqlite3_column_int(stmt, 0);
        sub.category_id = sqlite3_column_int(stmt, 1);
        const char *name = (const char *)sqlite3_column_text(stmt, 2);
        if (!name) { rc = -1; break; }
        sub.name = xstrdup(name);
        if (!sub.name) { rc = -1; break; }

        if (subcats->len == subcats->cap) {
            size_t new_cap = (subcats->cap == 0) ? 8 : subcats->cap * 2;
            Subcategory *tmp = realloc(subcats->items, new_cap * sizeof(*tmp));
            if (!tmp) { free(sub.name); rc = -1; break; }
            subcats->items = tmp;
            subcats->cap   = new_cap;
        }
        subcats->items[subcats->len++] = sub;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return rc;
}

int storage_cat_save(const char *path, const CategoryList *cats, const SubcategoryList *subcats) {
    sqlite3 *db;
    if (db_open(path, &db) != 0) return -1;

    if (sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL) != SQLITE_OK) {
        sqlite3_close(db); return -1;
    }

    /* delete and reinsert both tables atomically */
    if (sqlite3_exec(db, "DELETE FROM categories",   NULL, NULL, NULL) != SQLITE_OK ||
        sqlite3_exec(db, "DELETE FROM subcategories", NULL, NULL, NULL) != SQLITE_OK) {
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        sqlite3_close(db); return -1;
    }

    sqlite3_stmt *stmt;

    /* insert categories */
    if (sqlite3_prepare_v2(db,
            "INSERT INTO categories (id, name) VALUES (?, ?)",
            -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        sqlite3_close(db); return -1;
    }

    for (size_t i = 0; i < cats->len; ++i) {
        sqlite3_bind_int (stmt, 1, cats->items[i].id);
        sqlite3_bind_text(stmt, 2, cats->items[i].name, -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            sqlite3_finalize(stmt);
            sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
            sqlite3_close(db); return -1;
        }
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
    }
    sqlite3_finalize(stmt);

    /* insert subcategories */
    if (sqlite3_prepare_v2(db,
            "INSERT INTO subcategories (id, category_id, name) VALUES (?, ?, ?)",
            -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        sqlite3_close(db); return -1;
    }

    for (size_t i = 0; i < subcats->len; ++i) {
        sqlite3_bind_int (stmt, 1, subcats->items[i].id);
        sqlite3_bind_int (stmt, 2, subcats->items[i].category_id);
        sqlite3_bind_text(stmt, 3, subcats->items[i].name, -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            sqlite3_finalize(stmt);
            sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
            sqlite3_close(db); return -1;
        }
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
    }
    sqlite3_finalize(stmt);

    if (sqlite3_exec(db, "COMMIT", NULL, NULL, NULL) != SQLITE_OK) {
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        sqlite3_close(db); return -1;
    }

    sqlite3_close(db);
    return 0;
}
