#include "flat_native.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

char *flat_native_trim(char *s) {
    while (*s && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return s;
    }
    {
        char *e = s + strlen(s) - 1;
        while (e > s && isspace((unsigned char)*e)) {
            *e-- = '\0';
        }
    }
    return s;
}

int flat_native_parse_dotted_call(const char *line, char *mod, size_t mod_max, char *func,
                                  size_t func_max, char *inner, size_t inner_max) {
    const char *dot;
    const char *lp;
    const char *rp;
    size_t n;

    lp = strchr(line, '(');
    rp = strrchr(line, ')');
    if (!lp || !rp || rp <= lp) {
        return -1;
    }
    {
        const char *last_dot = NULL;
        const char *p;
        for (p = lp - 1; p >= line; p--) {
            if (*p == '.') {
                last_dot = p;
                break;
            }
        }
        dot = last_dot;
    }
    if (!dot || dot <= line) {
        return -1;
    }
    n = (size_t)(dot - line);
    if (n == 0 || n >= mod_max) {
        return -1;
    }
    memcpy(mod, line, n);
    mod[n] = '\0';
    n = (size_t)(lp - dot - 1);
    if (n == 0 || n >= func_max) {
        return -1;
    }
    memcpy(func, dot + 1, n);
    func[n] = '\0';
    n = (size_t)(rp - lp - 1);
    if (n + 1 >= inner_max) {
        return -1;
    }
    memcpy(inner, lp + 1, n);
    inner[n] = '\0';
    return 0;
}

int flat_native_find_var(const char *name, int var_count, const char (*names)[64]) {
    int i;
    for (i = 0; i < var_count; i++) {
        if (strcmp(names[i], name) == 0) {
            return i;
        }
    }
    return -1;
}
