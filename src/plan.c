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

int plan_add(PlanList *list, const char *text, const char *timestamp, int category_id, int subcat_id) {
    PlanItem item;
    item.id = plan_next_id(list);
    item.status = PLAN_STATUS_BORN;
    item.priority = PRIO_NORMAL;
    memcpy(item.created_at, timestamp, 21);
    item.category_id = category_id;
    item.subcat_id = subcat_id;
    item.text = xstrdup(text);
    if (!item.text) return -1;

    if (plan_list_append(list, item) != 0) {
        free(item.text);
        return -1;
    }

    return item.id;
}

int plan_update(PlanList *list, int id, const char *text, int category_id, int subcat_id, PlanPriority priority, PlanStatus status) {
    PlanItem *item = plan_find_by_id(list, id);
    if (!item) return -1;

    /* text is optional — NULL means the caller wants to preserve the existing text */
    if (text != NULL) {
        char *dup = xstrdup(text);
        if (!dup) return -1;
        free(item->text);
        item->text = dup;
    }

    item->category_id = category_id;
    item->subcat_id   = subcat_id;
    item->priority    = priority;
    item->status      = status;
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
        case PLAN_STATUS_BORN: return "born";
        case PLAN_STATUS_OPEN: return "open";
        case PLAN_STATUS_DONE: return "done";
        case PLAN_STATUS_WAITING: return "waiting";
        case PLAN_STATUS_ARCHIVED: return "archived";
        default: return "open";
    }
}

int plan_status_from_string(const char *s, PlanStatus *out) {
    if (strcmp(s, "born") == 0) {
        *out = PLAN_STATUS_BORN;
        return 0;
    }
    if (strcmp(s, "open") == 0) {
        *out = PLAN_STATUS_OPEN;
        return 0;
    }
    if (strcmp(s, "done") == 0) {
        *out = PLAN_STATUS_DONE;
        return 0;
    }
    if (strcmp(s, "waiting") == 0) {
        *out = PLAN_STATUS_WAITING;
        return 0;
    }
    if (strcmp(s, "archived") == 0) {
        *out = PLAN_STATUS_ARCHIVED;
        return 0;
    }
    return -1;
}


const char *plan_priority_to_string(PlanPriority priority) {
    switch (priority) {
        case PRIO_TODAY: return "today";
        case PRIO_URGENT: return "urgent";
        case PRIO_HIGH: return "high";
        case PRIO_NORMAL: return "normal";
        case PRIO_LOW: return "low";
        default: return "normal";
    }
}

int plan_priority_from_string(const char *s, PlanPriority *out) {
    if (strcmp(s, "today") == 0) {
        *out = PRIO_TODAY;
        return 0;
    }
    if (strcmp(s, "urgent") == 0) {
        *out = PRIO_URGENT;
        return 0;
    }
    if (strcmp(s, "high") == 0) {
        *out = PRIO_HIGH;
        return 0;
    }
    if (strcmp(s, "normal") == 0) {
        *out = PRIO_NORMAL;
        return 0;
    }
    if (strcmp(s, "low") == 0) {
        *out = PRIO_LOW;
        return 0;
    }
    return -1;
}   