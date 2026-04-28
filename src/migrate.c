#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "category.h"
#include "plan.h"
#include "storage.h"
#include "util.h"

#define LINE_BUF 4096

/* -----------------------------------------------------------------------
 * Old flat-file parsers — reproduced here so migrate.c is self-contained.
 * These functions understand all three generations of the items format:
 *
 *   gen1 (4 fields):  id|status|created_at|text
 *   gen2 (6 fields):  id|status|created_at|cat_id|subcat_id|text
 *   gen3 (7 fields):  id|status|created_at|cat_id|subcat_id|priority|text
 * ----------------------------------------------------------------------- */
static int parse_item_line(char *line, PlanItem *out) {
    char *id_s     = strtok(line, "|");
    char *status_s = strtok(NULL, "|");
    char *ts_s     = strtok(NULL, "|");
    char *field4   = strtok(NULL, "|");
    char *field5   = strtok(NULL, "|");
    char *field6   = strtok(NULL, "|");
    char *field7   = strtok(NULL, "");

    if (!id_s || !status_s || !ts_s || !field4) return -1;

    if (parse_int(id_s, &out->id) != 0) return -1;
    if (plan_status_from_string(status_s, &out->status) != 0) return -1;
    if (strlen(ts_s) != 20) return -1;
    memcpy(out->created_at, ts_s, 21);

    if (field5 == NULL) {
        /* gen1 — no category, no priority */
        out->category_id = -1;
        out->subcat_id   = -1;
        out->priority    = PRIO_NORMAL;
        rstrip_newline(field4);
        out->text = xstrdup(field4);
    } else if (field7 == NULL) {
        /* gen2 — has category, no priority */
        if (parse_int(field4, &out->category_id) != 0) return -1;
        if (parse_int(field5, &out->subcat_id)   != 0) return -1;
        out->priority = PRIO_NORMAL;
        rstrip_newline(field6);
        out->text = xstrdup(field6);
    } else {
        /* gen3 — has category and priority */
        if (parse_int(field4, &out->category_id)             != 0) return -1;
        if (parse_int(field5, &out->subcat_id)               != 0) return -1;
        if (plan_priority_from_string(field6, &out->priority) != 0) return -1;
        rstrip_newline(field7);
        out->text = xstrdup(field7);
    }

    return out->text ? 0 : -1;
}

/* Old categories file format:
 *   C|id|name          — a category
 *   S|id|cat_id|name   — a subcategory */
static int parse_cat_line(char *line,
                          CategoryList *cats, SubcategoryList *subcats) {
    rstrip_newline(line);

    char *type_s = strtok(line, "|");
    if (!type_s) return 0;  /* blank line — skip silently */

    if (type_s[0] == 'C') {
        char *id_s   = strtok(NULL, "|");
        char *name_s = strtok(NULL, "");
        if (!id_s || !name_s) return -1;

        Category cat;
        if (parse_int(id_s, &cat.id) != 0) return -1;
        cat.name = xstrdup(name_s);
        if (!cat.name) return -1;

        if (cats->len == cats->cap) {
            size_t nc = (cats->cap == 0) ? 8 : cats->cap * 2;
            Category *tmp = realloc(cats->items, nc * sizeof(*tmp));
            if (!tmp) { free(cat.name); return -1; }
            cats->items = tmp;
            cats->cap   = nc;
        }
        cats->items[cats->len++] = cat;

    } else if (type_s[0] == 'S') {
        char *id_s     = strtok(NULL, "|");
        char *cat_id_s = strtok(NULL, "|");
        char *name_s   = strtok(NULL, "");
        if (!id_s || !cat_id_s || !name_s) return -1;

        Subcategory sub;
        if (parse_int(id_s,     &sub.id)          != 0) return -1;
        if (parse_int(cat_id_s, &sub.category_id) != 0) return -1;
        sub.name = xstrdup(name_s);
        if (!sub.name) return -1;

        if (subcats->len == subcats->cap) {
            size_t nc = (subcats->cap == 0) ? 8 : subcats->cap * 2;
            Subcategory *tmp = realloc(subcats->items, nc * sizeof(*tmp));
            if (!tmp) { free(sub.name); return -1; }
            subcats->items = tmp;
            subcats->cap   = nc;
        }
        subcats->items[subcats->len++] = sub;
    }

    return 0;
}

/* -----------------------------------------------------------------------
 * Build the old flat-file paths from the same XDG logic used by storage.c.
 * The old files sit in the same directory as the new planc.db.
 * ----------------------------------------------------------------------- */
static int old_items_path(char *buf, size_t buf_size) {
    char dir[1024];
    const char *xdg = getenv("XDG_DATA_HOME");
    if (xdg) {
        snprintf(dir, sizeof(dir), "%s/plan", xdg);
    } else {
        const char *home = getenv("HOME");
        if (!home) return -1;
        snprintf(dir, sizeof(dir), "%s/.local/share/plan", home);
    }
    int n = snprintf(buf, buf_size, "%s/items", dir);
    return (n < 0 || (size_t)n >= buf_size) ? -1 : 0;
}

static int old_cats_path(char *buf, size_t buf_size) {
    char dir[1024];
    const char *xdg = getenv("XDG_DATA_HOME");
    if (xdg) {
        snprintf(dir, sizeof(dir), "%s/plan", xdg);
    } else {
        const char *home = getenv("HOME");
        if (!home) return -1;
        snprintf(dir, sizeof(dir), "%s/.local/share/plan", home);
    }
    int n = snprintf(buf, buf_size, "%s/categories", dir);
    return (n < 0 || (size_t)n >= buf_size) ? -1 : 0;
}

/* ----------------------------------------------------------------------- */

int main(void) {
    char items_path[1024], cats_path[1024], db_path[1024];

    if (old_items_path(items_path, sizeof(items_path)) != 0 ||
        old_cats_path(cats_path,   sizeof(cats_path))  != 0 ||
        storage_default_path(db_path, sizeof(db_path)) != 0) {
        fprintf(stderr, "error: could not resolve paths\n");
        return 1;
    }

    printf("items file : %s\n", items_path);
    printf("categories : %s\n", cats_path);
    printf("target db  : %s\n\n", db_path);

    /* --- read old items file ------------------------------------------ */
    PlanList list;
    plan_list_init(&list);

    FILE *fp = fopen(items_path, "r");
    if (!fp) {
        /* ENOENT just means no old data — not an error */
        if (errno == ENOENT)
            printf("no old items file found — nothing to migrate\n");
        else
            fprintf(stderr, "error: cannot open %s\n", items_path);
        plan_list_free(&list);
        return errno == ENOENT ? 0 : 1;
    }

    char line[LINE_BUF];
    int line_num = 0;
    while (fgets(line, sizeof(line), fp)) {
        ++line_num;
        PlanItem item;
        memset(&item, 0, sizeof(item));
        if (parse_item_line(line, &item) != 0) {
            fprintf(stderr, "error: malformed item on line %d — aborting\n", line_num);
            fclose(fp);
            plan_list_free(&list);
            return 1;
        }
        if (plan_list_append(&list, item) != 0) {
            free(item.text);
            fclose(fp);
            plan_list_free(&list);
            return 1;
        }
    }
    fclose(fp);
    printf("read %zu items\n", list.len);

    /* --- read old categories file ------------------------------------- */
    CategoryList    cats;
    SubcategoryList subcats;
    category_list_init(&cats);
    subcat_list_init(&subcats);

    fp = fopen(cats_path, "r");
    if (fp) {
        line_num = 0;
        while (fgets(line, sizeof(line), fp)) {
            ++line_num;
            if (parse_cat_line(line, &cats, &subcats) != 0) {
                fprintf(stderr, "error: malformed category on line %d — aborting\n", line_num);
                fclose(fp);
                plan_list_free(&list);
                category_list_free(&cats);
                subcat_list_free(&subcats);
                return 1;
            }
        }
        fclose(fp);
    }
    printf("read %zu categories, %zu subcategories\n", cats.len, subcats.len);

    /* --- write everything to SQLite ----------------------------------- */
    /* storage_save and storage_cat_save are the same functions the app
     * uses — the migration reuses them directly rather than talking to
     * SQLite itself. This guarantees the same schema and format. */
    printf("\nwriting to %s ...\n", db_path);

    if (storage_cat_save(db_path, &cats, &subcats) != 0) {
        fprintf(stderr, "error: could not save categories\n");
        plan_list_free(&list);
        category_list_free(&cats);
        subcat_list_free(&subcats);
        return 1;
    }

    if (storage_save(db_path, &list) != 0) {
        fprintf(stderr, "error: could not save items\n");
        plan_list_free(&list);
        category_list_free(&cats);
        subcat_list_free(&subcats);
        return 1;
    }

    printf("migration complete — %zu items, %zu categories, %zu subcategories\n",
           list.len, cats.len, subcats.len);

    plan_list_free(&list);
    category_list_free(&cats);
    subcat_list_free(&subcats);
    return 0;
}
