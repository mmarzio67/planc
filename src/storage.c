#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "storage.h"
#include "util.h"

#define LINE_BUF_SIZE 4096

static int parse_line(char *line, PlanItem *out) {
    char *id_s = strtok(line, "|");
    char *status_s = strtok(NULL, "|");
    char *ts_s = strtok(NULL, "|");
    char *text_s = strtok(NULL, "");

    if (!id_s || !status_s || !ts_s || !text_s) {
        return -1;
    }

    rstrip_newline(text_s);

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

    out->text = xstrdup(text_s);
    if (!out->text) {
        return -1;
    }

    return 0;
}

int storage_default_path(char *buf, size_t buf_size) {
    const char *home = getenv("HOME");
    if (!home) return -1;

    int n = snprintf(buf, buf_size, "%s/.plan", home);
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
        if (fprintf(fp, "%d|%s|%s|%s\n",
                    item->id,
                    plan_status_to_string(item->status),
                    item->created_at,
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