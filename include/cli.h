#ifndef CLI_H
#define CLI_H

typedef enum {
    CMD_INVALID = 0,
    CMD_LIST,
    CMD_ADD,
    CMD_UPDATE,
    CMD_DELETE,
    CMD_DONE,
    CMD_SHOW
} CommandType;

typedef struct {
    CommandType type;
    int id;
    const char *text;
} Command;

int cli_parse(int argc, char **argv, Command *out);
void cli_print_usage(const char *progname);

#endif