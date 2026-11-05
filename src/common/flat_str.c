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

int flat_str_split_three(char *inner, char **first, char **second, char **third) {
    int depth = 0;
    char quote = 0;
    char *p;
    char *c1 = NULL;
    char *c2 = NULL;

    if (!inner || !first || !second || !third) {
        return -1;
    }
    for (p = inner; *p; p++) {
        if (!quote && (*p == '\'' || *p == '"')) {
            quote = *p;
        } else if (quote && *p == quote) {
            quote = 0;
        } else if (!quote && (*p == '[' || *p == '(')) {
            depth++;
        } else if (!quote && (*p == ']' || *p == ')')) {
            if (depth > 0) {
                depth--;
            }
        } else if (!quote && depth == 0 && *p == ',') {
            if (!c1) {
                *p = '\0';
                c1 = p;
            } else if (!c2) {
                *p = '\0';
                c2 = p;
                break;
            }
        }
    }
    if (!c1 || !c2) {
        return -1;
    }
    *first = flat_str_trim(inner);
    *second = flat_str_trim(c1 + 1);
    *third = flat_str_trim(c2 + 1);
    return 0;
}
