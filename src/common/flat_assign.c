#include "flat_value.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static char *trim_mut(char *s) {
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

static const char *find_arrow_outside_quotes(const char *s) {
    int in_s = 0;
    int in_d = 0;

    if (!s) {
        return NULL;
    }
    for (; *s; s++) {
        if (*s == '\'' && !in_d) {
            in_s = !in_s;
        } else if (*s == '"' && !in_s) {
            in_d = !in_d;
        } else if (!in_s && !in_d && s[0] == '-' && s[1] == '>') {
            return s;
        }
    }
    return NULL;
}

static char *find_single_assign_eq(char *s) {
    char *p = s;

    if (!s) {
        return NULL;
    }
    while ((p = strchr(p, '=')) != NULL) {
        if (p[1] != '=') {
            return p;
        }
        p += 2;
    }
    return NULL;
}

int flat_assign_split_line(char *line_buf, char **bind_out, char **value_out,
                           FlatAssignSplitForm *form_out) {
    char *eq;
    const char *arrow;
    char *arrow_mut;

    if (!line_buf || !bind_out || !value_out || !form_out) {
        return -1;
    }
    eq = find_single_assign_eq(line_buf);
    arrow = find_arrow_outside_quotes(line_buf);
    if (eq && arrow) {
        return -1;
    }
    if (eq) {
        *eq = '\0';
        *bind_out = trim_mut(line_buf);
        *value_out = trim_mut(eq + 1);
        *form_out = FLAT_ASSIGN_SPLIT_EQ;
        return (*bind_out)[0] != '\0' && (*value_out)[0] != '\0' ? 0 : -1;
    }
    if (!arrow) {
        return -1;
    }
    arrow_mut = (char *)arrow;
    *arrow_mut = '\0';
    *value_out = trim_mut(line_buf);
    *bind_out = trim_mut(arrow_mut + 2);
    *form_out = FLAT_ASSIGN_SPLIT_ARROW;
    return (*bind_out)[0] != '\0' && (*value_out)[0] != '\0' ? 0 : -1;
}

/* First `.` or `[` that starts a value path (not `<schema>[N]` list-slot type suffix). */
static const char *lhs_path_start(const char *lhs) {
    const char *colon;
    const char *p;

    if (!lhs) {
        return NULL;
    }
    colon = strchr(lhs, ':');
    if (!colon) {
        return strpbrk(lhs, ".[");
    }
    p = colon + 1;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p == '<') {
        const char *end = strchr(p, '>');
        if (!end) {
            return NULL;
        }
        p = end + 1;
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == '[') {
            const char *rb = strchr(p, ']');
            if (!rb) {
                return NULL;
            }
            p = rb + 1;
            while (*p && isspace((unsigned char)*p)) {
                p++;
            }
        }
        return strpbrk(p, ".[");
    }
    if (*p == '/') {
        while (*p && !isspace((unsigned char)*p)) {
            p++;
        }
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        return strpbrk(p, ".[");
    }
    if (*p == '\0') {
        return NULL;
    }
    while (*p && !isspace((unsigned char)*p) && *p != '.' && *p != '[') {
        p++;
    }
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    return strpbrk(p, ".[");
}

int flat_assign_lhs_has_path(const char *lhs) {
    return lhs_path_start(lhs) != NULL;
}

int flat_assign_path_build_expr(const char *lhs, char *path_expr, size_t path_sz, char *root_bind,
                                size_t root_bind_sz) {
    const char *colon;
    const char *dot;
    const char *suffix;
    const char *name_end;
    size_t name_len;
    size_t bind_len;

    if (!lhs || !path_expr || path_sz == 0) {
        return -1;
    }
    suffix = lhs_path_start(lhs);
    if (!suffix) {
        return -1;
    }
    colon = strchr(lhs, ':');
    dot = strchr(lhs, '.');
    name_end = suffix;
    if (colon && dot && colon < dot) {
        name_end = colon;
    } else if (colon && colon < suffix) {
        name_end = colon;
    }
    name_len = (size_t)(name_end - lhs);
    if (name_len == 0 || name_len >= path_sz) {
        return -1;
    }
    snprintf(path_expr, path_sz, "%.*s%s", (int)name_len, lhs, suffix);
    if (root_bind && root_bind_sz > 0) {
        if (colon && dot && colon < dot) {
            bind_len = (size_t)(dot - lhs);
        } else {
            bind_len = name_len;
        }
        if (bind_len >= root_bind_sz) {
            return -1;
        }
        memcpy(root_bind, lhs, bind_len);
        root_bind[bind_len] = '\0';
    }
    return 0;
}

int flat_assign_strip_const_prefix(char *line_buf, int *is_const_out) {
    char *p;
    size_t rest_len;

    if (!line_buf) {
        return -1;
    }
    if (is_const_out) {
        *is_const_out = 0;
    }
    p = trim_mut(line_buf);
    if (strncmp(p, "const", 5) != 0) {
        return 0;
    }
    if (p[5] != '\0' && !isspace((unsigned char)p[5])) {
        return 0;
    }
    p += 5;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    rest_len = strlen(p);
    memmove(line_buf, p, rest_len + 1);
    if (is_const_out) {
        *is_const_out = 1;
    }
    return 0;
}

int flat_assign_lhs_has_type_in_bind(const char *lhs) {
    const char *colon;
    const char *p;

    if (!lhs) {
        return 0;
    }
    colon = strchr(lhs, ':');
    if (!colon) {
        return 0;
    }
    p = colon + 1;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    return *p != '\0';
}
