#include "flat_native_mod.h"
#include "flat_native_util.h"

#include <stdio.h>
#include <string.h>

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
    flat_native_trim(buf);
    if (buf[0] == '\0') {
        *st = FLAT_IO_PANIC_CANCEL;
        return 0;
    }
    *st = FLAT_IO_OK;
    return 0;
}

static int read_line_allow_empty(char *buf, size_t buf_size, FlatIoStatus *st) {
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
    flat_native_trim(buf);
    *st = FLAT_IO_OK;
    return 0;
}

static int read_one_char(FlatValue *out, FlatIoStatus *st) {
    int c = fgetc(stdin);
    char ch[2];

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

static int parse_in_collection_line(const char *linebuf, int mode, FlatValue *out,
                                    FlatStringPool *pool, FlatCollectionPool *coll,
                                    FlatIoStatus *st) {
    int rc;

    *st = FLAT_IO_OK;
    if (mode == 0) {
        rc = flat_json_parse_list(linebuf, out, pool, coll);
    } else if (mode == 1) {
        rc = flat_json_parse_object(linebuf, out, pool, coll);
    } else {
        rc = flat_json_parse_bools_io(linebuf, out, pool, coll);
        if (rc == -2) {
            *st = FLAT_IO_PANIC_CANCEL;
            return 0;
        }
    }
    if (rc != 0) {
        *st = FLAT_IO_PANIC_PARSE;
        return 0;
    }
    return 0;
}

static int eval_prompt(FlatNativeCtx *ctx, char *prompt, size_t prompt_size) {
    FlatValue v;
    char inner_buf[4096];

    if (!ctx->inner || ctx->inner[0] == '\0') {
        prompt[0] = '\0';
        return 0;
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (flat_value_parse_assign_rhs(flat_native_trim(inner_buf), &v, ctx->pool, ctx->coll,
                                    ctx->vars, ctx->var_count, ctx->var_names) != 0) {
        return -1;
    }
    if (v.tag == VAL_STR_INLINE) {
        size_t n = v.u.ins.len;
        if (n >= prompt_size) {
            n = prompt_size - 1;
        }
        memcpy(prompt, v.u.ins.bytes, n);
        prompt[n] = '\0';
        return 0;
    }
    if (v.tag == VAL_STR_REF) {
        size_t len = 0;
        const uint8_t *p = flat_pool_read(ctx->pool, v.u.ref_id, &len);
        if (!p) {
            return -1;
        }
        if (len >= prompt_size) {
            len = prompt_size - 1;
        }
        memcpy(prompt, p, len);
        prompt[len] = '\0';
        return 0;
    }
    return -1;
}

int flat_native_io_outln(FlatNativeCtx *ctx) {
    FlatValue v;
    char inner_buf[4096];

    if (!ctx->io_status) {
        return -1;
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (flat_value_parse_assign_rhs(flat_native_trim(inner_buf), &v, ctx->pool, ctx->coll,
                                    ctx->vars, ctx->var_count, ctx->var_names) != 0) {
        return -1;
    }
    flat_value_fprint_io_ln(stdout, &v, ctx->pool, ctx->coll);
    fflush(stdout);
    *ctx->io_status = FLAT_IO_OK;
    return 0;
}

int flat_native_io_out(FlatNativeCtx *ctx) {
    FlatValue v;
    char inner_buf[4096];

    if (!ctx->io_status) {
        return -1;
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (flat_value_parse_assign_rhs(flat_native_trim(inner_buf), &v, ctx->pool, ctx->coll,
                                    ctx->vars, ctx->var_count, ctx->var_names) != 0) {
        return -1;
    }
    flat_value_fprint_io(stdout, &v, ctx->pool, ctx->coll);
    fflush(stdout);
    *ctx->io_status = FLAT_IO_OK;
    return 0;
}

int flat_native_io_outvar(FlatNativeCtx *ctx) {
    char varname[FLAT_VAR_NAME_MAX + 1];
    int vi;

    if (!ctx->io_status) {
        return -1;
    }
    snprintf(varname, sizeof varname, "%s", ctx->inner);
    flat_native_trim(varname);
    vi = flat_native_find_var(varname, ctx->var_count, ctx->var_names);
    if (vi < 0) {
        return -1;
    }
    flat_value_fprint_io_ln(stdout, &ctx->vars[vi], ctx->pool, ctx->coll);
    fflush(stdout);
    *ctx->io_status = FLAT_IO_OK;
    return 0;
}

int flat_native_io_in(FlatNativeCtx *ctx) {
    char prompt[256];

    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    if (eval_prompt(ctx, prompt, sizeof prompt) != 0) {
        return -1;
    }
    emit_prompt(prompt);
    read_one_char(ctx->out, ctx->io_status);
    return 0;
}

static int io_read_stdin_line(FlatNativeCtx *ctx, char *linebuf, size_t linebuf_size,
                              int allow_empty) {
    char prompt[256];

    if (eval_prompt(ctx, prompt, sizeof prompt) != 0) {
        return -1;
    }
    emit_prompt(prompt);
    if (allow_empty) {
        read_line_allow_empty(linebuf, linebuf_size, ctx->io_status);
    } else {
        read_line(linebuf, linebuf_size, ctx->io_status);
    }
    return 0;
}

int flat_native_io_inln(FlatNativeCtx *ctx) {
    char linebuf[4096];

    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    if (io_read_stdin_line(ctx, linebuf, sizeof linebuf, 0) != 0) {
        return -1;
    }
    if (*ctx->io_status != FLAT_IO_OK) {
        return 0;
    }
    return flat_value_set_str_inline(ctx->out, linebuf, strlen(linebuf));
}

int flat_native_io_in_num(FlatNativeCtx *ctx) {
    char linebuf[4096];

    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    if (io_read_stdin_line(ctx, linebuf, sizeof linebuf, 0) != 0) {
        return -1;
    }
    if (*ctx->io_status != FLAT_IO_OK) {
        return 0;
    }
    return parse_in_line_num(linebuf, ctx->out, ctx->io_status);
}

int flat_native_io_in_bool(FlatNativeCtx *ctx) {
    char linebuf[4096];

    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    if (io_read_stdin_line(ctx, linebuf, sizeof linebuf, 0) != 0) {
        return -1;
    }
    if (*ctx->io_status != FLAT_IO_OK) {
        return 0;
    }
    return parse_in_line_bool(linebuf, 1, ctx->out, ctx->io_status);
}

int flat_native_io_in_list(FlatNativeCtx *ctx) {
    char linebuf[4096];

    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    if (io_read_stdin_line(ctx, linebuf, sizeof linebuf, 1) != 0) {
        return -1;
    }
    if (*ctx->io_status != FLAT_IO_OK) {
        return 0;
    }
    return parse_in_collection_line(linebuf, 0, ctx->out, ctx->pool, ctx->coll, ctx->io_status);
}

int flat_native_io_in_object(FlatNativeCtx *ctx) {
    char linebuf[4096];

    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    if (io_read_stdin_line(ctx, linebuf, sizeof linebuf, 1) != 0) {
        return -1;
    }
    if (*ctx->io_status != FLAT_IO_OK) {
        return 0;
    }
    return parse_in_collection_line(linebuf, 1, ctx->out, ctx->pool, ctx->coll, ctx->io_status);
}

int flat_native_io_in_bools(FlatNativeCtx *ctx) {
    char linebuf[4096];

    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    if (io_read_stdin_line(ctx, linebuf, sizeof linebuf, 1) != 0) {
        return -1;
    }
    if (*ctx->io_status != FLAT_IO_OK) {
        return 0;
    }
    return parse_in_collection_line(linebuf, 2, ctx->out, ctx->pool, ctx->coll, ctx->io_status);
}
