#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "category.h"
#include "storage.h"
#include "util.h"

#define LINE_BUF_SIZE 4096

static int parse_line(char *line, PlanItem *out) {
    /* parse all possible fields upfront; fields beyond what the line contains
       will be NULL — we use that to detect which format generation we are reading */
    char *id_s     = strtok(line, "|");
    char *status_s = strtok(NULL, "|");
    char *ts_s     = strtok(NULL, "|");
    char *field4   = strtok(NULL, "|");  /* gen1: text | gen2+: cat_id   */
    char *field5   = strtok(NULL, "|");  /* gen2+: subcat_id             */
    char *field6   = strtok(NULL, "|");  /* gen2: text  | gen3: priority */
    char *field7   = strtok(NULL, "");   /* gen3: text                   */

    if (!id_s || !status_s || !ts_s || !field4) {
        return -1;
    }

    if (parse_int(id_s, &out->id) != 0) {
        return -1;
    }

    if (plan_status_from_string(status_s, &out->status) != 0) {
        return -1;
    }

    if (strlen(ts_s) != 20) {
        return -1;
    }

    memcpy(out->created_at, ts_s, 21);

    if (field5 == NULL) {
        /* gen1 — 4 fields: id|status|created_at|text
           no category, no priority — apply defaults */
        out->category_id = -1;
        out->subcat_id   = -1;
        out->priority    = PRIO_NORMAL;
        rstrip_newline(field4);
        out->text = xstrdup(field4);

    } else if (field7 == NULL) {
        /* gen2 — 6 fields: id|status|created_at|cat_id|subcat_id|text
           has category but no priority — default priority to NORMAL */
        if (parse_int(field4, &out->category_id) != 0) return -1;
        if (parse_int(field5, &out->subcat_id)   != 0) return -1;
        out->priority = PRIO_NORMAL;
        rstrip_newline(field6);
        out->text = xstrdup(field6);

    } else {
        /* gen3 — 7 fields: id|status|created_at|cat_id|subcat_id|priority|text */
        if (parse_int(field4, &out->category_id)        != 0) return -1;
        if (parse_int(field5, &out->subcat_id)          != 0) return -1;
        if (plan_priority_from_string(field6, &out->priority) != 0) return -1;
        rstrip_newline(field7);
        out->text = xstrdup(field7);
    }

    if (!out->text) {
        return -1;
    }

    return 0;
}

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

    int n = snprintf(buf, buf_size, "%s/items", dir);
    if (n < 0 || (size_t)n >= buf_size) return -1;

    return 0;
}

int storage_load(const char *path, PlanList *list) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        if (errno == ENOENT) {
            return 0;
        }
        return -1;
    }

    char line[LINE_BUF_SIZE];
    while (fgets(line, sizeof(line), fp)) {
        PlanItem item;
        memset(&item, 0, sizeof(item));

        if (parse_line(line, &item) != 0) {
            fclose(fp);
            return -1;
        }

        if (plan_list_append(list, item) != 0) {
            free(item.text);
            fclose(fp);
            return -1;
        }
    }

    fclose(fp);
    return 0;
}

int storage_save(const char *path, const PlanList *list) {
    char tmp_path[1200];
    int n = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    if (n < 0 || (size_t)n >= sizeof(tmp_path)) {
        return -1;
    }

    FILE *fp = fopen(tmp_path, "w");
    if (!fp) return -1;

    for (size_t i = 0; i < list->len; ++i) {
        const PlanItem *item = &list->items[i];
        if (fprintf(fp, "%d|%s|%s|%d|%d|%s|%s\n",
                    item->id,
                    plan_status_to_string(item->status),
                    item->created_at,
                    item->category_id,
                    item->subcat_id,
                    plan_priority_to_string(item->priority),
                    item->text) < 0) {
            fclose(fp);
            return -1;
        }
    }

    if (fclose(fp) != 0) {
        return -1;
    }

    if (rename(tmp_path, path) != 0) {
        remove(tmp_path);
        return -1;
    }

    return 0;
}

int storage_cat_path(char *buf, size_t buf_size) {
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

    int n = snprintf(buf, buf_size, "%s/categories", dir);
    if (n < 0 || (size_t)n >= buf_size) return -1;

    return 0;
}

int storage_cat_load(const char *path, CategoryList *cats, SubcategoryList *subcats) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        if (errno == ENOENT) return 0;
        return -1;
    }

    char line[LINE_BUF_SIZE];
    while (fgets(line, sizeof(line), fp)) {
        rstrip_newline(line);

        char *type_s = strtok(line, "|");
        if (!type_s) continue;

        if (type_s[0] == 'C') {
            char *id_s   = strtok(NULL, "|");
            char *name_s = strtok(NULL, "");
            if (!id_s || !name_s) { fclose(fp); return -1; }

            Category cat;
            if (parse_int(id_s, &cat.id) != 0) { fclose(fp); return -1; }
            cat.name = xstrdup(name_s);
            if (!cat.name) { fclose(fp); return -1; }

            if (cats->len == cats->cap) {
                size_t new_cap = (cats->cap == 0) ? 8 : cats->cap * 2;
                Category *tmp = realloc(cats->items, new_cap * sizeof(*tmp));
                if (!tmp) { free(cat.name); fclose(fp); return -1; }
                cats->items = tmp;
                cats->cap = new_cap;
            }
            cats->items[cats->len++] = cat;

        } else if (type_s[0] == 'S') {
            char *id_s     = strtok(NULL, "|");
            char *cat_id_s = strtok(NULL, "|");
            char *name_s   = strtok(NULL, "");
            if (!id_s || !cat_id_s || !name_s) { fclose(fp); return -1; }

            Subcategory sub;
            if (parse_int(id_s, &sub.id) != 0)         { fclose(fp); return -1; }
            if (parse_int(cat_id_s, &sub.category_id) != 0) { fclose(fp); return -1; }
            sub.name = xstrdup(name_s);
            if (!sub.name) { fclose(fp); return -1; }

            if (subcats->len == subcats->cap) {
                size_t new_cap = (subcats->cap == 0) ? 8 : subcats->cap * 2;
                Subcategory *tmp = realloc(subcats->items, new_cap * sizeof(*tmp));
                if (!tmp) { free(sub.name); fclose(fp); return -1; }
                subcats->items = tmp;
                subcats->cap = new_cap;
            }
            subcats->items[subcats->len++] = sub;
        }
    }

    fclose(fp);
    return 0;
}

int storage_cat_save(const char *path, const CategoryList *cats, const SubcategoryList *subcats) {
    char tmp_path[1200];
    int n = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    if (n < 0 || (size_t)n >= sizeof(tmp_path)) return -1;

    FILE *fp = fopen(tmp_path, "w");
    if (!fp) return -1;

    for (size_t i = 0; i < cats->len; ++i) {
        if (fprintf(fp, "C|%d|%s\n", cats->items[i].id, cats->items[i].name) < 0) {
            fclose(fp); return -1;
        }
    }

    for (size_t i = 0; i < subcats->len; ++i) {
        if (fprintf(fp, "S|%d|%d|%s\n",
                    subcats->items[i].id,
                    subcats->items[i].category_id,
                    subcats->items[i].name) < 0) {
            fclose(fp); return -1;
        }
    }

    if (fclose(fp) != 0) return -1;

    if (rename(tmp_path, path) != 0) {
        remove(tmp_path);
        return -1;
    }

    return 0;
}