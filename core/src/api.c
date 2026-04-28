#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "category.h"
#include "plan.h"
#include "planc_api.h"
#include "storage.h"
#include "strbuf.h"
#include "util.h"

static int item_to_json(Strbuf *sb, const PlanItem *item) {
    if (sb_append(sb, "{\"id\":") != 0)                                     return -1;
    if (sb_append_int(sb, item->id) != 0)                                   return -1;
    if (sb_append(sb, ",\"status\":") != 0)                                 return -1;
    if (sb_append_json_str(sb, plan_status_to_string(item->status)) != 0)   return -1;
    if (sb_append(sb, ",\"priority\":") != 0)                               return -1;
    if (sb_append_json_str(sb, plan_priority_to_string(item->priority)) != 0) return -1;
    if (sb_append(sb, ",\"created_at\":") != 0)                             return -1;
    if (sb_append_json_str(sb, item->created_at) != 0)                      return -1;
    if (sb_append(sb, ",\"category_id\":") != 0)                            return -1;
    if (sb_append_int(sb, item->category_id) != 0)                          return -1;
    if (sb_append(sb, ",\"subcat_id\":") != 0)                              return -1;
    if (sb_append_int(sb, item->subcat_id) != 0)                            return -1;
    if (sb_append(sb, ",\"text\":") != 0)                                   return -1;
    if (sb_append_json_str(sb, item->text) != 0)                            return -1;
    return sb_append(sb, "}");
}

/* ----------------------------------------------------------------------- */

char *planc_list(const char *db_path,
                 int show_all,
                 int has_status,   const char *status_s,
                 int has_priority, const char *priority_s) {
    PlanList list;
    plan_list_init(&list);
    if (storage_load(db_path, &list) != 0) {
        plan_list_free(&list);
        return NULL;
    }

    PlanStatus   filter_status   = PLAN_STATUS_OPEN;
    PlanPriority filter_priority = PRIO_NORMAL;

    if (has_status   && plan_status_from_string(status_s,     &filter_status)   != 0) goto fail_list;
    if (has_priority && plan_priority_from_string(priority_s, &filter_priority) != 0) goto fail_list;

    {
        Strbuf sb;
        if (sb_init(&sb) != 0) goto fail_list;
        if (sb_append(&sb, "[") != 0) goto fail_sb;

        int first = 1;
        for (size_t i = 0; i < list.len; ++i) {
            const PlanItem *item = &list.items[i];
            if (!show_all) {
                if (has_status) {
                    if (item->status != filter_status) continue;
                } else {
                    if (item->status == PLAN_STATUS_DONE)     continue;
                    if (item->status == PLAN_STATUS_ARCHIVED) continue;
                }
            }
            if (has_priority && item->priority != filter_priority) continue;

            if (!first && sb_append(&sb, ",") != 0) goto fail_sb;
            if (item_to_json(&sb, item) != 0) goto fail_sb;
            first = 0;
        }

        if (sb_append(&sb, "]") != 0) goto fail_sb;
        plan_list_free(&list);
        return sb.buf;

    fail_sb:
        free(sb.buf);
    }

fail_list:
    plan_list_free(&list);
    return NULL;
}

int planc_add(const char *db_path, const char *text,
              int cat_id, int subcat_id,
              char *err_buf, size_t err_size) {
    PlanList list;
    plan_list_init(&list);

    if (storage_load(db_path, &list) != 0) {
        snprintf(err_buf, err_size, "could not load storage");
        plan_list_free(&list);
        return -1;
    }

    char ts[21];
    if (current_timestamp_utc(ts) != 0) {
        snprintf(err_buf, err_size, "could not generate timestamp");
        plan_list_free(&list);
        return -1;
    }

    int id = plan_add(&list, text, ts, cat_id, subcat_id);
    if (id < 0) {
        snprintf(err_buf, err_size, "could not add item");
        plan_list_free(&list);
        return -1;
    }

    if (storage_save(db_path, &list) != 0) {
        snprintf(err_buf, err_size, "could not save storage");
        plan_list_free(&list);
        return -1;
    }

    plan_list_free(&list);
    return id;
}

int planc_update(const char *db_path, int id,
                 const char *text,
                 int cat_id, int subcat_id,
                 const char *priority_s, const char *status_s,
                 char *err_buf, size_t err_size) {
    PlanList list;
    plan_list_init(&list);

    if (storage_load(db_path, &list) != 0) {
        snprintf(err_buf, err_size, "could not load storage");
        plan_list_free(&list);
        return -1;
    }

    const PlanItem *existing = plan_find_by_id_const(&list, id);
    if (!existing) {
        snprintf(err_buf, err_size, "item %d not found", id);
        plan_list_free(&list);
        return -1;
    }

    /* -2 = keep current; -1 = unassign; >= 0 = set to this id */
    int use_cat_id    = (cat_id    != -2) ? cat_id    : existing->category_id;
    int use_subcat_id = (subcat_id != -2) ? subcat_id : existing->subcat_id;

    PlanPriority priority = existing->priority;
    if (priority_s && plan_priority_from_string(priority_s, &priority) != 0) {
        snprintf(err_buf, err_size, "invalid priority: %s", priority_s);
        plan_list_free(&list);
        return -1;
    }

    PlanStatus status = existing->status;
    if (status_s && plan_status_from_string(status_s, &status) != 0) {
        snprintf(err_buf, err_size, "invalid status: %s", status_s);
        plan_list_free(&list);
        return -1;
    }

    if (plan_update(&list, id, text, use_cat_id, use_subcat_id, priority, status) != 0 ||
        storage_save(db_path, &list) != 0) {
        snprintf(err_buf, err_size, "could not update item %d", id);
        plan_list_free(&list);
        return -1;
    }

    plan_list_free(&list);
    return 0;
}

int planc_delete(const char *db_path, int id, char *err_buf, size_t err_size) {
    PlanList list;
    plan_list_init(&list);

    if (storage_load(db_path, &list) != 0) {
        snprintf(err_buf, err_size, "could not load storage");
        plan_list_free(&list);
        return -1;
    }

    if (plan_delete(&list, id) != 0) {
        snprintf(err_buf, err_size, "item %d not found", id);
        plan_list_free(&list);
        return -1;
    }

    if (storage_save(db_path, &list) != 0) {
        snprintf(err_buf, err_size, "could not save storage");
        plan_list_free(&list);
        return -1;
    }

    plan_list_free(&list);
    return 0;
}

char *planc_categories(const char *db_path) {
    CategoryList    cats;
    SubcategoryList subcats;
    category_list_init(&cats);
    subcat_list_init(&subcats);
    Strbuf sb;
    sb.buf = NULL;

    if (storage_cat_load(db_path, &cats, &subcats) != 0) goto fail;
    if (sb_init(&sb) != 0) goto fail;

    if (sb_append(&sb, "{\"categories\":[") != 0) goto fail;
    for (size_t i = 0; i < cats.len; ++i) {
        if (i > 0 && sb_append(&sb, ",") != 0) goto fail;
        if (sb_append(&sb, "{\"id\":") != 0) goto fail;
        if (sb_append_int(&sb, cats.items[i].id) != 0) goto fail;
        if (sb_append(&sb, ",\"name\":") != 0) goto fail;
        if (sb_append_json_str(&sb, cats.items[i].name) != 0) goto fail;
        if (sb_append(&sb, "}") != 0) goto fail;
    }

    if (sb_append(&sb, "],\"subcategories\":[") != 0) goto fail;
    for (size_t i = 0; i < subcats.len; ++i) {
        if (i > 0 && sb_append(&sb, ",") != 0) goto fail;
        if (sb_append(&sb, "{\"id\":") != 0) goto fail;
        if (sb_append_int(&sb, subcats.items[i].id) != 0) goto fail;
        if (sb_append(&sb, ",\"category_id\":") != 0) goto fail;
        if (sb_append_int(&sb, subcats.items[i].category_id) != 0) goto fail;
        if (sb_append(&sb, ",\"name\":") != 0) goto fail;
        if (sb_append_json_str(&sb, subcats.items[i].name) != 0) goto fail;
        if (sb_append(&sb, "}") != 0) goto fail;
    }

    if (sb_append(&sb, "]}") != 0) goto fail;

    category_list_free(&cats);
    subcat_list_free(&subcats);
    return sb.buf;

fail:
    free(sb.buf);
    category_list_free(&cats);
    subcat_list_free(&subcats);
    return NULL;
}

int planc_cat_add(const char *db_path, const char *name,
                  char *err_buf, size_t err_size) {
    CategoryList    cats;
    SubcategoryList subcats;
    category_list_init(&cats);
    subcat_list_init(&subcats);

    if (storage_cat_load(db_path, &cats, &subcats) != 0) {
        snprintf(err_buf, err_size, "could not load categories");
        goto fail;
    }

    int id = category_add(&cats, name);
    if (id < 0) {
        snprintf(err_buf, err_size, "could not add category");
        goto fail;
    }

    if (storage_cat_save(db_path, &cats, &subcats) != 0) {
        snprintf(err_buf, err_size, "could not save categories");
        goto fail;
    }

    category_list_free(&cats);
    subcat_list_free(&subcats);
    return id;

fail:
    category_list_free(&cats);
    subcat_list_free(&subcats);
    return -1;
}

int planc_subcat_add(const char *db_path, int cat_id, const char *name,
                     char *err_buf, size_t err_size) {
    CategoryList    cats;
    SubcategoryList subcats;
    category_list_init(&cats);
    subcat_list_init(&subcats);

    if (storage_cat_load(db_path, &cats, &subcats) != 0) {
        snprintf(err_buf, err_size, "could not load categories");
        goto fail;
    }

    if (!category_find_by_id(&cats, cat_id)) {
        snprintf(err_buf, err_size, "category %d not found", cat_id);
        goto fail;
    }

    int id = subcat_add(&subcats, cat_id, name);
    if (id < 0) {
        snprintf(err_buf, err_size, "could not add subcategory");
        goto fail;
    }

    if (storage_cat_save(db_path, &cats, &subcats) != 0) {
        snprintf(err_buf, err_size, "could not save categories");
        goto fail;
    }

    category_list_free(&cats);
    subcat_list_free(&subcats);
    return id;

fail:
    category_list_free(&cats);
    subcat_list_free(&subcats);
    return -1;
}

void planc_free(char *ptr) {
    free(ptr);
}
