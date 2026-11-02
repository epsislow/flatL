#include "flat_file.h"
#include "flat_io_ctx.h"
#include "flat_native_mod.h"
#include "flat_native_util.h"
#include "flat_value.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static int require_fp(FlatNativeCtx *ctx, const char *expr, uint32_t *out_id) {
    FlatValue v;

    if (parse_expr(ctx, expr, &v) != 0 || v.tag != VAL_FILE) {
        return -1;
    }
    if (!ctx->file_tbl || !flat_file_is_live(ctx->file_tbl, v.u.file_id)) {
        return -1;
    }
    *out_id = v.u.file_id;
    return 0;
}

static int open_path_native(FlatNativeCtx *ctx, FlatFileMode mode) {
    char path[4096];
    uint32_t id;

    if (!ctx->out || !ctx->io_status || !ctx->file_tbl) {
        return -1;
    }
    if (path_from_expr(ctx, ctx->inner, path, sizeof path) != 0) {
        return -1;
    }
    if (flat_file_open(ctx->file_tbl, path, mode, &id) != 0) {
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_file(ctx->out, id);
}

int flat_native_file_read_ctx(FlatNativeCtx *ctx) {
    return open_path_native(ctx, FLAT_FILE_MODE_READ);
}

int flat_native_file_create_ctx(FlatNativeCtx *ctx) {
    return open_path_native(ctx, FLAT_FILE_MODE_CREATE);
}

int flat_native_file_write_ctx(FlatNativeCtx *ctx) {
    return open_path_native(ctx, FLAT_FILE_MODE_WRITE_AT0);
}

int flat_native_file_append_ctx(FlatNativeCtx *ctx) {
    return open_path_native(ctx, FLAT_FILE_MODE_APPEND);
}

int flat_native_file_read_write_ctx(FlatNativeCtx *ctx) {
    return open_path_native(ctx, FLAT_FILE_MODE_RDWR);
}

int flat_native_file_readln(FlatNativeCtx *ctx) {
    char line[4096];
    uint32_t fid;
    FILE *fp;

    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    if (require_fp(ctx, ctx->inner, &fid) != 0) {
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
        return -1;
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &left, &right) == 0) {
        FlatValue n;
        if (parse_expr(ctx, right, &n) != 0 || n.tag != VAL_NUMBER) {
            return -1;
        }
        if (n.u.number < 0) {
            return -1;
        }
        max_read = (size_t)n.u.number;
    } else {
        left = flat_native_trim(inner_buf);
    }
    if (require_fp(ctx, left, &fid) != 0) {
        return -1;
    }
    fp = flat_file_fp(ctx->file_tbl, fid);
    if (!fp) {
        return -1;
    }
    start = ftell(fp);
    if (start < 0) {
        return -1;
    }
    buf = malloc(cap);
    if (!buf) {
        return -1;
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
                return -1;
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
            return -1;
        }
        free(buf);
        *ctx->io_status = FLAT_IO_OK;
        return flat_value_set_str_ref(ctx->out, rid);
    }
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
        return -1;
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &left, &right) != 0) {
        return -1;
    }
    if (require_fp(ctx, left, &fid) != 0) {
        return -1;
    }
    if (parse_expr(ctx, right, &data) != 0) {
        return -1;
    }
    fp = flat_file_fp(ctx->file_tbl, fid);
    if (!fp) {
        return -1;
    }
    mem = open_memstream(&bytes, &blen);
    if (!mem) {
        return -1;
    }
    flat_value_fprint_io(mem, &data, ctx->pool, ctx->coll);
    fclose(mem);
    if (fwrite(bytes, 1, blen, fp) != blen) {
        free(bytes);
        return -1;
    }
    free(bytes);
    *ctx->io_status = FLAT_IO_OK;
    return 0;
}

int flat_native_file_flush(FlatNativeCtx *ctx) {
    uint32_t fid;
    FILE *fp;

    if (!ctx->io_status) {
        return -1;
    }
    if (require_fp(ctx, ctx->inner, &fid) != 0) {
        return -1;
    }
    fp = flat_file_fp(ctx->file_tbl, fid);
    if (!fp || fflush(fp) != 0) {
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    return 0;
}

static int file_out_num(FlatNativeCtx *ctx, double n) {
    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_number(ctx->out, n);
}

int flat_native_file_pos(FlatNativeCtx *ctx) {
    uint32_t fid;
    FILE *fp;
    long p;

    if (require_fp(ctx, ctx->inner, &fid) != 0) {
        return -1;
    }
    fp = flat_file_fp(ctx->file_tbl, fid);
    p = fp ? ftell(fp) : -1;
    if (p < 0) {
        return -1;
    }
    return file_out_num(ctx, (double)p);
}

int flat_native_file_size(FlatNativeCtx *ctx) {
    uint32_t fid;
    FILE *fp;
    long cur;
    long end;

    if (require_fp(ctx, ctx->inner, &fid) != 0) {
        return -1;
    }
    fp = flat_file_fp(ctx->file_tbl, fid);
    if (!fp) {
        return -1;
    }
    cur = ftell(fp);
    if (fseek(fp, 0, SEEK_END) != 0) {
        return -1;
    }
    end = ftell(fp);
    if (cur >= 0 && fseek(fp, cur, SEEK_SET) != 0) {
        return -1;
    }
    if (end < 0) {
        return -1;
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
        return -1;
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &left, &right) != 0) {
        return -1;
    }
    if (require_fp(ctx, left, &fid) != 0) {
        return -1;
    }
    if (parse_expr(ctx, right, &n) != 0 || n.tag != VAL_NUMBER) {
        return -1;
    }
    fp = flat_file_fp(ctx->file_tbl, fid);
    if (!fp || fseek(fp, (long)n.u.number, SEEK_SET) != 0) {
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    return 0;
}

int flat_native_file_iseof(FlatNativeCtx *ctx) {
    uint32_t fid;
    FILE *fp;

    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    if (require_fp(ctx, ctx->inner, &fid) != 0) {
        return -1;
    }
    fp = flat_file_fp(ctx->file_tbl, fid);
    if (!fp) {
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_bool(ctx->out, feof(fp) ? 1 : 0);
}

int flat_native_file_close(FlatNativeCtx *ctx) {
    uint32_t fid;

    if (!ctx->io_status || !ctx->file_tbl) {
        return -1;
    }
    if (require_fp(ctx, ctx->inner, &fid) != 0) {
        return -1;
    }
    if (ctx->io_ctx) {
        flat_io_ctx_file_closed(ctx->io_ctx, fid);
    }
    if (flat_file_close(ctx->file_tbl, fid) != 0) {
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    return 0;
}
