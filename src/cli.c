#include <stdio.h>
#include <string.h>

#include "cli.h"
#include "util.h"

/** Parses command-line arguments into a Command struct.
 * Bridges argv (raw strings) and main() (typed execution) —
 * main() never touches argv, it only reads from the filled struct.
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @param out  Output command structure, filled on success
 * @return 0 on success, -1 on invalid or unrecognized arguments
 */

int cli_parse(int argc, char **argv, Command *out) {
    if (argc < 2 || !out) {
        return -1;
    }

    /* zero every field in *out — all ints become 0, all pointers become NULL;
     * this means has_status, has_priority, subcat_id etc. start in their
     * "not provided" state */
    memset(out, 0, sizeof(*out));

    if (strcmp(argv[1], "list") == 0) {
        out->type = CMD_LIST;

        /* walk optional flags — same pattern as update */
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "--status") == 0) {
                if (i + 1 >= argc) return -1;
                if (plan_status_from_string(argv[++i], &out->status) != 0) return -1;
                out->has_status = 1;
            } else if (strcmp(argv[i], "--priority") == 0) {
                if (i + 1 >= argc) return -1;
                if (plan_priority_from_string(argv[++i], &out->priority) != 0) return -1;
                out->has_priority = 1;  
            } else if (strcmp(argv[i], "--subcat") == 0) {
                if (i + 1 >= argc) return -1;
                if (parse_int(argv[++i], &out->subcat_id) != 0) return -1;
                out->has_subcat = 1;        
            } else if (strcmp(argv[i], "--sort-priority") == 0) {    
                out->sortByPriority = 1;
            } else if (strcmp(argv[i], "--all") == 0) {
                out->show_all = 1;  /* no ++i — no value to consume */
            } else {
                return -1;  /* unknown flag */
            }
        }

        return 0;
    }

    if (strcmp(argv[1], "add") == 0) {
        if (argc == 3) {
            out->type = CMD_ADD;
            out->text = argv[2];
            out->subcat_id = -1;
            return 0;
        }
        if (argc == 4) {
            out->type = CMD_ADD;
            out->text = argv[2];
            if (parse_int(argv[3], &out->subcat_id) != 0) return -1;
            return 0;
        }
        return -1;
    }

    if (strcmp(argv[1], "update") == 0) {
        /* requires at least: update <id> <one flag> <value> */
        if (argc < 4) return -1;
        if (parse_int(argv[2], &out->id) != 0) return -1;
        out->type = CMD_UPDATE;
        out->text = NULL;      /* NULL = not provided, preserve existing */
        out->subcat_id = -1;   /* -1  = not provided, preserve existing */
        out->has_priority = 0; /*  0  = not provided, preserve existing */

        /* walk the remaining argv entries looking for --text, --subcat, --priority flags;
           each flag consumes the next argument as its value (i advances by 2) */
        for (int i = 3; i < argc; ++i) {
            if (strcmp(argv[i], "--text") == 0) {
                if (i + 1 >= argc) return -1;  /* flag with no value */
                out->text = argv[++i];
            } else if (strcmp(argv[i], "--subcat") == 0) {
                if (i + 1 >= argc) return -1;  /* flag with no value */
                if (parse_int(argv[++i], &out->subcat_id) != 0) return -1;
            } else if (strcmp(argv[i], "--priority") == 0) {
                if (i + 1 >= argc) return -1;  /* flag with no value */
                /* parse the string value into the enum — returns -1 if unrecognized */
                if (plan_priority_from_string(argv[++i], &out->priority) != 0) return -1;
                out->has_priority = 1;  /* mark that user explicitly provided a priority */
            } else if (strcmp(argv[i], "--status") == 0) {
                if (i + 1 >= argc) return -1;
                if (plan_status_from_string(argv[++i], &out->status) != 0) return -1;
                out->has_status = 1;
            } else {
                return -1;  /* unknown flag */
            }
        }

        /* at least one flag must be provided — otherwise nothing to update */
        if (out->text == NULL && out->subcat_id == -1 && !out->has_priority && !out->has_status) return -1;
        return 0;
    }

    if (strcmp(argv[1], "delete") == 0) {
        if (argc != 3) return -1;
        if (parse_int(argv[2], &out->id) != 0) return -1;
        out->type = CMD_DELETE;
        return 0;
    }

    if (strcmp(argv[1], "show") == 0) {
        if (argc != 3) return -1;
        if (parse_int(argv[2], &out->id) != 0) return -1;
        out->type = CMD_SHOW;
        return 0;
    }

    if (strcmp(argv[1], "cat") == 0) {
        if (argc == 3 && strcmp(argv[2], "list") == 0) {
            out->type = CMD_CAT_LIST;
            return 0;
        }
        if (argc == 4 && strcmp(argv[2], "add") == 0) {
            out->type = CMD_CAT_ADD;
            out->text = argv[3];
            return 0;
        }
        return -1;
    }

    if (strcmp(argv[1], "subcat") == 0) {
        if (argc == 3 && strcmp(argv[2], "list") == 0) {
            out->type = CMD_SUBCAT_LIST;
            return 0;
        }
        if (argc == 5 && strcmp(argv[2], "add") == 0) {
            if (parse_int(argv[3], &out->id) != 0) return -1;
            out->type = CMD_SUBCAT_ADD;
            out->text = argv[4];
            return 0;
        }
        return -1;
    }

    return -1;
}

void cli_print_usage(const char *progname) {
    fprintf(stderr,
        "Usage:\n"
        "  %s list [--status \"status\"] [--priority \"priority\"] --sort-priority [--all] [--subcat \"subcat\"] \n"
        "  %s add \"text\" [subcat_id]\n"
        "  %s update <id> [--text \"text\"] [--priority \"priority\"] [--subcat <id>] [--status \"status\"]\n"
        "  %s delete <id>\n"
        "  %s show <id>\n"
        "  %s cat list\n"
        "  %s cat add \"name\"\n"
        "  %s subcat list\n"
        "  %s subcat add <cat_id> \"name\"\n",
        progname, progname, progname, progname, progname, progname, progname,
        progname, progname);
}