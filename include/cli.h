#ifndef CLI_H
#define CLI_H

#include "plan.h"

typedef enum {
    CMD_INVALID = 0,
    CMD_LIST,
    CMD_ADD,
    CMD_UPDATE,
    CMD_DELETE,
    CMD_DONE,
    CMD_SHOW,
    CMD_CAT_ADD,
    CMD_CAT_LIST,
    CMD_SUBCAT_ADD,
    CMD_SUBCAT_LIST
} CommandType;

typedef struct {
    CommandType type;
    int id;
    int subcat_id;        /* -1 = not assigned */
    int has_priority;     /* 0 = not provided, 1 = user explicitly set it */
    int has_status;       /* 0 = not provided, 1 = user explicitly set it */
    PlanPriority priority;
    PlanStatus status;
    const char *text;
} Command;

int cli_parse(int argc, char **argv, Command *out);
void cli_print_usage(const char *progname);

#endif