#ifndef STRBUF_H
#define STRBUF_H

/* Internal dynamic string buffer used by api.c and timesheet.c to build
 * JSON responses without an external library. Not part of the public API. */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct { char *buf; size_t len; size_t cap; } Strbuf;

static inline int sb_init(Strbuf *sb) {
    sb->buf = malloc(256);
    if (!sb->buf) return -1;
    sb->buf[0] = '\0'; sb->len = 0; sb->cap = 256;
    return 0;
}

static inline int sb_append(Strbuf *sb, const char *s) {
    size_t n = strlen(s);
    while (sb->len + n + 1 > sb->cap) {
        size_t nc  = sb->cap * 2;
        char  *tmp = realloc(sb->buf, nc);
        if (!tmp) return -1;
        sb->buf = tmp; sb->cap = nc;
    }
    memcpy(sb->buf + sb->len, s, n + 1);
    sb->len += n;
    return 0;
}

static inline int sb_append_int(Strbuf *sb, int n) {
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%d", n);
    return sb_append(sb, tmp);
}

static inline int sb_append_json_str(Strbuf *sb, const char *s) {
    if (sb_append(sb, "\"") != 0) return -1;
    for (; *s; ++s) {
        char tmp[3] = {'\0', '\0', '\0'};
        if (*s == '"' || *s == '\\') { tmp[0] = '\\'; tmp[1] = *s; }
        else { tmp[0] = *s; }
        if (sb_append(sb, tmp) != 0) return -1;
    }
    return sb_append(sb, "\"");
}

#endif /* STRBUF_H */
