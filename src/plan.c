#include <stdlib.h>
#include <string.h>

#include "plan.h"
#include "util.h"

void plan_list_init(PlanList *list) {
    list->items = NULL;
    list->len = 0;
    list->cap = 0;
}

void plan_list_free(PlanList *list) {
    if (!list) return;
    for (size_t i = 0; i < list->len; ++i) {
        free(list->items[i].text);
    }
    free(list->items);
    list->items = NULL;
    list->len = 0;
    list->cap = 0;
}

int plan_list_append(PlanList *list, PlanItem item) {
    if (list->len == list->cap) {
        size_t new_cap = (list->cap == 0) ? 8 : list->cap * 2;
        PlanItem *new_items = realloc(list->items, new_cap * sizeof(*new_items));
        if (!new_items) return -1;
        list->items = new_items;
        list->cap = new_cap;
    }
    list->items[list->len++] = item;
    return 0;
}

int plan_next_id(const PlanList *list) {
    int max_id = 0;
    for (size_t i = 0; i < list->len; ++i) {
        if (list->items[i].id > max_id) {
            max_id = list->items[i].id;
        }
    }
    return max_id + 1;
}

PlanItem *plan_find_by_id(PlanList *list, int id) {
    for (size_t i = 0; i < list->len; ++i) {
        if (list->items[i].id == id) {
            return &list->items[i];
        }
    }
    return NULL;
}

const PlanItem *plan_find_by_id_const(const PlanList *list, int id) {
    for (size_t i = 0; i < list->len; ++i) {
        if (list->items[i].id == id) {
            return &list->items[i];
        }
    }
    return NULL;
}

int plan_add(PlanList *list, const char *text, const char *timestamp) {
    PlanItem item;
    item.id = plan_next_id(list);
    item.status = PLAN_STATUS_OPEN;
    memcpy(item.created_at, timestamp, 21);
    item.text = xstrdup(text);
    if (!item.text) return -1;

    if (plan_list_append(list, item) != 0) {
        free(item.text);
        return -1;
    }

    return item.id;
}

int plan_update(PlanList *list, int id, const char *text) {
    PlanItem *item = plan_find_by_id(list, id);
    if (!item) return -1;

    char *dup = xstrdup(text);
    if (!dup) return -1;

    free(item->text);
    item->text = dup;
    return 0;
}

int plan_delete(PlanList *list, int id) {
    for (size_t i = 0; i < list->len; ++i) {
        if (list->items[i].id == id) {
            free(list->items[i].text);
            for (size_t j = i; j + 1 < list->len; ++j) {
                list->items[j] = list->items[j + 1];
            }
            list->len--;
            return 0;
        }
    }
    return -1;
}

int plan_mark_done(PlanList *list, int id) {
    PlanItem *item = plan_find_by_id(list, id);
    if (!item) return -1;
    item->status = PLAN_STATUS_DONE;
    return 0;
}

const char *plan_status_to_string(PlanStatus status) {
    switch (status) {
        case PLAN_STATUS_OPEN: return "open";
        case PLAN_STATUS_DONE: return "done";
        default: return "open";
    }
}

int plan_status_from_string(const char *s, PlanStatus *out) {
    if (strcmp(s, "open") == 0) {
        *out = PLAN_STATUS_OPEN;
        return 0;
    }
    if (strcmp(s, "done") == 0) {
        *out = PLAN_STATUS_DONE;
        return 0;
    }
    return -1;
}