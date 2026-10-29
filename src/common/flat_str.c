#include "flat_str.h"

#include <ctype.h>
#include <string.h>

char *flat_str_trim(char *s) {
    char *p = s;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p == '\0') {
        *s = '\0';
        return s;
    }
    if (p != s) {
        memmove(s, p, strlen(p) + 1);
    }
    {
        char *e = s + strlen(s) - 1;
        while (e > s && isspace((unsigned char)*e)) {
            *e-- = '\0';
        }
    }
    return s;
}

char *flat_str_next(char **cursor, char delim) {
    char *s;
    char *start;

    if (!cursor || !*cursor) {
        return NULL;
    }
    s = *cursor;
    if (*s == '\0') {
        return NULL;
    }
    start = s;
    while (*s && *s != delim) {
        s++;
    }
    if (*s == delim) {
        *s++ = '\0';
    }
    *cursor = s;
    return start;
}
