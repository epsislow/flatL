#include "flat_io_ctx.h"
#include "flat_memstream.h"
#include "flat_native_util.h"
#include "flat_value.h"
#include "flat_wasm_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    EP_TIN,
    EP_TOUT,
    EP_TERR,
    EP_LITERAL,
    EP_SNAPSHOT,
    EP_REF,
    EP_FILE
} EpKind;

typedef struct {
    EpKind kind;
    char *buf;
    size_t len;
    size_t pos;
    int var_idx;
    char ref_var_name[FLAT_VAR_NAME_MAX + 1];
    uint32_t ref_id;
    uint32_t file_id;
} IoEndpoint;

struct FlatIOContext {
    FlatFileTable *files;
    FlatValue *vars;
    int var_count;
    FlatVcxRuntime *vcx;
    char panic_reason[64];
    IoEndpoint in;
    IoEndpoint out;
    IoEndpoint err;
};

static void ep_clear(IoEndpoint *ep) {
    free(ep->buf);
    memset(ep, 0, sizeof *ep);
}

static void ep_set_terminal(IoEndpoint *ep, EpKind term) {
    ep_clear(ep);
    ep->kind = term;
}

FlatIOContext *flat_io_ctx_create(FlatFileTable *files) {
    FlatIOContext *io = calloc(1, sizeof *io);
    if (!io) {
        return NULL;
    }
    io->files = files;
    ep_set_terminal(&io->in, EP_TIN);
    ep_set_terminal(&io->out, EP_TOUT);
    ep_set_terminal(&io->err, EP_TERR);
    return io;
}

void flat_io_ctx_destroy(FlatIOContext *io) {
    if (!io) {
        return;
    }
    ep_clear(&io->in);
    ep_clear(&io->out);
    ep_clear(&io->err);
    free(io);
}

void flat_io_ctx_set_vcx(FlatIOContext *io, FlatVcxRuntime *vcx) {
    if (io) {
        io->vcx = vcx;
    }
}

void flat_io_ctx_bind_vars(FlatIOContext *io, FlatValue *vars, int var_count, FlatVcxRuntime *vcx) {
    if (io) {
        io->vars = vars;
        io->var_count = var_count;
        if (vcx) {
            io->vcx = vcx;
        }
    }
}

const char *flat_io_ctx_panic_reason(const FlatIOContext *io) {
    return io && io->panic_reason[0] ? io->panic_reason : "io.context";
}

static void ctx_panic(FlatIOContext *io, const char *reason) {
    if (io) {
        snprintf(io->panic_reason, sizeof io->panic_reason, "%s", reason ? reason : "io.context");
    }
}

static int eval_arg_value(FlatNativeCtx *ctx, FlatValue *out) {
    char inner_buf[4096];
    char trimmed[4096];

    if (!ctx->inner) {
        return -1;
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    flat_native_trim(inner_buf);
    snprintf(trimmed, sizeof trimmed, "%s", inner_buf);
    flat_native_trim(trimmed);
    if (strcmp(trimmed, "tin") == 0 || strcmp(trimmed, "tout") == 0 ||
        strcmp(trimmed, "terr") == 0) {
        if (strcmp(trimmed, "tin") == 0) {
            out->tag = VAL_EMPTY;
            out->u.number = 1; /* sentinel: tin */
        } else if (strcmp(trimmed, "tout") == 0) {
            out->tag = VAL_EMPTY;
            out->u.number = 2;
        } else {
            out->tag = VAL_EMPTY;
            out->u.number = 3;
        }
        return 0;
    }
    if (flat_native_eval_call(ctx, inner_buf, out) == 0) {
        return 0;
    }
    return flat_value_parse_assign_rhs(inner_buf, out, ctx->pool, ctx->coll, ctx->vars,
                                       ctx->var_count, ctx->var_names);
}

static int ref_still_bound(FlatIOContext *io, IoEndpoint *ep, FlatStringPool *pool,
                           const FlatNativeCtx *ctx) {
    FlatValue *v;
    int vi;

    if (ep->kind != EP_REF) {
        return 1;
    }
    if (pool && !flat_pool_slot_in_use(pool, ep->ref_id)) {
        return 0;
    }
    if (!io->vars || !ctx || !ctx->var_names) {
        return 0;
    }
    if (ep->ref_var_name[0] != '\0') {
        vi = flat_native_find_var(ep->ref_var_name, io->var_count, ctx->var_names);
    } else {
        vi = ep->var_idx;
    }
    if (vi < 0) {
        /* Name no longer in current var table (e.g. variable table replaced). */
        return 0;
    }
    if (vi >= io->var_count) {
        return 0;
    }
    v = &io->vars[vi];
    return v->tag == VAL_STR_REF && v->u.ref_id == ep->ref_id;
}

static int value_to_bytes(FlatNativeCtx *ctx, const FlatValue *v, int with_nl, char **out_buf,
                          size_t *out_len) {
    FILE *mem;
    char *buf = NULL;
    size_t sz = 0;

    mem = flat_open_memstream(&buf, &sz);
    if (!mem) {
        return -1;
    }
    flat_value_fprint_io(mem, v, ctx->pool, ctx->coll);
    if (with_nl) {
        fputc('\n', mem);
    }
    if (flat_close_memstream(mem) != 0) {
        return -1;
    }
    *out_buf = buf;
    *out_len = sz;
    return 0;
}

static int write_bytes_to_ep(FlatIOContext *io, FlatNativeCtx *ctx, IoEndpoint *ep,
                             const char *data, size_t len, FlatIoStatus *st) {
    if (ep->kind == EP_REF && !ref_still_bound(io, ep, ctx->pool, ctx)) {
        ctx_panic(io, "io.ctx_stale");
        *st = FLAT_IO_PANIC_CTX;
        return -1;
    }
    switch (ep->kind) {
    case EP_TOUT:
#ifdef FLAT_WASM_BUILD
        flat_wasm_io_write_stdout(data, len);
#else
        fwrite(data, 1, len, stdout);
        fflush(stdout);
#endif
        break;
    case EP_TERR:
#ifdef FLAT_WASM_BUILD
        flat_wasm_io_write_stderr(data, len);
#else
        fwrite(data, 1, len, stderr);
        fflush(stderr);
#endif
        break;
    case EP_REF:
        if (flat_pool_append(ctx->pool, ep->ref_id, data, len) != 0) {
            return -1;
        }
        break;
    case EP_SNAPSHOT:
        if (ep->len + len > FLAT_INLINE_STR_MAX) {
            ctx_panic(io, "io.context");
            *st = FLAT_IO_PANIC_CTX;
            return -1;
        }
        ep->buf = realloc(ep->buf, ep->len + len + 1);
        if (!ep->buf) {
            return -1;
        }
        memcpy(ep->buf + ep->len, data, len);
        ep->len += len;
        ep->buf[ep->len] = '\0';
        break;
    case EP_FILE: {
        FILE *fp = flat_file_fp(io->files, ep->file_id);
        if (!fp) {
            ctx_panic(io, "io.context");
            *st = FLAT_IO_PANIC_CTX;
            return -1;
        }
        if (fwrite(data, 1, len, fp) != len) {
            return -1;
        }
        break;
    }
    default:
        ctx_panic(io, "io.context");
        *st = FLAT_IO_PANIC_CTX;
        return -1;
    }
    return 0;
}

int flat_io_ctx_write(FlatIOContext *io, FlatNativeCtx *ctx, int stream, const FlatValue *v,
                      int with_nl) {
    IoEndpoint *ep;
    char *bytes = NULL;
    size_t blen = 0;
    FlatIoStatus st = FLAT_IO_OK;

    if (!io || !ctx || !ctx->io_status) {
        return -1;
    }
    ep = stream == 2 ? &io->err : &io->out;
    if (value_to_bytes(ctx, v, with_nl, &bytes, &blen) != 0) {
        return -1;
    }
    if (write_bytes_to_ep(io, ctx, ep, bytes, blen, &st) != 0) {
        free(bytes);
        *ctx->io_status = st;
        return st == FLAT_IO_OK ? -1 : 0;
    }
    free(bytes);
    *ctx->io_status = FLAT_IO_OK;
    return 0;
}

static int ep_bind_from_value(FlatIOContext *io, FlatNativeCtx *ctx, IoEndpoint *ep,
                              const FlatValue *v, int allow_in, int allow_out, int allow_err,
                              FlatIoStatus *st) {
    const uint8_t *p;
    size_t len;

    ep_clear(ep);
    if (v->tag == VAL_EMPTY) {
        if (v->u.number == 1) {
            if (!allow_in) {
                goto invalid;
            }
            ep->kind = EP_TIN;
            return 0;
        }
        if (v->u.number == 2) {
            if (!allow_out) {
                goto invalid;
            }
            ep->kind = EP_TOUT;
            return 0;
        }
        if (v->u.number == 3) {
            if (!allow_err) {
                goto invalid;
            }
            ep->kind = EP_TERR;
            return 0;
        }
    }
    if (v->tag == VAL_FILE) {
        if (!flat_file_is_live(io->files, v->u.file_id)) {
            goto invalid;
        }
        if (!allow_in && !allow_out && !allow_err) {
            goto invalid;
        }
        ep->kind = EP_FILE;
        ep->file_id = v->u.file_id;
        return 0;
    }
    if (v->tag == VAL_STR_REF) {
        if (!allow_out && !allow_err && !allow_in) {
            goto invalid;
        }
        {
            char name[FLAT_VAR_NAME_MAX + 1];
            int vi;
            snprintf(name, sizeof name, "%s", ctx->inner ? ctx->inner : "");
            flat_native_trim(name);
            vi = flat_native_find_var(name, ctx->var_count, ctx->var_names);
            if (vi >= 0 && (allow_out || allow_err)) {
                if (ctx->var_slot_frozen && ctx->var_slot_frozen[vi]) {
                    ctx_panic(io, "type.freeze.ctx");
                    *st = FLAT_IO_PANIC_CTX;
                    return -1;
                }
                if (ctx->var_slot_const && ctx->var_slot_const[vi]) {
                    ctx_panic(io, "type.freeze.ctx");
                    *st = FLAT_IO_PANIC_CTX;
                    return -1;
                }
            }
            ep->kind = EP_REF;
            ep->ref_id = v->u.ref_id;
            ep->var_idx = vi;
            snprintf(ep->ref_var_name, sizeof ep->ref_var_name, "%s", name);
        }
        return 0;
    }
    if (v->tag == VAL_STR_INLINE) {
        if (allow_in) {
            ep->kind = EP_LITERAL;
            ep->len = v->u.ins.len;
            ep->buf = malloc(ep->len + 1);
            if (!ep->buf) {
                return -1;
            }
            memcpy(ep->buf, v->u.ins.bytes, ep->len);
            ep->buf[ep->len] = '\0';
            ep->pos = 0;
            return 0;
        }
        if (allow_out || allow_err) {
            ep->kind = EP_SNAPSHOT;
            ep->len = v->u.ins.len;
            ep->buf = malloc(ep->len + 1);
            if (!ep->buf) {
                return -1;
            }
            memcpy(ep->buf, v->u.ins.bytes, ep->len);
            ep->buf[ep->len] = '\0';
            return 0;
        }
    }
    if ((v->tag == VAL_STR_INLINE || v->tag == VAL_STR_REF) && allow_in) {
        (void)p;
        (void)len;
    }

invalid:
    ctx_panic(io, "io.context");
    *st = FLAT_IO_PANIC_CTX;
    return -1;
}

static int set_ctx(FlatIOContext *io, FlatNativeCtx *ctx, IoEndpoint *ep, int allow_in,
                   int allow_out, int allow_err) {
    FlatValue v;
    FlatIoStatus st = FLAT_IO_OK;

    if (!ctx->io_status) {
        return -1;
    }
    if (eval_arg_value(ctx, &v) != 0) {
        ctx_panic(io, "io.context");
        *ctx->io_status = FLAT_IO_PANIC_CTX;
        return 0;
    }
    if (ep_bind_from_value(io, ctx, ep, &v, allow_in, allow_out, allow_err, &st) != 0) {
        *ctx->io_status = st;
        return 0;
    }
    *ctx->io_status = FLAT_IO_OK;
    return 0;
}

int flat_io_ctx_set_in(FlatIOContext *io, FlatNativeCtx *ctx, FlatIoStatus *st) {
    if (set_ctx(io, ctx, &io->in, 1, 0, 0) != 0) {
        return -1;
    }
    *st = *ctx->io_status;
    return 0;
}

int flat_io_ctx_set_out(FlatIOContext *io, FlatNativeCtx *ctx, FlatIoStatus *st) {
    if (set_ctx(io, ctx, &io->out, 0, 1, 0) != 0) {
        return -1;
    }
    *st = *ctx->io_status;
    return 0;
}

int flat_io_ctx_set_err(FlatIOContext *io, FlatNativeCtx *ctx, FlatIoStatus *st) {
    if (set_ctx(io, ctx, &io->err, 0, 0, 1) != 0) {
        return -1;
    }
    *st = *ctx->io_status;
    return 0;
}

static int read_line_from_string(IoEndpoint *ep, char *buf, size_t buf_size, FlatIoStatus *st) {
    size_t i = 0;
    if (ep->pos >= ep->len) {
        *st = FLAT_IO_PANIC_EOF;
        return -1;
    }
    while (ep->pos < ep->len && i + 1 < buf_size) {
        char c = ep->buf[ep->pos++];
        if (c == '\n') {
            break;
        }
        if (c == '\r') {
            continue;
        }
        buf[i++] = c;
    }
    buf[i] = '\0';
    *st = FLAT_IO_OK;
    return 0;
}

int flat_io_ctx_read_line(FlatIOContext *io, FlatNativeCtx *ctx, char *buf, size_t buf_size,
                          int allow_empty, int input_kind, FlatIoStatus *st) {
    IoEndpoint *ep = &io->in;

    (void)ctx;
    if (ep->kind == EP_LITERAL || ep->kind == EP_SNAPSHOT) {
        if (read_line_from_string(ep, buf, buf_size, st) != 0) {
            return 0;
        }
        flat_native_trim(buf);
        if (!allow_empty && buf[0] == '\0') {
            *st = FLAT_IO_PANIC_CANCEL;
        }
        return 0;
    }
    if (ep->kind == EP_REF) {
        if (!ref_still_bound(io, ep, ctx->pool, ctx)) {
            ctx_panic(io, "io.ctx_stale");
            *st = FLAT_IO_PANIC_CTX;
            return 0;
        }
        {
            const uint8_t *p = flat_pool_read(ctx->pool, ep->ref_id, NULL);
            size_t len = 0;
            if (!p) {
                *st = FLAT_IO_PANIC_EOF;
                return 0;
            }
            flat_pool_read(ctx->pool, ep->ref_id, &len);
            if (ep->pos >= len) {
                *st = FLAT_IO_PANIC_EOF;
                return 0;
            }
            {
                size_t i = 0;
                while (ep->pos < len && i + 1 < buf_size) {
                    char c = (char)p[ep->pos++];
                    if (c == '\n') {
                        break;
                    }
                    if (c == '\r') {
                        continue;
                    }
                    buf[i++] = c;
                }
                buf[i] = '\0';
            }
            flat_native_trim(buf);
            if (!allow_empty && buf[0] == '\0') {
                *st = FLAT_IO_PANIC_CANCEL;
            } else {
                *st = FLAT_IO_OK;
            }
        }
        return 0;
    }
    if (ep->kind == EP_FILE) {
        FILE *fp = flat_file_fp(io->files, ep->file_id);
        if (!fp || !fgets(buf, (int)buf_size, fp)) {
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
        if (!allow_empty && buf[0] == '\0') {
            *st = FLAT_IO_PANIC_CANCEL;
        } else {
            *st = FLAT_IO_OK;
        }
        return 0;
    }
#ifdef FLAT_WASM_BUILD
    if (flat_wasm_io_read_line(buf, buf_size, allow_empty, "", input_kind, st) != 0) {
        *st = FLAT_IO_PANIC_EOF;
    }
#else
    (void)input_kind;
    (void)allow_empty;
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
    if (!allow_empty && buf[0] == '\0') {
        *st = FLAT_IO_PANIC_CANCEL;
    } else {
        *st = FLAT_IO_OK;
    }
#endif
    return 0;
}

int flat_io_ctx_read_char(FlatIOContext *io, FlatNativeCtx *ctx, char *ch_out, FlatIoStatus *st) {
    IoEndpoint *ep = &io->in;

    (void)ctx;
    if (ep->kind == EP_LITERAL || ep->kind == EP_SNAPSHOT || ep->kind == EP_REF) {
        char line[8];
        if (flat_io_ctx_read_line(io, ctx, line, sizeof line, 1, 0, st) != 0) {
            return -1;
        }
        if (*st != FLAT_IO_OK) {
            return 0;
        }
        if (line[0] == '\0') {
            *st = FLAT_IO_PANIC_CANCEL;
            return 0;
        }
        ch_out[0] = line[0];
        ch_out[1] = '\0';
        *st = FLAT_IO_OK;
        return 0;
    }
    if (ep->kind == EP_FILE) {
        int c = fgetc(flat_file_fp(io->files, ep->file_id));
        if (c == EOF) {
            *st = FLAT_IO_PANIC_EOF;
            return 0;
        }
        ch_out[0] = (char)c;
        ch_out[1] = '\0';
        *st = FLAT_IO_OK;
        return 0;
    }
#ifdef FLAT_WASM_BUILD
    {
        char c = 0;
        if (flat_wasm_io_read_char(&c, "", st) != 0) {
            *st = FLAT_IO_PANIC_EOF;
            return 0;
        }
        if (*st != FLAT_IO_OK) {
            return 0;
        }
        ch_out[0] = c;
        ch_out[1] = '\0';
    }
#else
    {
        int c = fgetc(stdin);
        if (c == EOF) {
            *st = FLAT_IO_PANIC_EOF;
            return 0;
        }
        if (c == 27 || c == '\n') {
            *st = FLAT_IO_PANIC_CANCEL;
            return 0;
        }
        ch_out[0] = (char)c;
        ch_out[1] = '\0';
        *st = FLAT_IO_OK;
    }
#endif
    return 0;
}

void flat_io_ctx_var_updated(FlatIOContext *io, int var_index, const FlatValue *new_val) {
    (void)new_val;
    (void)var_index;
    (void)io;
}

void flat_io_ctx_reset_terminals(FlatIOContext *io) {
    if (!io) {
        return;
    }
    ep_set_terminal(&io->in, EP_TIN);
    ep_set_terminal(&io->out, EP_TOUT);
    ep_set_terminal(&io->err, EP_TERR);
}

void flat_io_ctx_file_closed(FlatIOContext *io, uint32_t file_id) {
    if (!io) {
        return;
    }
    if (io->in.kind == EP_FILE && io->in.file_id == file_id) {
        ep_set_terminal(&io->in, EP_TIN);
    }
    if (io->out.kind == EP_FILE && io->out.file_id == file_id) {
        ep_set_terminal(&io->out, EP_TOUT);
    }
    if (io->err.kind == EP_FILE && io->err.file_id == file_id) {
        ep_set_terminal(&io->err, EP_TERR);
    }
}
