#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "util.h"

char *xstrdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *p = malloc(len);
    if (!p) return NULL;
    memcpy(p, s, len);
    return p;
}

int parse_int(const char *s, int *out) {
    char *end = NULL;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0 || !end || *end != '\0') return -1;
    if (v < -2147483648L || v > 2147483647L) return -1;
    *out = (int)v;
    return 0;
}

void rstrip_newline(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[--len] = '\0';
    }
}

int current_timestamp_utc(char out[21]) {
    time_t now = time(NULL);
    if (now == (time_t)-1) return -1;

    struct tm tm_utc;
#if defined(_POSIX_VERSION)
    if (gmtime_r(&now, &tm_utc) == NULL) return -1;
#else
    struct tm *tmp = gmtime(&now);
    if (!tmp) return -1;
    tm_utc = *tmp;
#endif

    if (strftime(out, 21, "%Y-%m-%dT%H:%M:%SZ", &tm_utc) != 20) {
        return -1;
    }

    return 0;
}