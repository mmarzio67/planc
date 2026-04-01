#include <stdio.h>
#include <stdlib.h>

#include "cli.h"
#include "plan.h"
#include "storage.h"
#include "util.h"

static void print_item(const PlanItem *item) {
    printf("[%d] %-4s %s %s\n",
           item->id,
           plan_status_to_string(item->status),
           item->created_at,
           item->text);
}

int main(int argc, char **argv) {
    Command cmd = {0};
    if (cli_parse(argc, argv, &cmd) != 0) {
        cli_print_usage(argv[0]);
        return 1;
    }

    char path[1024];
    if (storage_default_path(path, sizeof(path)) != 0) {
        fprintf(stderr, "error: could not resolve storage path\n");
        return 2;
    }

    PlanList list;
    plan_list_init(&list);

    if (storage_load(path, &list) != 0) {
        fprintf(stderr, "error: could not load storage: %s\n", path);
        plan_list_free(&list);
        return 2;
    }

    int rc = 0;

    switch (cmd.type) {
        case CMD_LIST:
            for (size_t i = 0; i < list.len; ++i) {
                print_item(&list.items[i]);
            }
            break;

        case CMD_SHOW: {
            const PlanItem *item = plan_find_by_id_const(&list, cmd.id);
            if (!item) {
                fprintf(stderr, "error: item %d not found\n", cmd.id);
                rc = 3;
                break;
            }
            print_item(item);
            break;
        }

        case CMD_ADD: {
            char ts[21];
            if (current_timestamp_utc(ts) != 0) {
                fprintf(stderr, "error: could not generate timestamp\n");
                rc = 2;
                break;
            }
            int id = plan_add(&list, cmd.text, ts);
            if (id < 0 || storage_save(path, &list) != 0) {
                fprintf(stderr, "error: could not save item\n");
                rc = 2;
                break;
            }
            printf("added [%d]\n", id);
            break;
        }

        case CMD_UPDATE:
            if (plan_update(&list, cmd.id, cmd.text) != 0 ||
                storage_save(path, &list) != 0) {
                fprintf(stderr, "error: could not update item %d\n", cmd.id);
                rc = 3;
            } else {
                printf("updated [%d]\n", cmd.id);
            }
            break;

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

        default:
            cli_print_usage(argv[0]);
            rc = 1;
            break;
    }

    plan_list_free(&list);
    return rc;
}