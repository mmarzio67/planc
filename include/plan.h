#ifndef PLAN_H
#define PLAN_H

#include <stddef.h>

typedef enum {
    PLAN_STATUS_OPEN = 0,
    PLAN_STATUS_DONE = 1
} PlanStatus;

typedef struct {
    int id;
    PlanStatus status;
    char created_at[21]; /* YYYY-MM-DDTHH:MM:SSZ */
    char *text;
} PlanItem;

typedef struct {
    PlanItem *items;
    size_t len;
    size_t cap;
} PlanList;

/* lifecycle */
void plan_list_init(PlanList *list);
void plan_list_free(PlanList *list);
int  plan_list_append(PlanList *list, PlanItem item);

/* lookup */
int plan_next_id(const PlanList *list);
PlanItem *plan_find_by_id(PlanList *list, int id);
const PlanItem *plan_find_by_id_const(const PlanList *list, int id);

/* operations */
int plan_add(PlanList *list, const char *text, const char *timestamp);
int plan_update(PlanList *list, int id, const char *text);
int plan_delete(PlanList *list, int id);
int plan_mark_done(PlanList *list, int id);

/* helpers */
const char *plan_status_to_string(PlanStatus status);
int plan_status_from_string(const char *s, PlanStatus *out);

#endif