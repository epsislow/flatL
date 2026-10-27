#include "flat_value.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static char *trim_inplace(char *s) {
    char *e;
    while (*s && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return s;
    }
    e = s + strlen(s) - 1;
    while (e > s && isspace((unsigned char)*e)) {
        *e-- = '\0';
    }
    return s;
}

static int ci_equal(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == *b;
}

int flat_parse_bool_token(const char *token, int *out_bool) {
    char buf[64];
    char *t;

    if (!token || !out_bool) {
        return -1;
    }
    snprintf(buf, sizeof buf, "%s", token);
    t = trim_inplace(buf);
    if (*t == '\0') {
        return -1;
    }
    if (ci_equal(t, "true") || ci_equal(t, "t") || ci_equal(t, "yes") || ci_equal(t, "y") ||
        ci_equal(t, "on") || ci_equal(t, "1")) {
        *out_bool = 1;
        return 0;
    }
    if (ci_equal(t, "false") || ci_equal(t, "f") || ci_equal(t, "no") || ci_equal(t, "n") ||
        ci_equal(t, "off") || ci_equal(t, "0")) {
        *out_bool = 0;
        return 0;
    }
    return -1;
}

int flat_parse_bool_cancel_token(const char *token) {
    char buf[64];
    char *t;

    if (!token) {
        return 0;
    }
    snprintf(buf, sizeof buf, "%s", token);
    t = trim_inplace(buf);
    if (ci_equal(t, "cancel") || ci_equal(t, "c") || ci_equal(t, "quit") || ci_equal(t, "q")) {
        return 1;
    }
    return 0;
}

int flat_parse_number_token(const char *token, double *out_num) {
    char *end = NULL;
    double v;
    char buf[128];
    char *t;

    if (!token || !out_num) {
        return -1;
    }
    snprintf(buf, sizeof buf, "%s", token);
    t = trim_inplace(buf);
    if (*t == '\0') {
        return -1;
    }
    v = strtod(t, &end);
    if (end == t || (end && *end != '\0')) {
        return -1;
    }
    if (v != v || v > 1e308 || v < -1e308) {
        return -1;
    }
    *out_num = v;
    return 0;
}
