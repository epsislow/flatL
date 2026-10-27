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

static int parse_dotted_call(const char *line, char *mod, size_t mod_max, char *func, size_t func_max,
                             char *inner, size_t inner_max) {
    const char *dot;
    const char *lp;
    const char *rp;
    size_t n;

    lp = strchr(line, '(');
    rp = strrchr(line, ')');
    if (!lp || !rp || rp <= lp) {
        return -1;
    }
    dot = strchr(line, '.');
    if (!dot || dot > lp) {
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

static int eval_prompt(const char *inner, FlatStringPool *pool, FlatCollectionPool *coll,
                       FlatValue *vars, int var_count, const char (*names)[64], char *out,
                       size_t out_size) {
    FlatValue v;
    char names_copy[FLAT_MAX_VARS][64];
    FlatValue vals[FLAT_MAX_VARS];
    int i;

    char inner_buf[4096];

    if (!inner || inner[0] == '\0') {
        out[0] = '\0';
        return 0;
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", inner);
    for (i = 0; i < var_count; i++) {
        vals[i] = vars[i];
        snprintf(names_copy[i], 64, "%s", names[i]);
    }
    if (flat_value_parse_assign_rhs(trim(inner_buf), &v, pool, coll, vals, var_count, names_copy) !=
        0) {
        return -1;
    }
    if (v.tag == VAL_STR_INLINE) {
        size_t n = v.u.ins.len;
        if (n >= out_size) {
            n = out_size - 1;
        }
        memcpy(out, v.u.ins.bytes, n);
        out[n] = '\0';
        return 0;
    }
    if (v.tag == VAL_STR_REF) {
        size_t len = 0;
        const uint8_t *p = flat_pool_read(pool, v.u.ref_id, &len);
        if (!p) {
            return -1;
        }
        if (len >= out_size) {
            len = out_size - 1;
        }
        memcpy(out, p, len);
        out[len] = '\0';
        return 0;
    }
    return -1;
}

static void emit_prompt(const char *prompt) {
    if (prompt && prompt[0] != '\0') {
        fputs(prompt, stdout);
        fflush(stdout);
    }
}

static int read_line(char *buf, size_t buf_size, FlatIoStatus *st) {
    if (!fgets(buf, (int)buf_size, stdin)) {
        *st = FLAT_IO_PANIC_EOF;
        return 0;
    }
    {
        size_t n = strlen(buf);
        if (n > 0 && buf[n - 1] == '\n') {
            buf[n - 1] = '\0';
        }
    }
    trim(buf);
    if (buf[0] == '\0') {
        *st = FLAT_IO_PANIC_CANCEL;
        return 0;
    }
    *st = FLAT_IO_OK;
    return 0;
}

static int read_one_char(FlatValue *out, FlatStringPool *pool, FlatIoStatus *st) {
    int c = fgetc(stdin);
    char ch[2];

    (void)pool;
    if (c == EOF) {
        *st = FLAT_IO_PANIC_EOF;
        return 0;
    }
    if (c == 27 || c == '\n') {
        *st = FLAT_IO_PANIC_CANCEL;
        return 0;
    }
    ch[0] = (char)c;
    ch[1] = '\0';
    *st = FLAT_IO_OK;
    return flat_value_set_str_inline(out, ch, 1);
}

static int parse_in_line_bool(const char *line, int allow_cancel, FlatValue *out, FlatIoStatus *st) {
    int b;

    if (allow_cancel && flat_parse_bool_cancel_token(line)) {
        *st = FLAT_IO_PANIC_CANCEL;
        return 0;
    }
    if (flat_parse_bool_token(line, &b) != 0) {
        *st = FLAT_IO_PANIC_PARSE;
        return 0;
    }
    *st = FLAT_IO_OK;
    return flat_value_set_bool(out, b);
}

static int parse_in_line_num(const char *line, FlatValue *out, FlatIoStatus *st) {
    double n;

    if (flat_parse_number_token(line, &n) != 0) {
        *st = FLAT_IO_PANIC_PARSE;
        return 0;
    }
    *st = FLAT_IO_OK;
    return flat_value_set_number(out, n);
}

static int run_str(const char *func, const char *inner, FlatStringPool *pool, FlatCollectionPool *coll,
                   FlatValue *vars, int var_count, const char (*names)[64], FlatValue *out,
                   FlatIoStatus *st) {
    FlatValue src;
    char token[512];
    char *p;
    int b;
    double n;

    char inner_buf[4096];

    *st = FLAT_IO_OK;
    snprintf(inner_buf, sizeof inner_buf, "%s", inner);
    if (flat_value_parse_assign_rhs(trim(inner_buf), &src, pool, coll, vars, var_count, names) !=
        0) {
        *st = FLAT_IO_ERR;
        return -1;
    }
    if (src.tag != VAL_STR_INLINE && src.tag != VAL_STR_REF) {
        *st = FLAT_IO_ERR;
        return -1;
    }
    if (src.tag == VAL_STR_INLINE) {
        snprintf(token, sizeof token, "%s", src.u.ins.bytes);
    } else {
        size_t len = 0;
        const uint8_t *bp = flat_pool_read(pool, src.u.ref_id, &len);
        if (!bp || len >= sizeof token) {
            *st = FLAT_IO_ERR;
            return -1;
        }
        memcpy(token, bp, len);
        token[len] = '\0';
    }
    p = trim(token);
    if (strcmp(func, "to_num") == 0) {
        if (flat_parse_number_token(p, &n) != 0) {
            *st = FLAT_IO_ERR;
            return -1;
        }
        return flat_value_set_number(out, n);
    }
    if (strcmp(func, "to_bool") == 0) {
        if (flat_parse_bool_token(p, &b) != 0) {
            *st = FLAT_IO_ERR;
            return -1;
        }
        return flat_value_set_bool(out, b);
    }
    *st = FLAT_IO_ERR;
    return -1;
}

int flat_io_exec_line(const char *line, FlatStringPool *pool, FlatCollectionPool *coll,
                      FlatValue *vars, int var_count, const char (*var_names)[64],
                      FlatValue *out_result, FlatIoStatus *out_status) {
    char mod[16];
    char func[32];
    char inner[4096];
    char prompt[256];
    char linebuf[4096];
    char names[FLAT_MAX_VARS][64];
    FlatValue vals[FLAT_MAX_VARS];
    int i;
    int vi;
    if (out_status) {
        *out_status = FLAT_IO_ERR;
    }
    if (!line || !pool) {
        return -1;
    }
    if (parse_dotted_call(line, mod, sizeof mod, func, sizeof func, inner, sizeof inner) != 0) {
        return -1;
    }
    for (i = 0; i < var_count; i++) {
        vals[i] = vars[i];
        snprintf(names[i], 64, "%s", var_names[i]);
    }

    if (strcmp(mod, "str") == 0) {
        if (!out_result || run_str(func, inner, pool, coll, vals, var_count, names, out_result,
                                   out_status) != 0) {
            return -1;
        }
        return 0;
    }
    if (strcmp(mod, "io") != 0) {
        return -1;
    }

    if (strcmp(func, "outln") == 0) {
        FlatValue v;
        if (flat_value_parse_assign_rhs(trim(inner), &v, pool, coll, vals, var_count, names) != 0) {
            return -1;
        }
        flat_value_fprint_io_ln(stdout, &v, pool, coll);
        fflush(stdout);
        if (out_status) {
            *out_status = FLAT_IO_OK;
        }
        return 0;
    }
    if (strcmp(func, "out") == 0) {
        FlatValue v;
        if (flat_value_parse_assign_rhs(trim(inner), &v, pool, coll, vals, var_count, names) != 0) {
            return -1;
        }
        flat_value_fprint_io(stdout, &v, pool, coll);
        fflush(stdout);
        if (out_status) {
            *out_status = FLAT_IO_OK;
        }
        return 0;
    }
    if (strcmp(func, "outvar") == 0) {
        char varname[FLAT_VAR_NAME_MAX + 1];
        snprintf(varname, sizeof varname, "%s", trim(inner));
        vi = find_var_index(varname, var_count, var_names);
        if (vi < 0) {
            return -1;
        }
        flat_value_fprint_io_ln(stdout, &vars[vi], pool, coll);
        fflush(stdout);
        if (out_status) {
            *out_status = FLAT_IO_OK;
        }
        return 0;
    }

    if (!out_result) {
        return -1;
    }
    if (eval_prompt(inner, pool, coll, vals, var_count, names, prompt, sizeof prompt) != 0) {
        return -1;
    }

    if (strcmp(func, "in") == 0) {
        emit_prompt(prompt);
        read_one_char(out_result, pool, out_status);
        return (*out_status == FLAT_IO_OK) ? 0 : 0;
    }
    if (strcmp(func, "inln") == 0 || strcmp(func, "in_num") == 0 || strcmp(func, "in_bool") == 0) {
        emit_prompt(prompt);
        read_line(linebuf, sizeof linebuf, out_status);
        if (*out_status != FLAT_IO_OK) {
            return 0;
        }
        if (strcmp(func, "inln") == 0) {
            return flat_value_set_str_inline(out_result, linebuf, strlen(linebuf));
        }
        if (strcmp(func, "in_bool") == 0) {
            return parse_in_line_bool(linebuf, 1, out_result, out_status);
        }
        if (strcmp(func, "in_num") == 0) {
            return parse_in_line_num(linebuf, out_result, out_status);
        }
    }

    return -1;
}
