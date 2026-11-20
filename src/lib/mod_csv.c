#include "flat_csv_runtime.h"
#include "flat_file.h"
#include "flat_native_mod.h"
#include "flat_native_util.h"
#include "flat_number.h"
#include "flat_value.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int csv_fail(FlatNativeCtx *ctx, const char *reason) {
    if (ctx && ctx->io_status) {
        *ctx->io_status = FLAT_IO_ERR;
    }
    if (ctx && reason && reason != ctx->panic_detail) {
        snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", reason);
    }
    return -1;
}

static int split_variadic_args(char *inner, char *parts[], int max_parts, int *out_count) {
    int depth = 0;
    int n = 0;
    char *start;
    char quote = 0;
    char *p;

    if (!inner || !parts || !out_count || max_parts <= 0) {
        return -1;
    }
    start = flat_native_trim(inner);
    if (*start == '\0') {
        return -1;
    }
    parts[n++] = start;
    for (p = start; *p; p++) {
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
            if (n >= max_parts) {
                return -1;
            }
            *p = '\0';
            parts[n++] = flat_native_trim(p + 1);
        }
    }
    *out_count = n;
    return 0;
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

static int require_fp(FlatNativeCtx *ctx, const char *expr, uint32_t *out_id) {
    FlatValue v;
    if (flat_native_parse_expr(ctx, expr, &v) != 0) {
        return -1;
    }
    if (!ctx->file_tbl || !flat_value_get_file_handle(&v, ctx->file_tbl, out_id)) {
        return -1;
    }
    return 0;
}

static int require_sess_id(FlatNativeCtx *ctx, const char *expr, uint32_t *out_id) {
    FlatValue v;
    double d;
    if (flat_native_parse_expr(ctx, expr, &v) != 0) {
        return -1;
    }
    if (v.tag != VAL_NUMBER) {
        return -1;
    }
    d = flat_number_as_f64(&v);
    if (d != d || d < 1.0 || floor(d) != d) {
        return -1;
    }
    *out_id = (uint32_t)d;
    return 0;
}

static int require_int(FlatNativeCtx *ctx, const char *expr, int *out) {
    FlatValue v;
    double d;
    if (flat_native_parse_expr(ctx, expr, &v) != 0 || v.tag != VAL_NUMBER) {
        return -1;
    }
    d = flat_number_as_f64(&v);
    if (d != d || floor(d) != d) {
        return -1;
    }
    *out = (int)d;
    return 0;
}

static int csv_out_num(FlatNativeCtx *ctx, double n) {
    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_number(ctx->out, n);
}

static int csv_out_bool(FlatNativeCtx *ctx, int b) {
    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_bool(ctx->out, b);
}

static FlatCsvRuntime *rt(FlatNativeCtx *ctx) {
    return ctx ? ctx->csv : NULL;
}

int flat_native_csv_reader(FlatNativeCtx *ctx) {
    uint32_t fp;
    uint32_t rid;
    if (!ctx || !ctx->file_tbl) {
        return csv_fail(ctx, "csv.reader");
    }
    if (require_fp(ctx, ctx->inner, &fp) != 0) {
        return csv_fail(ctx, "csv.reader");
    }
    ctx->panic_detail[0] = '\0';
    rid = flat_csv_reader_open(rt(ctx), ctx->file_tbl, fp, ctx->panic_detail, sizeof ctx->panic_detail);
    if (rid == 0) {
        return csv_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "csv.reader");
    }
    return csv_out_num(ctx, (double)rid);
}

int flat_native_csv_writer(FlatNativeCtx *ctx) {
    uint32_t fp;
    uint32_t wid;
    if (!ctx || !ctx->file_tbl) {
        return csv_fail(ctx, "csv.writer");
    }
    if (require_fp(ctx, ctx->inner, &fp) != 0) {
        return csv_fail(ctx, "csv.writer");
    }
    ctx->panic_detail[0] = '\0';
    wid = flat_csv_writer_open(rt(ctx), ctx->file_tbl, fp, ctx->panic_detail, sizeof ctx->panic_detail);
    if (wid == 0) {
        return csv_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "csv.writer");
    }
    return csv_out_num(ctx, (double)wid);
}

int flat_native_csv_close(FlatNativeCtx *ctx) {
    uint32_t sid;
    if (!ctx) {
        return csv_fail(ctx, "csv.close");
    }
    if (require_sess_id(ctx, ctx->inner, &sid) != 0) {
        return csv_fail(ctx, "csv.close");
    }
    ctx->panic_detail[0] = '\0';
    if (flat_csv_close(rt(ctx), sid, ctx->file_tbl, ctx->panic_detail, sizeof ctx->panic_detail) != 0) {
        return csv_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "csv.close");
    }
    *ctx->io_status = FLAT_IO_OK;
    return 0;
}

int flat_native_csv_reset(FlatNativeCtx *ctx) {
    uint32_t rid, root; int x, y, w, h, kind, cursor;
    if (require_sess_id(ctx, ctx->inner, &rid) != 0 ||
        flat_csv_view_resolve(rt(ctx), rid, &root, &x, &y, &w, &h, &kind, &cursor) != 0 ||
        kind != FLAT_CSV_SESS_READER) return csv_fail(ctx, "csv.reset");
    if (w >= 0) {
        if (flat_csv_view_set_cursor(rt(ctx), rid, 0) != 0) return csv_fail(ctx, "csv.reset");
    } else if (flat_csv_reader_reset(rt(ctx), ctx->file_tbl, root) != 0) return csv_fail(ctx, "csv.reset");
    *ctx->io_status = FLAT_IO_OK;
    return 0;
}

int flat_native_csv_is_eof(FlatNativeCtx *ctx) {
    uint32_t rid;
    int eof;
    if (require_sess_id(ctx, ctx->inner, &rid) != 0) {
        return csv_fail(ctx, "csv.isEof");
    }
    if (flat_csv_reader_is_eof(rt(ctx), rid, &eof) != 0) {
        return csv_fail(ctx, "csv.isEof");
    }
    return csv_out_bool(ctx, eof);
}

int flat_native_csv_rows(FlatNativeCtx *ctx) {
    uint32_t rid, root; int n, x, y, w, h, kind, cursor;
    if (require_sess_id(ctx, ctx->inner, &rid) != 0) return csv_fail(ctx, "csv.rows");
    if (flat_csv_view_resolve(rt(ctx), rid, &root, &x, &y, &w, &h, &kind, &cursor) != 0)
        return csv_fail(ctx, "csv.rows");
    if (w >= 0) return csv_out_num(ctx, (double)h);
    if (flat_csv_reader_rows(rt(ctx), ctx->file_tbl, root, &n) != 0) return csv_fail(ctx, "csv.rows");
    return csv_out_num(ctx, (double)n);
}

int flat_native_csv_cols(FlatNativeCtx *ctx) {
    uint32_t rid, root; int n, x, y, w, h, kind, cursor;
    if (require_sess_id(ctx, ctx->inner, &rid) != 0) return csv_fail(ctx, "csv.cols");
    if (flat_csv_view_resolve(rt(ctx), rid, &root, &x, &y, &w, &h, &kind, &cursor) != 0)
        return csv_fail(ctx, "csv.cols");
    if (w >= 0) return csv_out_num(ctx, (double)w);
    if (flat_csv_reader_cols(rt(ctx), ctx->file_tbl, root, &n) != 0) return csv_fail(ctx, "csv.cols");
    return csv_out_num(ctx, (double)n);
}

int flat_native_csv_get_row(FlatNativeCtx *ctx) {
    uint32_t rid;
    int n;
    if (require_sess_id(ctx, ctx->inner, &rid) != 0) {
        return csv_fail(ctx, "csv.getRow");
    }
    if (flat_csv_reader_get_row(rt(ctx), rid, &n) != 0) {
        return csv_fail(ctx, "csv.getRow");
    }
    return csv_out_num(ctx, (double)n);
}

int flat_native_csv_get_col(FlatNativeCtx *ctx) {
    uint32_t rid;
    int n;
    if (require_sess_id(ctx, ctx->inner, &rid) != 0) {
        return csv_fail(ctx, "csv.getCol");
    }
    if (flat_csv_reader_get_col(rt(ctx), rid, &n) != 0) {
        return csv_fail(ctx, "csv.getCol");
    }
    return csv_out_num(ctx, (double)n);
}

int flat_native_csv_set_row(FlatNativeCtx *ctx) {
    char buf[512];
    char *left;
    char *right;
    uint32_t rid;
    int row;
    snprintf(buf, sizeof buf, "%s", ctx->inner);
    if (split_two_args(buf, &left, &right) != 0) {
        return csv_fail(ctx, "csv.setRow");
    }
    if (require_sess_id(ctx, left, &rid) != 0 || require_int(ctx, right, &row) != 0) {
        return csv_fail(ctx, "csv.setRow");
    }
    ctx->panic_detail[0] = '\0';
    if (flat_csv_reader_set_row(rt(ctx), ctx->file_tbl, rid, row, ctx->panic_detail,
                                sizeof ctx->panic_detail) != 0) {
        return csv_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "csv.setRow");
    }
    *ctx->io_status = FLAT_IO_OK;
    return 0;
}

int flat_native_csv_next(FlatNativeCtx *ctx) {
    uint32_t rid, root, list_id; int x, y, w, h, kind, cursor, is_obj;
    if (require_sess_id(ctx, ctx->inner, &rid) != 0) return csv_fail(ctx, "csv.next");
    if (flat_csv_view_resolve(rt(ctx), rid, &root, &x, &y, &w, &h, &kind, &cursor) != 0 ||
        kind != FLAT_CSV_SESS_READER) return csv_fail(ctx, "csv.next");
    if (w >= 0) {
        if (cursor >= h) return csv_fail(ctx, "csv.next");
        ctx->panic_detail[0] = '\0';
        if (flat_csv_reader_get_cells(rt(ctx), ctx->file_tbl, root, x, y + cursor, w, 1, 0,
                                      ctx->coll, ctx->pool, &list_id, &is_obj,
                                      ctx->panic_detail, sizeof ctx->panic_detail) != 0 ||
            flat_csv_view_set_cursor(rt(ctx), rid, cursor + 1) != 0)
            return csv_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "csv.next");
        *ctx->io_status = FLAT_IO_OK;
        return flat_value_set_list(ctx->out, list_id);
    }
    ctx->panic_detail[0] = '\0';
    if (flat_csv_reader_next(rt(ctx), ctx->file_tbl, root, ctx->coll, ctx->pool, &list_id,
                             ctx->panic_detail, sizeof ctx->panic_detail) != 0)
        return csv_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "csv.next");
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_list(ctx->out, list_id);
}

int flat_native_csv_next_cells(FlatNativeCtx *ctx) {
    char buf[512];
    char *left;
    char *right;
    uint32_t rid;
    int len;
    uint32_t list_id;
    snprintf(buf, sizeof buf, "%s", ctx->inner);
    if (split_two_args(buf, &left, &right) != 0) {
        return csv_fail(ctx, "csv.nextCells");
    }
    if (require_sess_id(ctx, left, &rid) != 0 || require_int(ctx, right, &len) != 0) {
        return csv_fail(ctx, "csv.nextCells");
    }
    ctx->panic_detail[0] = '\0';
    if (flat_csv_reader_next_cells(rt(ctx), ctx->file_tbl, rid, len, ctx->coll, ctx->pool,
                                   &list_id, ctx->panic_detail, sizeof ctx->panic_detail) != 0) {
        return csv_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "csv.nextCells");
    }
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_list(ctx->out, list_id);
}

static int format_set_string(FlatNativeCtx *ctx, int kind) {
    char buf[512];
    char *left;
    char *right;
    char str[256];
    uint32_t sid;
    FlatValue v;
    snprintf(buf, sizeof buf, "%s", ctx->inner);
    if (split_two_args(buf, &left, &right) != 0) {
        return csv_fail(ctx, "csv.set");
    }
    if (require_sess_id(ctx, left, &sid) != 0) {
        return csv_fail(ctx, "csv.set");
    }
    if (flat_native_parse_expr(ctx, right, &v) != 0 || flat_value_get_cstr(&v, ctx->pool, str, sizeof str) != 0) {
        return csv_fail(ctx, "csv.set");
    }
    ctx->panic_detail[0] = '\0';
    if (kind == 0) {
        if (flat_csv_format_set_separator(rt(ctx), sid, str, ctx->panic_detail,
                                          sizeof ctx->panic_detail) != 0) {
            return csv_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "csv.setSeparator");
        }
    } else if (kind == 1) {
        if (flat_csv_format_set_line_endings(rt(ctx), sid, str, ctx->panic_detail,
                                             sizeof ctx->panic_detail) != 0) {
            return csv_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "csv.setLineEndings");
        }
    } else if (kind == 2) {
        if (flat_csv_format_set_encoding(rt(ctx), sid, str, ctx->panic_detail,
                                         sizeof ctx->panic_detail) != 0) {
            return csv_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "csv.setEncoding");
        }
    } else {
        if (flat_csv_format_set_quote(rt(ctx), sid, str, ctx->panic_detail,
                                      sizeof ctx->panic_detail) != 0) {
            return csv_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "csv.setQuote");
        }
    }
    *ctx->io_status = FLAT_IO_OK;
    return 0;
}

int flat_native_csv_set_separator(FlatNativeCtx *ctx) {
    return format_set_string(ctx, 0);
}
int flat_native_csv_set_line_endings(FlatNativeCtx *ctx) {
    return format_set_string(ctx, 1);
}
int flat_native_csv_set_encoding(FlatNativeCtx *ctx) {
    return format_set_string(ctx, 2);
}
int flat_native_csv_set_quote(FlatNativeCtx *ctx) {
    return format_set_string(ctx, 3);
}

int flat_native_csv_write_rows(FlatNativeCtx *ctx) {
    char buf[512];
    char *left;
    char *right;
    uint32_t wid;
    FlatValue rows;
    snprintf(buf, sizeof buf, "%s", ctx->inner);
    if (split_two_args(buf, &left, &right) != 0) {
        return csv_fail(ctx, "csv.writeRows");
    }
    if (require_sess_id(ctx, left, &wid) != 0 || flat_native_parse_expr(ctx, right, &rows) != 0) {
        return csv_fail(ctx, "csv.writeRows");
    }
    ctx->panic_detail[0] = '\0';
    if (flat_csv_writer_write_rows(rt(ctx), wid, &rows, ctx->coll, ctx->pool, ctx->panic_detail,
                                   sizeof ctx->panic_detail) != 0) {
        return csv_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "csv.writeRows");
    }
    *ctx->io_status = FLAT_IO_OK;
    return 0;
}

int flat_native_csv_write_cols(FlatNativeCtx *ctx) {
    char buf[512];
    char *left;
    char *right;
    uint32_t wid;
    FlatValue cols;
    snprintf(buf, sizeof buf, "%s", ctx->inner);
    if (split_two_args(buf, &left, &right) != 0) {
        return csv_fail(ctx, "csv.writeCols");
    }
    if (require_sess_id(ctx, left, &wid) != 0 || flat_native_parse_expr(ctx, right, &cols) != 0) {
        return csv_fail(ctx, "csv.writeCols");
    }
    ctx->panic_detail[0] = '\0';
    if (flat_csv_writer_write_cols(rt(ctx), wid, &cols, ctx->coll, ctx->pool, ctx->panic_detail,
                                   sizeof ctx->panic_detail) != 0) {
        return csv_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "csv.writeCols");
    }
    *ctx->io_status = FLAT_IO_OK;
    return 0;
}

int flat_native_csv_flush(FlatNativeCtx *ctx) {
    uint32_t wid;
    if (require_sess_id(ctx, ctx->inner, &wid) != 0) {
        return csv_fail(ctx, "csv.flush");
    }
    ctx->panic_detail[0] = '\0';
    if (flat_csv_writer_flush(rt(ctx), wid, ctx->file_tbl, ctx->panic_detail,
                              sizeof ctx->panic_detail) != 0) {
        return csv_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "csv.flush");
    }
    *ctx->io_status = FLAT_IO_OK;
    return 0;
}

int flat_native_csv_get_cells(FlatNativeCtx *ctx) {
    char buf[1024];
    char *parts[8];
    int argc;
    uint32_t rid;
    int x0;
    int y0;
    int xLen;
    int yLen;
    int as_object = 0;
    uint32_t coll_id;
    int is_object;
    FlatValue mode;

    snprintf(buf, sizeof buf, "%s", ctx->inner);
    if (split_variadic_args(buf, parts, 8, &argc) != 0 || argc < 5 || argc > 6) {
        return csv_fail(ctx, "csv.getCells");
    }
    {
        uint32_t root; int vx, vy, vw, vh, kind, cursor;
        if (require_sess_id(ctx, parts[0], &rid) != 0 || require_int(ctx, parts[1], &x0) != 0 ||
            require_int(ctx, parts[2], &y0) != 0 || require_int(ctx, parts[3], &xLen) != 0 ||
            require_int(ctx, parts[4], &yLen) != 0 ||
            flat_csv_view_resolve(rt(ctx), rid, &root, &vx, &vy, &vw, &vh, &kind, &cursor) != 0)
            return csv_fail(ctx, "csv.getCells");
        if (kind != FLAT_CSV_SESS_READER) return csv_fail(ctx, "csv.id.type");
        if (vw >= 0 && (x0 < 0 || y0 < 0 || xLen <= 0 || yLen <= 0 ||
                        (long long)x0 + xLen > vw || (long long)y0 + yLen > vh))
            return csv_fail(ctx, "csv.view.bounds");
        x0 += vx; y0 += vy; rid = root;
    }
    if (argc == 6) {
        char mode_buf[32];
        if (flat_native_parse_expr(ctx, parts[5], &mode) != 0 ||
            flat_value_get_cstr(&mode, ctx->pool, mode_buf, sizeof mode_buf) != 0) {
            return csv_fail(ctx, "csv.getCells");
        }
        if (strcmp(mode_buf, "obj") == 0) {
            as_object = 1;
        } else if (strcmp(mode_buf, "list") != 0) {
            return csv_fail(ctx, "csv.getCells");
        }
    }
    ctx->panic_detail[0] = '\0';
    if (flat_csv_reader_get_cells(rt(ctx), ctx->file_tbl, rid, x0, y0, xLen, yLen, as_object,
                                  ctx->coll, ctx->pool, &coll_id, &is_object, ctx->panic_detail,
                                  sizeof ctx->panic_detail) != 0) {
        return csv_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "csv.getCells");
    }
    *ctx->io_status = FLAT_IO_OK;
    if (is_object) {
        return flat_value_set_object(ctx->out, coll_id);
    }
    return flat_value_set_list(ctx->out, coll_id);
}
int flat_native_csv_view(FlatNativeCtx *ctx) {
    char buf[512];
    char *parts[5];
    int argc, x, y, width, height;
    uint32_t parent, id;
    if (!ctx) return csv_fail(ctx, "csv.view");
    snprintf(buf, sizeof buf, "%s", ctx->inner);
    if (split_variadic_args(buf, parts, 5, &argc) != 0 || argc != 5 ||
        require_sess_id(ctx, parts[0], &parent) != 0 ||
        require_int(ctx, parts[1], &x) != 0 || require_int(ctx, parts[2], &y) != 0 ||
        require_int(ctx, parts[3], &width) != 0 || require_int(ctx, parts[4], &height) != 0)
        return csv_fail(ctx, "csv.view");
    ctx->panic_detail[0] = '\0';
    if (flat_csv_view_create(rt(ctx), ctx->file_tbl, parent, x, y, width, height, &id,
                             ctx->panic_detail, sizeof ctx->panic_detail) != 0)
        return csv_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "csv.view");
    return csv_out_num(ctx, (double)id);
}

int flat_native_csv_close_view(FlatNativeCtx *ctx) {
    uint32_t id;
    if (!ctx || require_sess_id(ctx, ctx->inner, &id) != 0)
        return csv_fail(ctx, "csv.closeView");
    ctx->panic_detail[0] = '\0';
    if (flat_csv_view_close(rt(ctx), id, ctx->panic_detail, sizeof ctx->panic_detail) != 0)
        return csv_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "csv.closeView");
    *ctx->io_status = FLAT_IO_OK;
    return 0;
}


int flat_native_csv_write_view_row(FlatNativeCtx *ctx) {
    char buf[1024];
    char *parts[3];
    int argc, y;
    uint32_t id;
    FlatValue row;
    if (!ctx) return csv_fail(ctx, "csv.writeRow");
    snprintf(buf, sizeof buf, "%s", ctx->inner);
    if (split_variadic_args(buf, parts, 3, &argc) != 0 || argc != 3 ||
        require_sess_id(ctx, parts[0], &id) != 0 || require_int(ctx, parts[1], &y) != 0 ||
        flat_native_parse_expr(ctx, parts[2], &row) != 0)
        return csv_fail(ctx, "csv.writeRow");
    ctx->panic_detail[0] = '\0';
    if (flat_csv_writer_write_view_row(rt(ctx), id, y, &row, ctx->coll, ctx->pool,
                                       ctx->panic_detail, sizeof ctx->panic_detail) != 0)
        return csv_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "csv.writeRow");
    *ctx->io_status = FLAT_IO_OK;
    return 0;
}

int flat_native_csv_set_cell(FlatNativeCtx *ctx) {
    char buf[1024], value[512]; char *parts[4]; int argc, x, y; uint32_t id; FlatValue v;
    if (!ctx) return csv_fail(ctx, "csv.set");
    snprintf(buf, sizeof buf, "%s", ctx->inner);
    if (split_variadic_args(buf, parts, 4, &argc) != 0 || argc != 4 ||
        require_sess_id(ctx, parts[0], &id) != 0 || require_int(ctx, parts[1], &x) != 0 ||
        require_int(ctx, parts[2], &y) != 0 || flat_native_parse_expr(ctx, parts[3], &v) != 0 ||
        flat_value_get_cstr(&v, ctx->pool, value, sizeof value) != 0)
        return csv_fail(ctx, "csv.set");
    ctx->panic_detail[0] = '\0';
    if (flat_csv_writer_set_cell(rt(ctx), id, x, y, value, ctx->panic_detail,
                                 sizeof ctx->panic_detail) != 0)
        return csv_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "csv.set");
    *ctx->io_status = FLAT_IO_OK;
    return 0;
}

int flat_native_csv_get_view_row(FlatNativeCtx *ctx) {
    char buf[512], *a, *b; uint32_t id, root, list_id; int y, x, oy, w, h, kind, cursor, is_obj;
    if (!ctx) return csv_fail(ctx, "csv.row");
    snprintf(buf, sizeof buf, "%s", ctx->inner);
    if (split_two_args(buf, &a, &b) != 0 || require_sess_id(ctx, a, &id) != 0 ||
        require_int(ctx, b, &y) != 0 ||
        flat_csv_view_resolve(rt(ctx), id, &root, &x, &oy, &w, &h, &kind, &cursor) != 0 ||
        kind != FLAT_CSV_SESS_READER || w < 0 || y < 0 || y >= h)
        return csv_fail(ctx, "csv.row");
    ctx->panic_detail[0] = '\0';
    if (flat_csv_reader_get_cells(rt(ctx), ctx->file_tbl, root, x, oy + y, w, 1, 0,
                                  ctx->coll, ctx->pool, &list_id, &is_obj, ctx->panic_detail,
                                  sizeof ctx->panic_detail) != 0)
        return csv_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "csv.row");
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_list(ctx->out, list_id);
}

int flat_native_csv_get_cell(FlatNativeCtx *ctx) {
    char buf[512], *parts[3]; int argc, x, y; uint32_t id, root, list_id;
    int vx, vy, w, h, kind, cursor, is_obj; FlatValue cell;
    if (!ctx) return csv_fail(ctx, "csv.get");
    snprintf(buf, sizeof buf, "%s", ctx->inner);
    if (split_variadic_args(buf, parts, 3, &argc) != 0 || argc != 3 ||
        require_sess_id(ctx, parts[0], &id) != 0 || require_int(ctx, parts[1], &x) != 0 ||
        require_int(ctx, parts[2], &y) != 0 ||
        flat_csv_view_resolve(rt(ctx), id, &root, &vx, &vy, &w, &h, &kind, &cursor) != 0 ||
        kind != FLAT_CSV_SESS_READER || w < 0 || x < 0 || y < 0 || x >= w || y >= h)
        return csv_fail(ctx, "csv.get");
    ctx->panic_detail[0] = '\0';
    if (flat_csv_reader_get_cells(rt(ctx), ctx->file_tbl, root, vx + x, vy + y, 1, 1, 0,
                                  ctx->coll, ctx->pool, &list_id, &is_obj, ctx->panic_detail,
                                  sizeof ctx->panic_detail) != 0 ||
        flat_list_get(ctx->coll, list_id, 0, &cell) != 0)
        return csv_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "csv.get");
    *ctx->io_status = FLAT_IO_OK;
    *ctx->out = cell;
    flat_value_retain(ctx->coll, ctx->pool, ctx->out);
    return 0;
}

static int csv_make_record_object(FlatNativeCtx *ctx, uint32_t id, uint32_t list_id,
                                  uint32_t *out_obj) {
    int count = 0, configured, total, i;
    char keys[FLAT_OBJ_MAX_KEYS][FLAT_OBJ_KEY_MAX + 1];
    FlatValue values[FLAT_OBJ_MAX_KEYS];
    if (flat_list_len(ctx->coll, list_id, &count) != 0 || count < 1 || count > FLAT_OBJ_MAX_KEYS)
        return -1;
    configured = flat_csv_obj_headers_count(rt(ctx), id);
    if (configured && count > configured) return -1;
    total = configured ? configured : count;
    for (i = 0; i < total; i++) {
        if (configured) {
            if (flat_csv_obj_headers_get(rt(ctx), id, i, keys[i], sizeof keys[i]) != 0) return -1;
        } else {
            snprintf(keys[i], sizeof keys[i], "col%d", i);
        }
        if (i < count) {
            if (flat_list_get(ctx->coll, list_id, i, &values[i]) != 0) return -1;
        } else if (flat_value_set_str_inline(&values[i], "", 0) != 0) return -1;
    }
    return flat_obj_build(ctx->coll, ctx->pool, keys, values, total, out_obj);
}

int flat_native_csv_set_obj_headers(FlatNativeCtx *ctx) {
    char buf[2048], *left, *right;
    uint32_t id;
    FlatValue list;
    const char *unused = NULL;
    char **headers = NULL;
    const char **header_ptrs = NULL;
    int count = 0, i, rc = -1;
    (void)unused;
    if (!ctx) return csv_fail(ctx, "csv.setObjHeaders");
    snprintf(buf, sizeof buf, "%s", ctx->inner);
    if (split_two_args(buf, &left, &right) != 0 || require_sess_id(ctx, left, &id) != 0 ||
        flat_native_parse_expr(ctx, right, &list) != 0 || list.tag != VAL_LIST ||
        flat_list_len(ctx->coll, list.u.coll_id, &count) != 0 || count < 1 || count > FLAT_OBJ_MAX_KEYS)
        return csv_fail(ctx, "csv.setObjHeaders");
    headers = (char **)calloc((size_t)count, sizeof(char *));
    header_ptrs = (const char **)calloc((size_t)count, sizeof(char *));
    if (!headers || !header_ptrs) goto done;
    for (i = 0; i < count; i++) {
        FlatValue item;
        char key[FLAT_OBJ_KEY_MAX + 1];
        size_t len;
        if (flat_list_get(ctx->coll, list.u.coll_id, i, &item) != 0 || item.tag == VAL_NULL ||
            flat_value_get_cstr(&item, ctx->pool, key, sizeof key) != 0) goto done;
        len = strlen(key);
        if (len == 0) { snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "csv.objHeaders.empty"); goto done; }
        if (len > FLAT_OBJ_KEY_MAX) { snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "csv.objHeaders.length"); goto done; }
        headers[i] = (char *)malloc(len + 1);
        if (!headers[i]) goto done;
        memcpy(headers[i], key, len + 1);
        header_ptrs[i] = headers[i];
    }
    if (flat_csv_obj_headers_set(rt(ctx), ctx->file_tbl, id, header_ptrs, count,
                                 ctx->panic_detail, sizeof ctx->panic_detail) == 0) rc = 0;
done:
    for (i = 0; i < count; i++) free(headers ? headers[i] : NULL);
    free(headers); free(header_ptrs);
    if (rc != 0) return csv_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "csv.setObjHeaders");
    *ctx->io_status = FLAT_IO_OK;
    return 0;
}

int flat_native_csv_next_record(FlatNativeCtx *ctx) {
    uint32_t id, root, list_id, obj_id;
    int x, y, w, h, kind, cursor, is_obj;
    if (!ctx || require_sess_id(ctx, ctx->inner, &id) != 0 ||
        flat_csv_view_resolve(rt(ctx), id, &root, &x, &y, &w, &h, &kind, &cursor) != 0 ||
        kind != FLAT_CSV_SESS_READER) return csv_fail(ctx, "csv.nextRecord");
    ctx->panic_detail[0] = '\0';
    if (w >= 0) {
        if (cursor >= h) return csv_fail(ctx, "csv.nextRecord");
        if (flat_csv_reader_get_cells(rt(ctx), ctx->file_tbl, root, x, y + cursor,
                w, 1, 0, ctx->coll, ctx->pool, &list_id, &is_obj,
                ctx->panic_detail, sizeof ctx->panic_detail) != 0)
            return csv_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "csv.nextRecord.cells");
        if (flat_csv_view_set_cursor(rt(ctx), id, cursor + 1) != 0)
            return csv_fail(ctx, "csv.nextRecord.cursor");
    } else if (flat_csv_reader_next(rt(ctx), ctx->file_tbl, root, ctx->coll, ctx->pool,
                                    &list_id, ctx->panic_detail, sizeof ctx->panic_detail) != 0) {
        return csv_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "csv.nextRecord");
    }
    if (csv_make_record_object(ctx, id, list_id, &obj_id) != 0)
        return csv_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "csv.nextRecord.object");
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_object(ctx->out, obj_id);
}

int flat_native_csv_get_collection(FlatNativeCtx *ctx) {
    char buf[512], *parts[3];
    int argc, offset, limit, x = 0, y = 0, width = 0, height = 0, kind, cursor;
    uint32_t id, root, raw_id, out_id;
    int is_obj, rows, actual, configured, i;
    FlatValue vals[FLAT_OBJ_MAX_KEYS];
    char keys[FLAT_OBJ_MAX_KEYS][FLAT_OBJ_KEY_MAX + 1];
    if (!ctx) return csv_fail(ctx, "csv.getCollection");
    snprintf(buf, sizeof buf, "%s", ctx->inner);
    if (split_variadic_args(buf, parts, 3, &argc) != 0 || argc != 3 ||
        require_sess_id(ctx, parts[0], &id) != 0 || require_int(ctx, parts[1], &offset) != 0 ||
        require_int(ctx, parts[2], &limit) != 0 || offset < 0 || limit < 1 ||
        flat_csv_view_resolve(rt(ctx), id, &root, &x, &y, &width, &height, &kind, &cursor) != 0 ||
        kind != FLAT_CSV_SESS_READER) return csv_fail(ctx, "csv.getCollection");
    if (width >= 0) { rows = height; }
    else if (flat_csv_reader_rows(rt(ctx), ctx->file_tbl, root, &rows) != 0 ||
             flat_csv_reader_cols_at(rt(ctx), ctx->file_tbl, root, 0, &width) != 0)
        return csv_fail(ctx, "csv.getCollection");
    if (width < 1 || width > FLAT_OBJ_MAX_KEYS || offset > rows)
        return csv_fail(ctx, "csv.getCollection");
    actual = rows - offset;
    if (actual > limit) actual = limit;
    if (width > FLAT_LIST_MAX_ELEMS / (actual > 0 ? actual : 1)) return csv_fail(ctx, "csv.getCollection");
    configured = flat_csv_obj_headers_count(rt(ctx), id);
    if (configured && configured != width) return csv_fail(ctx, "csv.objHeaders.length");
    ctx->panic_detail[0] = '\0';
    if (actual > 0) {
        if (flat_csv_reader_get_cells(rt(ctx), ctx->file_tbl, root, x, y + offset, width, actual, 1,
                ctx->coll, ctx->pool, &raw_id, &is_obj, ctx->panic_detail, sizeof ctx->panic_detail) != 0)
            return csv_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "csv.getCollection");
    }
    for (i = 0; i < width; i++) {
        char old_key[32];
        snprintf(old_key, sizeof old_key, "%d", i);
        if (configured) {
            if (flat_csv_obj_headers_get(rt(ctx), id, i, keys[i], sizeof keys[i]) != 0)
                return csv_fail(ctx, "csv.getCollection");
        } else snprintf(keys[i], sizeof keys[i], "col%d", i);
        if (actual > 0) {
            if (flat_obj_get(ctx->coll, raw_id, old_key, &vals[i]) != 0) return csv_fail(ctx, "csv.getCollection");
        } else {
            uint32_t empty_list;
            if (flat_list_empty(ctx->coll, ctx->pool, &empty_list) != 0 ||
                flat_value_set_list(&vals[i], empty_list) != 0)
                return csv_fail(ctx, "csv.getCollection");
        }
    }
    if (flat_obj_build(ctx->coll, ctx->pool, keys, vals, width, &out_id) != 0)
        return csv_fail(ctx, "csv.getCollection");
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_object(ctx->out, out_id);
}
