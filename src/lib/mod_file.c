#include "flat_csv_runtime.h"
#include "flat_number.h"
#include "flat_file.h"
#include "flat_path.h"
#include "flat_io_ctx.h"
#include "flat_memstream.h"
#include "flat_native_mod.h"
#include "flat_native_util.h"
#include "flat_value.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int file_fail(FlatNativeCtx *ctx, const char *why) {
    if (ctx && ctx->io_status) {
        *ctx->io_status = FLAT_IO_ERR;
    }
    if (ctx && why) {
        snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", why);
    }
    return -1;
}

static int parse_expr(FlatNativeCtx *ctx, const char *expr, FlatValue *out) {
    char buf[4096];

    if (!expr || !out) {
        return -1;
    }
    snprintf(buf, sizeof buf, "%s", expr);
    flat_native_trim(buf);
    if (flat_native_eval_call(ctx, buf, out) == 0) {
        return 0;
    }
    return flat_value_parse_assign_rhs(buf, out, ctx->pool, ctx->coll, ctx->vars, ctx->var_count,
                                       ctx->var_names);
}

static int path_from_expr(FlatNativeCtx *ctx, const char *expr, char *path, size_t path_sz) {
    FlatValue v;

    if (parse_expr(ctx, expr, &v) != 0) {
        return -1;
    }
    return flat_value_get_cstr(&v, ctx->pool, path, path_sz);
}

static int require_fp(FlatNativeCtx *ctx, const char *expr, uint32_t *out_id, const char *id_why) {
    FlatValue v;

    if (parse_expr(ctx, expr, &v) != 0) {
        return file_fail(ctx, id_why);
    }
    if (!ctx->file_tbl || !flat_value_get_file_handle(&v, ctx->file_tbl, out_id)) {
        return file_fail(ctx, id_why);
    }
    return 0;
}

static int open_path_native(FlatNativeCtx *ctx, FlatFileMode mode, const char *fn) {
    char path[4096];
    char why[64];
    uint32_t id;

    if (!ctx->out || !ctx->io_status || !ctx->file_tbl) {
        snprintf(why, sizeof why, "file.%s", fn);
        return file_fail(ctx, why);
    }
    if (path_from_expr(ctx, ctx->inner, path, sizeof path) != 0) {
        snprintf(why, sizeof why, "file.%s.path", fn);
        return file_fail(ctx, why);
    }
    if (flat_file_open(ctx->file_tbl, path, mode, &id) != 0) {
        snprintf(why, sizeof why, "file.%s.open", fn);
        return file_fail(ctx, why);
    }
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_file_handle(ctx->out, id);
}

int flat_native_file_read_ctx(FlatNativeCtx *ctx) {
    return open_path_native(ctx, FLAT_FILE_MODE_READ, "readCtx");
}

int flat_native_file_create_ctx(FlatNativeCtx *ctx) {
    return open_path_native(ctx, FLAT_FILE_MODE_CREATE, "createCtx");
}

int flat_native_file_write_ctx(FlatNativeCtx *ctx) {
    return open_path_native(ctx, FLAT_FILE_MODE_WRITE_AT0, "writeCtx");
}

int flat_native_file_append_ctx(FlatNativeCtx *ctx) {
    return open_path_native(ctx, FLAT_FILE_MODE_APPEND, "appendCtx");
}

int flat_native_file_read_write_ctx(FlatNativeCtx *ctx) {
    return open_path_native(ctx, FLAT_FILE_MODE_RDWR, "readWriteCtx");
}

int flat_native_file_readln(FlatNativeCtx *ctx) {
    char line[4096];
    uint32_t fid;
    FILE *fp;

    if (!ctx->out || !ctx->io_status) {
        return file_fail(ctx, "file.readln");
    }
    if (require_fp(ctx, ctx->inner, &fid, "file.readln.id") != 0) {
        return -1;
    }
    fp = flat_file_fp(ctx->file_tbl, fid);
    if (!fp || !fgets(line, (int)sizeof line, fp)) {
        *ctx->io_status = FLAT_IO_PANIC_EOF;
        return 0;
    }
    {
        size_t n = strlen(line);
        if (n > 0 && line[n - 1] == '\n') {
            line[n - 1] = '\0';
        }
    }
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_str_inline(ctx->out, line, strlen(line));
}

static int split_two_args(char *inner, char **left, char **right) {
    int depth = 0;
    char *p;

    for (p = inner; *p; p++) {
        if (*p == '[' || *p == '(') {
            depth++;
        } else if (*p == ']' || *p == ')') {
            if (depth > 0) {
                depth--;
            }
        } else if (*p == ',' && depth == 0) {
            *p = '\0';
            *left = flat_native_trim(inner);
            *right = flat_native_trim(p + 1);
            return 0;
        }
    }
    return -1;
}

int flat_native_file_read(FlatNativeCtx *ctx) {
    char inner_buf[4096];
    char *left;
    char *right;
    uint32_t fid;
    FILE *fp;
    long start;
    size_t cap = 4096;
    size_t len = 0;
    char *buf;
    size_t max_read = (size_t)-1;

    if (!ctx->out || !ctx->io_status || !ctx->inner) {
        return file_fail(ctx, "file.read");
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &left, &right) == 0) {
        FlatValue n;
        if (parse_expr(ctx, right, &n) != 0 || n.tag != VAL_NUMBER) {
            return file_fail(ctx, "file.read.args");
        }
        if (flat_number_as_f64(&n) < 0) {
            return file_fail(ctx, "file.read.range");
        }
        max_read = (size_t)flat_number_as_f64(&n);
    } else {
        left = flat_native_trim(inner_buf);
    }
    if (require_fp(ctx, left, &fid, "file.read.id") != 0) {
        return -1;
    }
    fp = flat_file_fp(ctx->file_tbl, fid);
    if (!fp) {
        return file_fail(ctx, "file.read.id");
    }
    start = ftell(fp);
    if (start < 0) {
        return file_fail(ctx, "file.read.io");
    }
    buf = malloc(cap);
    if (!buf) {
        return file_fail(ctx, "file.read.memory");
    }
    while (len < max_read) {
        int c = fgetc(fp);
        if (c == EOF) {
            break;
        }
        if (len + 1 >= cap) {
            char *grown = realloc(buf, cap * 2);
            if (!grown) {
                free(buf);
                return file_fail(ctx, "file.read.memory");
            }
            buf = grown;
            cap *= 2;
        }
        buf[len++] = (char)c;
    }
    if (len == 0 && feof(fp) && start == ftell(fp)) {
        free(buf);
        *ctx->io_status = FLAT_IO_PANIC_EOF;
        return 0;
    }
    {
        uint32_t rid;
        if (flat_pool_alloc_copy(ctx->pool, buf, len, &rid) != 0) {
            free(buf);
            return file_fail(ctx, "pool.full");
        }
        free(buf);
        *ctx->io_status = FLAT_IO_OK;
        return flat_value_set_str_ref(ctx->out, rid);
    }
}

static int string_payload(const FlatValue *v, const FlatStringPool *pool, const uint8_t **out_p,
                          size_t *out_len) {
    if (!v || !out_p || !out_len) {
        return -1;
    }
    if (v->tag == VAL_STR_INLINE) {
        *out_p = (const uint8_t *)v->u.ins.bytes;
        *out_len = v->u.ins.len;
        return 0;
    }
    if (v->tag == VAL_STR_REF && pool) {
        *out_p = flat_pool_read(pool, v->u.ref_id, out_len);
        return *out_p ? 0 : -1;
    }
    return -1;
}

int flat_native_file_write(FlatNativeCtx *ctx) {
    char inner_buf[4096];
    char *left;
    char *right;
    uint32_t fid;
    FILE *fp;
    FlatValue data;
    char *bytes = NULL;
    size_t blen = 0;
    FILE *mem;

    if (!ctx->io_status || !ctx->inner) {
        return file_fail(ctx, "file.write");
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &left, &right) != 0) {
        return file_fail(ctx, "file.write.args");
    }
    if (require_fp(ctx, left, &fid, "file.write.id") != 0) {
        return -1;
    }
    if (parse_expr(ctx, right, &data) != 0) {
        return file_fail(ctx, "file.write.args");
    }
    fp = flat_file_fp(ctx->file_tbl, fid);
    if (!fp) {
        return file_fail(ctx, "file.write.id");
    }
    mem = flat_open_memstream(&bytes, &blen);
    if (!mem) {
        return file_fail(ctx, "file.write.memory");
    }
    flat_value_fprint_io(mem, &data, ctx->pool, ctx->coll);
    if (flat_close_memstream(mem) != 0) {
        return file_fail(ctx, "file.write.memory");
    }
    if (fwrite(bytes, 1, blen, fp) != blen) {
        free(bytes);
        return file_fail(ctx, "file.write.io");
    }
    free(bytes);
    *ctx->io_status = FLAT_IO_OK;
    return 0;
}

int flat_native_file_writeln(FlatNativeCtx *ctx) {
    char inner_buf[4096];
    char *left;
    char *right;
    uint32_t fid;
    FILE *fp;
    FlatValue data;
    char *bytes = NULL;
    size_t blen = 0;
    FILE *mem;

    if (!ctx->io_status || !ctx->inner) {
        return file_fail(ctx, "file.writeln");
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &left, &right) != 0) {
        return file_fail(ctx, "file.writeln.args");
    }
    if (require_fp(ctx, left, &fid, "file.writeln.id") != 0) {
        return -1;
    }
    if (parse_expr(ctx, right, &data) != 0) {
        return file_fail(ctx, "file.writeln.args");
    }
    fp = flat_file_fp(ctx->file_tbl, fid);
    if (!fp) {
        return file_fail(ctx, "file.writeln.id");
    }
    mem = flat_open_memstream(&bytes, &blen);
    if (!mem) {
        return file_fail(ctx, "file.writeln.memory");
    }
    flat_value_fprint_io_ln(mem, &data, ctx->pool, ctx->coll);
    if (flat_close_memstream(mem) != 0) {
        return file_fail(ctx, "file.writeln.memory");
    }
    if (fwrite(bytes, 1, blen, fp) != blen) {
        free(bytes);
        return file_fail(ctx, "file.writeln.io");
    }
    free(bytes);
    *ctx->io_status = FLAT_IO_OK;
    return 0;
}

int flat_native_file_write_bytes(FlatNativeCtx *ctx) {
    char inner_buf[4096];
    char *left;
    char *right;
    uint32_t fid;
    FILE *fp;
    FlatValue data;
    const uint8_t *payload;
    size_t plen;

    if (!ctx->io_status || !ctx->inner) {
        return file_fail(ctx, "file.writeBytes");
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &left, &right) != 0) {
        return file_fail(ctx, "file.writeBytes.args");
    }
    if (require_fp(ctx, left, &fid, "file.writeBytes.id") != 0) {
        return -1;
    }
    if (parse_expr(ctx, right, &data) != 0) {
        return file_fail(ctx, "file.writeBytes.args");
    }
    if (string_payload(&data, ctx->pool, &payload, &plen) != 0) {
        return file_fail(ctx, "file.writeBytes.args");
    }
    fp = flat_file_fp(ctx->file_tbl, fid);
    if (!fp) {
        return file_fail(ctx, "file.writeBytes.id");
    }
    if (plen > 0 && fwrite(payload, 1, plen, fp) != plen) {
        return file_fail(ctx, "file.writeBytes.io");
    }
    *ctx->io_status = FLAT_IO_OK;
    return 0;
}

int flat_native_file_flush(FlatNativeCtx *ctx) {
    uint32_t fid;
    FILE *fp;

    if (!ctx->io_status) {
        return file_fail(ctx, "file.flush");
    }
    if (require_fp(ctx, ctx->inner, &fid, "file.flush.id") != 0) {
        return -1;
    }
    fp = flat_file_fp(ctx->file_tbl, fid);
    if (!fp || fflush(fp) != 0) {
        return file_fail(ctx, "file.flush.io");
    }
    *ctx->io_status = FLAT_IO_OK;
    return 0;
}

static int file_out_num(FlatNativeCtx *ctx, double n) {
    if (!ctx->out || !ctx->io_status) {
        return file_fail(ctx, "file.pos");
    }
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_number(ctx->out, n);
}

int flat_native_file_pos(FlatNativeCtx *ctx) {
    uint32_t fid;
    FILE *fp;
    long p;

    if (require_fp(ctx, ctx->inner, &fid, "file.pos.id") != 0) {
        return -1;
    }
    fp = flat_file_fp(ctx->file_tbl, fid);
    p = fp ? ftell(fp) : -1;
    if (p < 0) {
        return file_fail(ctx, "file.pos.io");
    }
    return file_out_num(ctx, (double)p);
}

int flat_native_file_size(FlatNativeCtx *ctx) {
    uint32_t fid;
    FILE *fp;
    long cur;
    long end;

    if (require_fp(ctx, ctx->inner, &fid, "file.size.id") != 0) {
        return -1;
    }
    fp = flat_file_fp(ctx->file_tbl, fid);
    if (!fp) {
        return file_fail(ctx, "file.size.id");
    }
    cur = ftell(fp);
    if (fseek(fp, 0, SEEK_END) != 0) {
        return file_fail(ctx, "file.size.io");
    }
    end = ftell(fp);
    if (cur >= 0 && fseek(fp, cur, SEEK_SET) != 0) {
        return file_fail(ctx, "file.size.io");
    }
    if (end < 0) {
        return file_fail(ctx, "file.size.io");
    }
    return file_out_num(ctx, (double)end);
}

int flat_native_file_setpos(FlatNativeCtx *ctx) {
    char inner_buf[4096];
    char *left;
    char *right;
    uint32_t fid;
    FILE *fp;
    FlatValue n;

    if (!ctx->io_status || !ctx->inner) {
        return file_fail(ctx, "file.setpos");
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &left, &right) != 0) {
        return file_fail(ctx, "file.setpos.args");
    }
    if (require_fp(ctx, left, &fid, "file.setpos.id") != 0) {
        return -1;
    }
    if (parse_expr(ctx, right, &n) != 0 || n.tag != VAL_NUMBER) {
        return file_fail(ctx, "file.setpos.args");
    }
    fp = flat_file_fp(ctx->file_tbl, fid);
    if (!fp) {
        return file_fail(ctx, "file.setpos.id");
    }
    {
        long off = (long)flat_number_as_f64(&n);
        int whence = off < 0 ? SEEK_END : SEEK_SET;
        if (fseek(fp, off, whence) != 0) {
            return file_fail(ctx, "file.setpos.io");
        }
    }
    *ctx->io_status = FLAT_IO_OK;
    return 0;
}

int flat_native_file_setposrel(FlatNativeCtx *ctx) {
    char inner_buf[4096];
    char *left;
    char *right;
    uint32_t fid;
    FILE *fp;
    FlatValue n;

    if (!ctx->io_status || !ctx->inner) {
        return file_fail(ctx, "file.setposrel");
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &left, &right) != 0) {
        return file_fail(ctx, "file.setposrel.args");
    }
    if (require_fp(ctx, left, &fid, "file.setposrel.id") != 0) {
        return -1;
    }
    if (parse_expr(ctx, right, &n) != 0 || n.tag != VAL_NUMBER) {
        return file_fail(ctx, "file.setposrel.args");
    }
    fp = flat_file_fp(ctx->file_tbl, fid);
    if (!fp || fseek(fp, (long)flat_number_as_f64(&n), SEEK_CUR) != 0) {
        return file_fail(ctx, fp ? "file.setposrel.io" : "file.setposrel.id");
    }
    *ctx->io_status = FLAT_IO_OK;
    return 0;
}

int flat_native_file_iseof(FlatNativeCtx *ctx) {
    uint32_t fid;
    FILE *fp;

    if (!ctx->out || !ctx->io_status) {
        return file_fail(ctx, "file.iseof");
    }
    if (require_fp(ctx, ctx->inner, &fid, "file.iseof.id") != 0) {
        return -1;
    }
    fp = flat_file_fp(ctx->file_tbl, fid);
    if (!fp) {
        return file_fail(ctx, "file.iseof.id");
    }
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_bool(ctx->out, feof(fp) ? 1 : 0);
}

int flat_native_file_sep(FlatNativeCtx *ctx) {
    char sep[2];
    if (!ctx || !ctx->out) {
        return file_fail(ctx, "file.sep");
    }
    sep[0] = flat_path_sep();
    sep[1] = '\0';
    if (ctx->io_status) {
        *ctx->io_status = FLAT_IO_OK;
    }
    return flat_value_set_str_inline(ctx->out, sep, 1);
}

int flat_native_file_truncate(FlatNativeCtx *ctx) {
    char inner_buf[4096];
    char *left;
    char *right;
    uint32_t fid;
    FlatValue n;
    double len_f;

    if (!ctx->io_status || !ctx->inner) {
        return file_fail(ctx, "file.truncate");
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &left, &right) != 0) {
        return file_fail(ctx, "file.truncate.args");
    }
    if (require_fp(ctx, left, &fid, "file.truncate.id") != 0) {
        return -1;
    }
    if (parse_expr(ctx, right, &n) != 0 || n.tag != VAL_NUMBER) {
        return file_fail(ctx, "file.truncate.args");
    }
    len_f = flat_number_as_f64(&n);
    if (len_f < 0.0 || len_f != (double)(uint64_t)len_f) {
        return file_fail(ctx, "file.truncate.range");
    }
    if (flat_file_truncate(ctx->file_tbl, fid, (uint64_t)len_f) != 0) {
        return file_fail(ctx, "file.truncate.io");
    }
    *ctx->io_status = FLAT_IO_OK;
    return 0;
}

int flat_native_file_close(FlatNativeCtx *ctx) {
    uint32_t fid;

    if (!ctx->io_status || !ctx->file_tbl) {
        return file_fail(ctx, "file.close");
    }
    if (require_fp(ctx, ctx->inner, &fid, "file.close.id") != 0) {
        return -1;
    }
    if (ctx->csv && flat_csv_fp_busy(ctx->csv, fid)) {
        if (ctx->io_status) {
            *ctx->io_status = FLAT_IO_ERR;
        }
        snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", "file.close.busy");
        return -1;
    }
    if (ctx->io_ctx) {
        flat_io_ctx_file_closed(ctx->io_ctx, fid);
    }
    if (flat_file_close(ctx->file_tbl, fid) != 0) {
        return file_fail(ctx, "file.close.io");
    }
    *ctx->io_status = FLAT_IO_OK;
    return 0;
}
