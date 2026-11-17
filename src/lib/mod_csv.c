#include "flat_csv_runtime.h"
#include "flat_file.h"
#include "flat_native_mod.h"
#include "flat_native_util.h"
#include "flat_number.h"
#include "flat_value.h"

#include <math.h>
#include <stdio.h>
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

static int parse_expr(FlatNativeCtx *ctx, const char *expr, FlatValue *out) {
    char buf[4096];
    if (!expr || !out) {
        return -1;
    }
    snprintf(buf, sizeof buf, "%s", expr);
    return flat_value_parse_assign_rhs(flat_native_trim(buf), out, ctx->pool, ctx->coll, ctx->vars,
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

static int require_fp(FlatNativeCtx *ctx, const char *expr, uint32_t *out_id) {
    FlatValue v;
    if (parse_expr(ctx, expr, &v) != 0) {
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
    if (parse_expr(ctx, expr, &v) != 0) {
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
    if (parse_expr(ctx, expr, &v) != 0 || v.tag != VAL_NUMBER) {
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
    uint32_t rid;
    if (require_sess_id(ctx, ctx->inner, &rid) != 0) {
        return csv_fail(ctx, "csv.reset");
    }
    if (flat_csv_reader_reset(rt(ctx), ctx->file_tbl, rid) != 0) {
        return csv_fail(ctx, "csv.reset");
    }
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
    uint32_t rid;
    int n;
    if (require_sess_id(ctx, ctx->inner, &rid) != 0) {
        return csv_fail(ctx, "csv.rows");
    }
    if (flat_csv_reader_rows(rt(ctx), ctx->file_tbl, rid, &n) != 0) {
        return csv_fail(ctx, "csv.rows");
    }
    return csv_out_num(ctx, (double)n);
}

int flat_native_csv_cols(FlatNativeCtx *ctx) {
    uint32_t rid;
    int n;
    if (require_sess_id(ctx, ctx->inner, &rid) != 0) {
        return csv_fail(ctx, "csv.cols");
    }
    if (flat_csv_reader_cols(rt(ctx), ctx->file_tbl, rid, &n) != 0) {
        return csv_fail(ctx, "csv.cols");
    }
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
    uint32_t rid;
    uint32_t list_id;
    if (require_sess_id(ctx, ctx->inner, &rid) != 0) {
        return csv_fail(ctx, "csv.next");
    }
    ctx->panic_detail[0] = '\0';
    if (flat_csv_reader_next(rt(ctx), ctx->file_tbl, rid, ctx->coll, ctx->pool, &list_id,
                             ctx->panic_detail, sizeof ctx->panic_detail) != 0) {
        return csv_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "csv.next");
    }
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
    if (parse_expr(ctx, right, &v) != 0 || flat_value_get_cstr(&v, ctx->pool, str, sizeof str) != 0) {
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
    if (require_sess_id(ctx, left, &wid) != 0 || parse_expr(ctx, right, &rows) != 0) {
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
    if (require_sess_id(ctx, left, &wid) != 0 || parse_expr(ctx, right, &cols) != 0) {
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