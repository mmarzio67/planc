#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>

char *xstrdup(const char *s);
int parse_int(const char *s, int *out);
void rstrip_newline(char *s);
int current_timestamp_utc(char out[21]);

#endif