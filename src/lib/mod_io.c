#include "flat_io_ctx.h"
#include "flat_native_mod.h"
#include "flat_native_util.h"
#include "flat_slot_type.h"
#include "flat_value.h"
#include "doc_lookup.h"

#include "flat_wasm_io.h"

#include <stdio.h>
#include <string.h>

static void emit_prompt(const char *prompt) {
    if (prompt && prompt[0] != '\0') {
#ifdef FLAT_WASM_BUILD
        flat_wasm_io_write_stdout(prompt, strlen(prompt));
#else
        fputs(prompt, stdout);
        fflush(stdout);
#endif
    }
}

static int read_line_with_prompt(char *buf, size_t buf_size, int allow_empty, const char *prompt,
                                 int input_kind, FlatIoStatus *st) {
#ifdef FLAT_WASM_BUILD
    if (flat_wasm_io_read_line(buf, buf_size, allow_empty, prompt, input_kind, st) != 0) {
        *st = FLAT_IO_PANIC_EOF;
        return 0;
    }
    if (*st != FLAT_IO_OK) {
        return 0;
    }
#else
    (void)prompt;
    (void)input_kind;
    (void)allow_empty;
    if (!fgets(buf, (int)buf_size, stdin)) {
        *st = FLAT_IO_PANIC_EOF;
        return 0;
    }
#endif
    {
        /* Chomp CRLF/LF (binary stdin on Windows pipes leaves \\r before \\n). */
        size_t n = strlen(buf);
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
            buf[--n] = '\0';
        }
    }
    flat_native_trim(buf);
    if (!allow_empty && buf[0] == '\0') {
        *st = FLAT_IO_PANIC_CANCEL;
        return 0;
    }
    *st = FLAT_IO_OK;
    return 0;
}

static int read_one_char_with_prompt(FlatValue *out, FlatIoStatus *st, const char *prompt) {
    char ch[2];
#ifdef FLAT_WASM_BUILD
    char c = 0;

    if (flat_wasm_io_read_char(&c, prompt ? prompt : "", st) != 0) {
        *st = FLAT_IO_PANIC_EOF;
        return 0;
    }
    if (*st != FLAT_IO_OK) {
        return 0;
    }
    if (c == 27 || c == '\n') {
        *st = FLAT_IO_PANIC_CANCEL;
        return 0;
    }
    ch[0] = c;
#else
    int c = fgetc(stdin);

    (void)prompt;
    if (c == EOF) {
        *st = FLAT_IO_PANIC_EOF;
        return 0;
    }
    if (c == 27 || c == '\n') {
        *st = FLAT_IO_PANIC_CANCEL;
        return 0;
    }
    ch[0] = (char)c;
#endif
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

static int eval_io_arg(FlatNativeCtx *ctx, FlatValue *out) {
    char inner_buf[4096];

    if (!ctx->inner) {
        return -1;
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    flat_native_trim(inner_buf);
    if (flat_native_eval_call(ctx, inner_buf, out) == 0) {
        return 0;
    }
    return flat_value_parse_assign_rhs(inner_buf, out, ctx->pool, ctx->coll, ctx->vars,
                                       ctx->var_count, ctx->var_names);
}

static int eval_io_arg_expr(FlatNativeCtx *ctx, const char *expr, FlatValue *out) {
    char inner_buf[4096];

    if (!expr) {
        return -1;
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", expr);
    flat_native_trim(inner_buf);
    if (flat_native_eval_call(ctx, inner_buf, out) == 0) {
        return 0;
    }
    return flat_value_parse_assign_rhs(inner_buf, out, ctx->pool, ctx->coll, ctx->vars,
                                       ctx->var_count, ctx->var_names);
}

static int split_two_args(char *inner, char **left, char **right) {
    int depth = 0;
    char quote = 0;
    char *p;

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
            *p = '\0';
            *left = flat_native_trim(inner);
            *right = flat_native_trim(p + 1);
            return 0;
        }
    }
    return -1;
}

static int io_string_from_value(FlatNativeCtx *ctx, const FlatValue *v, char *buf, size_t buf_sz) {
    if (!v || !buf || buf_sz == 0) {
        return -1;
    }
    if (v->tag == VAL_STR_INLINE) {
        if (v->u.ins.len + 1 > buf_sz) {
            return -1;
        }
        memcpy(buf, v->u.ins.bytes, v->u.ins.len);
        buf[v->u.ins.len] = '\0';
        return 0;
    }
    if (v->tag == VAL_STR_REF && ctx && ctx->pool) {
        size_t len = 0;
        const uint8_t *p = flat_pool_read(ctx->pool, v->u.ref_id, &len);
        if (!p || len + 1 > buf_sz) {
            return -1;
        }
        memcpy(buf, p, len);
        buf[len] = '\0';
        return 0;
    }
    return -1;
}

static int io_prompt_from_value(FlatNativeCtx *ctx, const FlatValue *v, char *prompt,
                                size_t prompt_sz) {
    if (v->tag == VAL_STR_INLINE) {
        size_t n = v->u.ins.len;
        if (n >= prompt_sz) {
            n = prompt_sz - 1;
        }
        memcpy(prompt, v->u.ins.bytes, n);
        prompt[n] = '\0';
        return 0;
    }
    if (v->tag == VAL_STR_REF) {
        size_t len = 0;
        const uint8_t *p = flat_pool_read(ctx->pool, v->u.ref_id, &len);
        if (!p) {
            return -1;
        }
        if (len >= prompt_sz) {
            len = prompt_sz - 1;
        }
        memcpy(prompt, p, len);
        prompt[len] = '\0';
        return 0;
    }
    return -1;
}

static int io_type_spec_from_expr(FlatNativeCtx *ctx, const char *expr, FlatSlotType *out) {
    FlatValue v;
    char spec[96];
    const FlatSchemaCatalog *cat;

    if (!ctx || !expr || !out) {
        return -1;
    }
    if (eval_io_arg_expr(ctx, expr, &v) != 0) {
        return -1;
    }
    if (io_string_from_value(ctx, &v, spec, sizeof spec) != 0) {
        return -1;
    }
    cat = ctx->bc ? &ctx->bc->schemas : NULL;
    return flat_slot_type_parse_spec(spec, out, cat, NULL, 0);
}

static int io_parse_prompt_and_type(FlatNativeCtx *ctx, char *prompt, size_t prompt_sz,
                                    FlatSlotType *expect, int *has_expect) {
    char inner_buf[4096];
    char *left;
    char *right = NULL;
    FlatValue v;

    if (has_expect) {
        *has_expect = 0;
    }
    if (prompt && prompt_sz > 0) {
        prompt[0] = '\0';
    }
    if (!ctx || !ctx->inner || ctx->inner[0] == '\0') {
        return 0;
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
  left = flat_native_trim(inner_buf);
    if (split_two_args(inner_buf, &left, &right) == 0 && right && right[0] != '\0') {
        if (eval_io_arg_expr(ctx, left, &v) != 0 || io_prompt_from_value(ctx, &v, prompt, prompt_sz) != 0) {
            return -1;
        }
        if (io_type_spec_from_expr(ctx, right, expect) != 0) {
            return -1;
        }
        if (has_expect) {
            *has_expect = 1;
        }
        return 0;
    }
    if (eval_io_arg(ctx, &v) != 0) {
        return -1;
    }
    return io_prompt_from_value(ctx, &v, prompt, prompt_sz);
}

static int io_apply_expect_type(FlatNativeCtx *ctx, FlatValue *val, const FlatSlotType *expect,
                                int has_expect) {
    FlatValue staged;
    char chk[64];
    const FlatSchemaCatalog *cat;

    if (!ctx || !val || !has_expect || !expect) {
        return 0;
    }
    cat = ctx->bc ? &ctx->bc->schemas : NULL;
    staged = *val;
    if (flat_slot_type_check_value(expect, val, ctx->pool, ctx->coll, cat, &staged, chk,
                                   sizeof chk) != 0) {
        if (ctx->io_status) {
            *ctx->io_status = FLAT_IO_ERR;
        }
        if (ctx->panic_detail[0] == '\0') {
            if (strcmp(chk, "type.assign.string.len") == 0) {
                snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", "type.io.len");
            } else {
                snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s",
                         chk[0] != '\0' ? chk : "type.io.expect");
            }
        }
        return -1;
    }
    *val = staged;
    return 0;
}

static void io_set_type_io_panic(FlatNativeCtx *ctx, const char *code) {
    if (!ctx) {
        return;
    }
    if (ctx->io_status) {
        *ctx->io_status = FLAT_IO_ERR;
    }
    if (ctx->panic_detail[0] == '\0' && code) {
        snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", code);
    }
}

static int io_read_stdin_line_prompt(FlatNativeCtx *ctx, char *linebuf, size_t linebuf_size,
                                     int allow_empty, int input_kind, const char *prompt) {
    if (ctx->io_ctx) {
        flat_io_ctx_bind_vars(ctx->io_ctx, ctx->vars, ctx->var_count, ctx->vcx);
        emit_prompt(prompt);
        flat_io_ctx_read_line(ctx->io_ctx, ctx, linebuf, linebuf_size, allow_empty, input_kind,
                              ctx->io_status);
        return 0;
    }
    emit_prompt(prompt);
    read_line_with_prompt(linebuf, linebuf_size, allow_empty, prompt, input_kind, ctx->io_status);
    return 0;
}

static int eval_prompt(FlatNativeCtx *ctx, char *prompt, size_t prompt_size) {
    FlatValue v;

    if (!ctx->inner || ctx->inner[0] == '\0') {
        prompt[0] = '\0';
        return 0;
    }
    if (eval_io_arg(ctx, &v) != 0) {
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

#ifdef FLAT_WASM_BUILD
static void docs_line_wasm(void *userdata, const char *line) {
    (void)userdata;
    flat_wasm_io_write_stdout(line, strlen(line));
    flat_wasm_io_write_stdout("\n", 1);
}
#endif

static int parse_docs_query(FlatNativeCtx *ctx, char *query, size_t query_sz) {
    FlatValue v;
    char inner_buf[4096];

    if (!ctx->inner) {
        return -1;
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    flat_native_trim(inner_buf);
    if (inner_buf[0] == '\'' || inner_buf[0] == '"') {
        if (flat_value_parse_assign_rhs(inner_buf, &v, ctx->pool, ctx->coll, ctx->vars,
                                        ctx->var_count, ctx->var_names) != 0) {
            return -1;
        }
        if (v.tag != VAL_STR_INLINE && v.tag != VAL_STR_REF) {
            return -1;
        }
        if (v.tag == VAL_STR_INLINE) {
            size_t n = v.u.ins.len;
            if (n >= query_sz) {
                n = query_sz - 1;
            }
            memcpy(query, v.u.ins.bytes, n);
            query[n] = '\0';
            return 0;
        }
        {
            size_t len = 0;
            const uint8_t *p = flat_pool_read(ctx->pool, v.u.ref_id, &len);
            if (!p || len >= query_sz) {
                return -1;
            }
            memcpy(query, p, len);
            query[len] = '\0';
            return 0;
        }
    }
    snprintf(query, query_sz, "%s", inner_buf);
    return 0;
}

int flat_native_io_docs(FlatNativeCtx *ctx) {
    char query[256];

    if (!ctx->io_status) {
        return -1;
    }
    if (parse_docs_query(ctx, query, sizeof query) != 0) {
        return -1;
    }
#ifdef FLAT_WASM_BUILD
    if (flat_doc_emit_cb(NULL, docs_line_wasm, ctx->doc_decls, ctx->doc_decl_count, ctx->bc,
                         query) != 0) {
        return -1;
    }
#else
    if (flat_doc_emit(stdout, ctx->doc_decls, ctx->doc_decl_count, ctx->bc, query) != 0) {
        return -1;
    }
    fflush(stdout);
#endif
    *ctx->io_status = FLAT_IO_OK;
    return 0;
}

int flat_native_io_outln(FlatNativeCtx *ctx) {
    FlatValue v;

    if (!ctx->io_status) {
        return -1;
    }
    if (eval_io_arg(ctx, &v) != 0) {
        return -1;
    }
    if (ctx->io_ctx) {
        flat_io_ctx_bind_vars(ctx->io_ctx, ctx->vars, ctx->var_count, ctx->vcx);
        return flat_io_ctx_write(ctx->io_ctx, ctx, 1, &v, 1);
    }
#ifdef FLAT_WASM_BUILD
    flat_wasm_io_fprint_io_ln(&v, ctx->pool, ctx->coll);
#else
    flat_value_fprint_io_ln(stdout, &v, ctx->pool, ctx->coll);
    fflush(stdout);
#endif
    *ctx->io_status = FLAT_IO_OK;
    return 0;
}

int flat_native_io_out(FlatNativeCtx *ctx) {
    FlatValue v;

    if (!ctx->io_status) {
        return -1;
    }
    if (eval_io_arg(ctx, &v) != 0) {
        return -1;
    }
    if (ctx->io_ctx) {
        flat_io_ctx_bind_vars(ctx->io_ctx, ctx->vars, ctx->var_count, ctx->vcx);
        return flat_io_ctx_write(ctx->io_ctx, ctx, 1, &v, 0);
    }
#ifdef FLAT_WASM_BUILD
    flat_wasm_io_fprint_io(&v, ctx->pool, ctx->coll);
#else
    flat_value_fprint_io(stdout, &v, ctx->pool, ctx->coll);
    fflush(stdout);
#endif
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
    if (ctx->io_ctx) {
        flat_io_ctx_bind_vars(ctx->io_ctx, ctx->vars, ctx->var_count, ctx->vcx);
        return flat_io_ctx_write(ctx->io_ctx, ctx, 1, &ctx->vars[vi], 1);
    }
#ifdef FLAT_WASM_BUILD
    flat_wasm_io_fprint_io_ln(&ctx->vars[vi], ctx->pool, ctx->coll);
#else
    flat_value_fprint_io_ln(stdout, &ctx->vars[vi], ctx->pool, ctx->coll);
    fflush(stdout);
#endif
    *ctx->io_status = FLAT_IO_OK;
    return 0;
}

int flat_native_io_in(FlatNativeCtx *ctx) {
    char prompt[256];
    char ch[2];

    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    if (eval_prompt(ctx, prompt, sizeof prompt) != 0) {
        return -1;
    }
    if (ctx->io_ctx) {
        flat_io_ctx_bind_vars(ctx->io_ctx, ctx->vars, ctx->var_count, ctx->vcx);
        emit_prompt(prompt);
        flat_io_ctx_read_char(ctx->io_ctx, ctx, ch, ctx->io_status);
        if (*ctx->io_status != FLAT_IO_OK) {
            return 0;
        }
        return flat_value_set_str_inline(ctx->out, ch, 1);
    }
    emit_prompt(prompt);
    read_one_char_with_prompt(ctx->out, ctx->io_status, prompt);
    return 0;
}

static int io_read_stdin_line(FlatNativeCtx *ctx, char *linebuf, size_t linebuf_size,
                              int allow_empty, int input_kind) {
    char prompt[256];

    if (eval_prompt(ctx, prompt, sizeof prompt) != 0) {
        return -1;
    }
    if (ctx->io_ctx) {
        flat_io_ctx_bind_vars(ctx->io_ctx, ctx->vars, ctx->var_count, ctx->vcx);
        emit_prompt(prompt);
        flat_io_ctx_read_line(ctx->io_ctx, ctx, linebuf, linebuf_size, allow_empty, input_kind,
                              ctx->io_status);
        return 0;
    }
    emit_prompt(prompt);
    read_line_with_prompt(linebuf, linebuf_size, allow_empty, prompt, input_kind, ctx->io_status);
    return 0;
}

int flat_native_io_inln(FlatNativeCtx *ctx) {
    char linebuf[4096];
    char prompt[256];
    FlatSlotType expect;
    int has_expect = 0;
    FlatValue outv;

    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    ctx->panic_detail[0] = '\0';
    if (io_parse_prompt_and_type(ctx, prompt, sizeof prompt, &expect, &has_expect) != 0) {
        io_set_type_io_panic(ctx, "type.io.expect");
        return -1;
    }
    if (io_read_stdin_line_prompt(ctx, linebuf, sizeof linebuf, 0, FLAT_WASM_IO_KIND_LN, prompt) !=
        0) {
        return -1;
    }
    if (*ctx->io_status != FLAT_IO_OK) {
        return 0;
    }
    if (flat_value_set_str_inline(&outv, linebuf, strlen(linebuf)) != 0) {
        return -1;
    }
    if (io_apply_expect_type(ctx, &outv, &expect, has_expect) != 0) {
        return -1;
    }
    if (outv.tag == VAL_STR_INLINE) {
        return flat_value_set_str_inline(ctx->out, outv.u.ins.bytes, outv.u.ins.len);
    }
    *ctx->out = outv;
    return 0;
}

int flat_native_io_in_num(FlatNativeCtx *ctx) {
    char linebuf[4096];
    char prompt[256];
    FlatSlotType expect;
    int has_expect = 0;

    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    ctx->panic_detail[0] = '\0';
    if (io_parse_prompt_and_type(ctx, prompt, sizeof prompt, &expect, &has_expect) != 0) {
        io_set_type_io_panic(ctx, "type.io.expect");
        return -1;
    }
    if (io_read_stdin_line_prompt(ctx, linebuf, sizeof linebuf, 0, FLAT_WASM_IO_KIND_NUM, prompt) !=
        0) {
        return -1;
    }
    if (*ctx->io_status != FLAT_IO_OK) {
        return 0;
    }
    if (parse_in_line_num(linebuf, ctx->out, ctx->io_status) != 0) {
        return 0;
    }
    if (*ctx->io_status != FLAT_IO_OK) {
        return 0;
    }
    if (io_apply_expect_type(ctx, ctx->out, &expect, has_expect) != 0) {
        return -1;
    }
    return 0;
}

int flat_native_io_in_bool(FlatNativeCtx *ctx) {
    char linebuf[4096];
    char prompt[256];
    FlatSlotType expect;
    int has_expect = 0;

    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    ctx->panic_detail[0] = '\0';
    if (io_parse_prompt_and_type(ctx, prompt, sizeof prompt, &expect, &has_expect) != 0) {
        io_set_type_io_panic(ctx, "type.io.expect");
        return -1;
    }
    if (io_read_stdin_line_prompt(ctx, linebuf, sizeof linebuf, 0, FLAT_WASM_IO_KIND_BOOL,
                                  prompt) != 0) {
        return -1;
    }
    if (*ctx->io_status != FLAT_IO_OK) {
        return 0;
    }
    if (parse_in_line_bool(linebuf, 1, ctx->out, ctx->io_status) != 0) {
        return 0;
    }
    if (*ctx->io_status != FLAT_IO_OK) {
        return 0;
    }
    if (io_apply_expect_type(ctx, ctx->out, &expect, has_expect) != 0) {
        return -1;
    }
    return 0;
}

int flat_native_io_in_list(FlatNativeCtx *ctx) {
    char linebuf[4096];

    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    if (io_read_stdin_line(ctx, linebuf, sizeof linebuf, 1, FLAT_WASM_IO_KIND_LIST) != 0) {
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
    if (io_read_stdin_line(ctx, linebuf, sizeof linebuf, 1, FLAT_WASM_IO_KIND_OBJECT) != 0) {
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
    if (io_read_stdin_line(ctx, linebuf, sizeof linebuf, 1, FLAT_WASM_IO_KIND_BOOLS) != 0) {
        return -1;
    }
    if (*ctx->io_status != FLAT_IO_OK) {
        return 0;
    }
    return parse_in_collection_line(linebuf, 2, ctx->out, ctx->pool, ctx->coll, ctx->io_status);
}

int flat_native_io_ctxin(FlatNativeCtx *ctx) {
    FlatIoStatus st;
    if (!ctx->io_ctx || !ctx->io_status) {
        return -1;
    }
    flat_io_ctx_bind_vars(ctx->io_ctx, ctx->vars, ctx->var_count, ctx->vcx);
    flat_io_ctx_set_in(ctx->io_ctx, ctx, &st);
    *ctx->io_status = st;
    return 0;
}

int flat_native_io_ctxout(FlatNativeCtx *ctx) {
    FlatIoStatus st;
    if (!ctx->io_ctx || !ctx->io_status) {
        return -1;
    }
    flat_io_ctx_bind_vars(ctx->io_ctx, ctx->vars, ctx->var_count, ctx->vcx);
    flat_io_ctx_set_out(ctx->io_ctx, ctx, &st);
    *ctx->io_status = st;
    return 0;
}

int flat_native_io_ctxerr(FlatNativeCtx *ctx) {
    FlatIoStatus st;
    if (!ctx->io_ctx || !ctx->io_status) {
        return -1;
    }
    flat_io_ctx_bind_vars(ctx->io_ctx, ctx->vars, ctx->var_count, ctx->vcx);
    flat_io_ctx_set_err(ctx->io_ctx, ctx, &st);
    *ctx->io_status = st;
    return 0;
}

int flat_native_io_err(FlatNativeCtx *ctx) {
    FlatValue v;
    if (!ctx->io_status) {
        return -1;
    }
    if (eval_io_arg(ctx, &v) != 0) {
        return -1;
    }
    if (ctx->io_ctx) {
        flat_io_ctx_bind_vars(ctx->io_ctx, ctx->vars, ctx->var_count, ctx->vcx);
        return flat_io_ctx_write(ctx->io_ctx, ctx, 2, &v, 0);
    }
    flat_value_fprint_io(stderr, &v, ctx->pool, ctx->coll);
    fflush(stderr);
    *ctx->io_status = FLAT_IO_OK;
    return 0;
}

int flat_native_io_errln(FlatNativeCtx *ctx) {
    FlatValue v;
    if (!ctx->io_status) {
        return -1;
    }
    if (eval_io_arg(ctx, &v) != 0) {
        return -1;
    }
    if (ctx->io_ctx) {
        flat_io_ctx_bind_vars(ctx->io_ctx, ctx->vars, ctx->var_count, ctx->vcx);
        return flat_io_ctx_write(ctx->io_ctx, ctx, 2, &v, 1);
    }
    flat_value_fprint_io_ln(stderr, &v, ctx->pool, ctx->coll);
    fflush(stderr);
    *ctx->io_status = FLAT_IO_OK;
    return 0;
}
