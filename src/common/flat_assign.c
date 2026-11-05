#include "flat_value.h"

#include <ctype.h>
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
