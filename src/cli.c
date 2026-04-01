#include <stdio.h>
#include <string.h>

#include "cli.h"
#include "util.h"

int cli_parse(int argc, char **argv, Command *out) {
    if (argc < 2 || !out) {
        return -1;
    }

    memset(out, 0, sizeof(*out));

    if (strcmp(argv[1], "list") == 0) {
        if (argc != 2) return -1;
        out->type = CMD_LIST;
        return 0;
    }

    if (strcmp(argv[1], "add") == 0) {
        if (argc != 3) return -1;
        out->type = CMD_ADD;
        out->text = argv[2];
        return 0;
    }

    if (strcmp(argv[1], "update") == 0) {
        if (argc != 4) return -1;
        if (parse_int(argv[2], &out->id) != 0) return -1;
        out->type = CMD_UPDATE;
        out->text = argv[3];
        return 0;
    }

    if (strcmp(argv[1], "delete") == 0) {
        if (argc != 3) return -1;
        if (parse_int(argv[2], &out->id) != 0) return -1;
        out->type = CMD_DELETE;
        return 0;
    }

    if (strcmp(argv[1], "done") == 0) {
        if (argc != 3) return -1;
        if (parse_int(argv[2], &out->id) != 0) return -1;
        out->type = CMD_DONE;
        return 0;
    }

    if (strcmp(argv[1], "show") == 0) {
        if (argc != 3) return -1;
        if (parse_int(argv[2], &out->id) != 0) return -1;
        out->type = CMD_SHOW;
        return 0;
    }

    return -1;
}

void cli_print_usage(const char *progname) {
    fprintf(stderr,
        "Usage:\n"
        "  %s list\n"
        "  %s add \"text\"\n"
        "  %s update <id> \"text\"\n"
        "  %s delete <id>\n"
        "  %s done <id>\n"
        "  %s show <id>\n",
        progname, progname, progname, progname, progname, progname);
}