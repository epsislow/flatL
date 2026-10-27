#include "flat_value.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return s;
    }
    char *e = s + strlen(s) - 1;
    while (e > s && isspace((unsigned char)*e)) {
        *e-- = '\0';
    }
    return s;
}

static int find_var_index(const char *name, int var_count, const char (*names)[64]) {
    int i;
    for (i = 0; i < var_count; i++) {
        if (strcmp(names[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

static int parse_io_call(const char *line, char *func, size_t func_max, char *inner,
                         size_t inner_max) {
    const char *dot;
    const char *lp;
    const char *rp;
    size_t n;

    if (strncmp(line, "io.", 3) != 0) {
        return -1;
    }
    dot = strchr(line, '.');
    lp = strchr(line, '(');
    rp = strrchr(line, ')');
    if (!dot || !lp || !rp || rp <= lp) {
        return -1;
    }
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

int flat_io_exec_line(const char *line, FlatStringPool *pool, FlatCollectionPool *coll,
                      FlatValue *vars, int var_count, const char (*var_names)[64]) {
    char func[32];
    char inner[4096];
    FlatValue v;
    char names[FLAT_MAX_VARS][64];
    FlatValue vals[FLAT_MAX_VARS];
    int i;
    int vi;

    if (!line || !pool) {
        return -1;
    }
    if (parse_io_call(line, func, sizeof func, inner, sizeof inner) != 0) {
        return -1;
    }
    for (i = 0; i < var_count; i++) {
        vals[i] = vars[i];
        snprintf(names[i], 64, "%s", var_names[i]);
    }

    if (strcmp(func, "outln") == 0) {
        if (flat_value_parse_assign_rhs(trim(inner), &v, pool, coll, vals, var_count, names) != 0) {
            return -1;
        }
        flat_value_fprint_ln(stdout, &v, pool, coll);
        fflush(stdout);
        return 0;
    }
    if (strcmp(func, "outvar") == 0) {
        char varname[FLAT_VAR_NAME_MAX + 1];
        snprintf(varname, sizeof varname, "%s", trim(inner));
        vi = find_var_index(varname, var_count, var_names);
        if (vi < 0) {
            return -1;
        }
        flat_value_fprint_ln(stdout, &vars[vi], pool, coll);
        fflush(stdout);
        return 0;
    }
    if (strcmp(func, "inln") == 0 || strcmp(func, "in") == 0) {
        fprintf(stderr, "flvm: io.%s not implemented yet (planned io.inln)\n", func);
        return -1;
    }
    return -1;
}
