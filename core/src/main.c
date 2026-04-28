#include <stdio.h>
#include <stdlib.h>

#include "category.h"
#include "cli.h"
#include "plan.h"
#include "storage.h"
#include "util.h"

static int compare_by_priority(const void *a, const void *b) {
    const PlanItem *ia = (const PlanItem *)a;
    const PlanItem *ib = (const PlanItem *)b;
    return ia->priority - ib->priority;
}

static void print_item(const PlanItem *item, const CategoryList *cats, const SubcategoryList *subcats) {
    printf("[%d] %-4s %-6s %s", item->id, 
             plan_status_to_string(item->status), 
             plan_priority_to_string(item->priority),
             item->created_at);

    if (item->category_id != -1) {
        const Category *cat = category_find_by_id(cats, item->category_id);
        printf(" [%s", cat ? cat->name : "?");

        if (item->subcat_id != -1) {
            /* find the subcategory name by matching both id and category_id */
            const char *subname = "?";
            for (size_t i = 0; i < subcats->len; ++i) {
                if (subcats->items[i].id == item->subcat_id) {
                    subname = subcats->items[i].name;
                    break;
                }
            }
            printf("/%s", subname);
        }
        printf("]");
    }

    printf(" %s\n", item->text);
}

int main(int argc, char **argv) {
    /* Parse argv into a typed Command struct — from this point on the code
     * never reads argv directly, it only acts on what cmd contains. */
    Command cmd = {0};
    /* cli_parse fills cmd and returns 0 on success, -1 on bad input —
     * both happen in the same expression before the condition is tested */
    if (cli_parse(argc, argv, &cmd) != 0) {
        cli_print_usage(argv[0]);
        return 1;
    }

    char path[1024];
    if (storage_default_path(path, sizeof(path)) != 0) {
        fprintf(stderr, "error: could not resolve storage path\n");
        return 2;
    }

    char cat_path[1024];
    if (storage_cat_path(cat_path, sizeof(cat_path)) != 0) {
        fprintf(stderr, "error: could not resolve categories path\n");
        return 2;
    }

    PlanList list;
    plan_list_init(&list);

    if (storage_load(path, &list) != 0) {
        fprintf(stderr, "error: could not load storage: %s\n", path);
        plan_list_free(&list);
        return 2;
    }

    CategoryList cats;
    SubcategoryList subcats;
    category_list_init(&cats);
    subcat_list_init(&subcats);

    if (storage_cat_load(cat_path, &cats, &subcats) != 0) {
        fprintf(stderr, "error: could not load categories: %s\n", cat_path);
        plan_list_free(&list);
        category_list_free(&cats);
        subcat_list_free(&subcats);
        return 2;
    }

    int rc = 0;

    switch (cmd.type) {
        case CMD_LIST:
            /* sort in place before iterating — only for display, does not affect storage */
            if (cmd.sortByPriority) {
                qsort(list.items, list.len, sizeof(PlanItem), compare_by_priority);
            }
            for (size_t i = 0; i < list.len; ++i) {
                if (cmd.show_all) {
                    /* --all: no status filtering, show everything */
                } else if (cmd.has_status) {
                    /* --status <value>: show only items matching that status */
                    if (list.items[i].status != cmd.status) continue;
                } else {
                    /* default: hide done and archived, show only actionable items */
                    if (list.items[i].status == PLAN_STATUS_DONE) continue;
                    if (list.items[i].status == PLAN_STATUS_ARCHIVED) continue;
                }
                if (cmd.has_priority && list.items[i].priority != cmd.priority) continue;
                print_item(&list.items[i], &cats, &subcats);
            }
            break;

            case CMD_SHOW: {
            const PlanItem *item = plan_find_by_id_const(&list, cmd.id);
            if (!item) {
                fprintf(stderr, "error: item %d not found\n", cmd.id);
                rc = 3;
                break;
            }
            print_item(item, &cats, &subcats);
            break;
        }

        case CMD_ADD: {
            /* resolve subcategory → category before creating the item */
            int cat_id = -1;
            if (cmd.subcat_id != -1) {
                int found = 0;
                for (size_t i = 0; i < subcats.len; ++i) {
                    if (subcats.items[i].id == cmd.subcat_id) {
                        cat_id = subcats.items[i].category_id;
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    fprintf(stderr, "error: subcategory %d not found\n", cmd.subcat_id);
                    rc = 3;
                    break;
                }
            }

            char ts[21];
            if (current_timestamp_utc(ts) != 0) {
                fprintf(stderr, "error: could not generate timestamp\n");
                rc = 2;
                break;
            }
            int id = plan_add(&list, cmd.text, ts, cat_id, cmd.subcat_id);
            if (id < 0 || storage_save(path, &list) != 0) {
                fprintf(stderr, "error: could not save item\n");
                rc = 2;
                break;
            }
            printf("added [%d]\n", id);
            break;
        }

        case CMD_UPDATE: {
            /* read the existing item first so we can preserve fields
               the user did not explicitly provide on this update */
            const PlanItem *existing = plan_find_by_id_const(&list, cmd.id);
            if (!existing) {
                fprintf(stderr, "error: item %d not found\n", cmd.id);
                rc = 3;
                break;
            }

            /* default: keep whatever is already stored on the item;
               cmd.text == NULL means --text was not passed, so text is preserved too */
            int cat_id    = existing->category_id;
            int subcat_id = existing->subcat_id;

            /* only if the user passed a new subcat_id do we validate and override */
            if (cmd.subcat_id != -1) {
                int found = 0;
                for (size_t i = 0; i < subcats.len; ++i) {
                    if (subcats.items[i].id == cmd.subcat_id) {
                        /* derive category_id from the subcategory record —
                           the user only needs to know the subcat id */
                        cat_id    = subcats.items[i].category_id;
                        subcat_id = cmd.subcat_id;
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    fprintf(stderr, "error: subcategory %d not found\n", cmd.subcat_id);
                    rc = 3;
                    break;
                }
            }
            PlanPriority priority = existing->priority;  /* default: keep existing */
            if (cmd.has_priority) {
                priority = cmd.priority;                 /* override only if --priority was given */
            }
            PlanStatus status = existing->status;      /* default: keep existing */
            if (cmd.has_status) {
                status = cmd.status;                    /* override only if --status was given */
            }   

            if (plan_update(&list, cmd.id, cmd.text, cat_id, subcat_id, priority, status) != 0 ||
                storage_save(path, &list) != 0) {
                fprintf(stderr, "error: could not update item %d\n", cmd.id);
                rc = 3;
            } else {
                printf("updated [%d]\n", cmd.id);
            }
            break;
        }

        case CMD_DELETE:
            if (plan_delete(&list, cmd.id) != 0 ||
                storage_save(path, &list) != 0) {
                fprintf(stderr, "error: could not delete item %d\n", cmd.id);
                rc = 3;
            } else {
                printf("deleted [%d]\n", cmd.id);
            }
            break;

        case CMD_DONE:
            if (plan_mark_done(&list, cmd.id) != 0 ||
                storage_save(path, &list) != 0) {
                fprintf(stderr, "error: could not mark item %d done\n", cmd.id);
                rc = 3;
            } else {
                printf("done [%d]\n", cmd.id);
            }
            break;

        case CMD_CAT_ADD: {
            int id = category_add(&cats, cmd.text);
            if (id < 0 || storage_cat_save(cat_path, &cats, &subcats) != 0) {
                fprintf(stderr, "error: could not save category\n");
                rc = 2;
                break;
            }
            printf("category added [%d]\n", id);
            break;
        }

        case CMD_CAT_LIST:
            for (size_t i = 0; i < cats.len; ++i) {
                printf("[%d] %s\n", cats.items[i].id, cats.items[i].name);
            }
            break;

        case CMD_SUBCAT_ADD: {
            if (!category_find_by_id(&cats, cmd.id)) {
                fprintf(stderr, "error: category %d not found\n", cmd.id);
                rc = 3;
                break;
            }
            int id = subcat_add(&subcats, cmd.id, cmd.text);
            if (id < 0 || storage_cat_save(cat_path, &cats, &subcats) != 0) {
                fprintf(stderr, "error: could not save subcategory\n");
                rc = 2;
                break;
            }
            printf("subcategory added [%d]\n", id);
            break;
        }

        case CMD_SUBCAT_LIST:
            for (size_t i = 0; i < subcats.len; ++i) {
                const Category *cat = category_find_by_id(&cats, subcats.items[i].category_id);
                printf("[%d] %s / %s\n",
                       subcats.items[i].id,
                       cat ? cat->name : "?",
                       subcats.items[i].name);
            }
            break;

        default:
            cli_print_usage(argv[0]);
            rc = 1;
            break;
    }

    plan_list_free(&list);
    category_list_free(&cats);
    subcat_list_free(&subcats);
    return rc;
}