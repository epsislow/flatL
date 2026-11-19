#include "flat_list_shape.h"
#include "flat_native_mod.h"
#include "flat_native_util.h"
#include "flat_number.h"
#include "flat_value.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef int (*MdimBinOpFn)(double a, double b, double *out);

static int mdim_at2(FlatCollectionPool *coll, uint32_t mat_id, int row, int col, double *out);

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

static void mdim_panic(FlatNativeCtx *ctx, const char *code) {
    if (!ctx) {
        return;
    }
    snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", code);
    *ctx->io_status = FLAT_IO_ERR;
}

static int mdim_dims_equal(const FlatListShapeInfo *a, const FlatListShapeInfo *b) {
    if (!a || !b || a->rank != b->rank || a->dims_n != b->dims_n) {
        return 0;
    }
    return memcmp(a->dims, b->dims, (size_t)a->dims_n * sizeof a->dims[0]) == 0;
}

static int walk_leaves_numeric(FlatCollectionPool *coll, uint32_t list_id) {
    int len;
    int i;
    FlatValue el;

    if (!coll || flat_list_len(coll, list_id, &len) != 0) {
        return -1;
    }
    for (i = 0; i < len; i++) {
        if (flat_list_get(coll, list_id, i, &el) != 0) {
            return -1;
        }
        if (el.tag == VAL_LIST) {
            int rc = walk_leaves_numeric(coll, el.u.coll_id);
            if (rc != 0) {
                return rc;
            }
        } else if (el.tag != VAL_NUMBER) {
            return -2;
        }
    }
    return 0;
}

static void mdim_stamp_shape(FlatCollectionPool *coll, uint32_t list_id,
                             const FlatListShapeInfo *info) {
    FlatListShapeInfo st;

    if (!coll || !info) {
        return;
    }
    st = *info;
    st.valid = 1;
    st.rect = 1;
    st.source = FLAT_LIST_SHAPE_SRC_WALK;
    flat_coll_shape_slot_write(coll, list_id, &st);
}

static int mdim_operand_info(FlatNativeCtx *ctx, const FlatValue *list_v, int require_rect,
                             FlatListShapeInfo *info_out) {
    int rc;

    if (!ctx || !list_v || list_v->tag != VAL_LIST || !info_out) {
        return -1;
    }
    if (flat_list_shape_query(ctx->coll, list_v->u.coll_id, info_out) != 0) {
        mdim_panic(ctx, "type.mdim.dim_mismatch");
        return -1;
    }
    if (require_rect && !info_out->rect) {
        mdim_panic(ctx, "type.mdim.not_rect");
        return -1;
    }
    rc = walk_leaves_numeric(ctx->coll, list_v->u.coll_id);
    if (rc == -2) {
        mdim_panic(ctx, "type.mdim.not_numeric");
        return -1;
    }
    if (rc != 0) {
        mdim_panic(ctx, "type.mdim.dim_mismatch");
        return -1;
    }
    return 0;
}

static int mdim_parse_list(FlatNativeCtx *ctx, const char *expr, FlatValue *out) {
    if (flat_native_parse_rhs(ctx, expr, out) != 0 || out->tag != VAL_LIST) {
        mdim_panic(ctx, "type.mdim.dim_mismatch");
        return -1;
    }
    return 0;
}

static int mdim_dup_list(FlatNativeCtx *ctx, uint32_t src_id, uint32_t *out_id) {
    int len;
    int i;
    FlatValue el;
    FlatValue items[FLAT_LIST_MAX_ELEMS];

    if (!ctx || !out_id || flat_list_len(ctx->coll, src_id, &len) != 0) {
        return -1;
    }
    if (len > FLAT_LIST_MAX_ELEMS) {
        return -1;
    }
    for (i = 0; i < len; i++) {
        if (flat_list_get(ctx->coll, src_id, i, &el) != 0) {
            return -1;
        }
        if (el.tag == VAL_LIST) {
            uint32_t sub;
            if (mdim_dup_list(ctx, el.u.coll_id, &sub) != 0) {
                return -1;
            }
            flat_value_set_list(&items[i], sub);
        } else if (el.tag == VAL_NUMBER) {
            items[i] = el;
        } else {
            return -1;
        }
    }
    return flat_list_build(ctx->coll, ctx->pool, items, len, out_id);
}

static int mdim_materialize_list_children(FlatNativeCtx *ctx, FlatValue *items, int len) {
    int i;

    for (i = 0; i < len; i++) {
        uint32_t dup;
        if (items[i].tag != VAL_LIST) {
            continue;
        }
        if (mdim_dup_list(ctx, items[i].u.coll_id, &dup) != 0) {
            return -1;
        }
        flat_value_set_list(&items[i], dup);
    }
    return 0;
}

static int mdim_at1(FlatCollectionPool *coll, uint32_t vec_id, int i, double *out) {
    FlatValue el;

    if (flat_list_get(coll, vec_id, i, &el) != 0 || el.tag != VAL_NUMBER) {
        return -1;
    }
    *out = flat_number_as_f64(&el);
    return 0;
}

static int mdim_map_binary_shaped(FlatNativeCtx *ctx, uint32_t a_id, uint32_t b_id,
                                  const FlatListShapeInfo *info, MdimBinOpFn op,
                                  uint32_t *out_id) {
    int i;
    FlatValue items[FLAT_LIST_MAX_ELEMS];

    if (!info || info->rank == 0 || info->dims_n == 0) {
        return -1;
    }
    if (info->rank == 1) {
        int len = (int)info->dims[0];
        if (len > FLAT_LIST_MAX_ELEMS) {
            return -1;
        }
        for (i = 0; i < len; i++) {
            double da;
            double db;
            double dr;
            int orc;
            if (mdim_at1(ctx->coll, a_id, i, &da) != 0 || mdim_at1(ctx->coll, b_id, i, &db) != 0) {
                mdim_panic(ctx, "type.mdim.not_numeric");
                return -1;
            }
            orc = op(da, db, &dr);
            if (orc == -2) {
                mdim_panic(ctx, "math.mdim.divzero");
                return -1;
            }
            if (orc != 0) {
                return -1;
            }
            flat_number_init_legacy_double(&items[i], dr);
        }
        return flat_list_build(ctx->coll, ctx->pool, items, len, out_id);
    }
    if (info->rank == 2) {
        int rows;
        int cols;
        int r;
        FlatValue probe;
        FlatValue built[FLAT_LIST_MAX_ELEMS];
        if (flat_list_len(ctx->coll, a_id, &rows) != 0 || rows <= 0 || rows > FLAT_LIST_MAX_ELEMS) {
            return -1;
        }
        if (flat_list_get(ctx->coll, a_id, 0, &probe) != 0 || probe.tag != VAL_LIST ||
            flat_list_len(ctx->coll, probe.u.coll_id, &cols) != 0 || cols <= 0 ||
            cols > FLAT_LIST_MAX_ELEMS) {
            return -1;
        }
        for (r = 0; r < rows; r++) {
            FlatValue cells[FLAT_LIST_MAX_ELEMS];
            int c;
            uint32_t row_id;
            uint32_t row_dup;
            memset(cells, 0, sizeof cells);
            for (c = 0; c < cols; c++) {
                double da;
                double db;
                double dr;
                int orc;
                if (mdim_at2(ctx->coll, a_id, r, c, &da) != 0 ||
                    mdim_at2(ctx->coll, b_id, r, c, &db) != 0) {
                    mdim_panic(ctx, "type.mdim.not_numeric");
                    return -1;
                }
                orc = op(da, db, &dr);
                if (orc == -2) {
                    mdim_panic(ctx, "math.mdim.divzero");
                    return -1;
                }
                if (orc != 0) {
                    return -1;
                }
                flat_number_init_legacy_double(&cells[c], dr);
            }
            if (flat_list_build(ctx->coll, ctx->pool, cells, cols, &row_id) != 0) {
                return -1;
            }
            if (flat_list_slice_copy(ctx->coll, ctx->pool, row_id, 0, cols, &row_dup) != 0) {
                return -1;
            }
            flat_value_set_list(&built[r], row_dup);
            flat_value_retain(ctx->coll, ctx->pool, &built[r]);
        }
        if (flat_list_build(ctx->coll, ctx->pool, built, rows, out_id) != 0) {
            for (r = 0; r < rows; r++) {
                flat_value_release(ctx->coll, ctx->pool, &built[r]);
            }
            return -1;
        }
        for (r = 0; r < rows; r++) {
            flat_value_release(ctx->coll, ctx->pool, &built[r]);
        }
        return 0;
    }
    {
        int outer = (int)info->dims[0];
        FlatListShapeInfo sub = *info;
        if (outer > FLAT_LIST_MAX_ELEMS) {
            return -1;
        }
        sub.rank--;
        sub.dims_n--;
        memmove(sub.dims, sub.dims + 1, (size_t)sub.dims_n * sizeof sub.dims[0]);
        for (i = 0; i < outer; i++) {
            FlatValue a_el;
            FlatValue b_el;
            uint32_t child_id;
            uint32_t child_dup;
            if (flat_list_get(ctx->coll, a_id, i, &a_el) != 0 ||
                flat_list_get(ctx->coll, b_id, i, &b_el) != 0 || a_el.tag != VAL_LIST ||
                b_el.tag != VAL_LIST) {
                return -1;
            }
            if (mdim_map_binary_shaped(ctx, a_el.u.coll_id, b_el.u.coll_id, &sub, op, &child_id) !=
                0) {
                return -1;
            }
            if (mdim_dup_list(ctx, child_id, &child_dup) != 0) {
                return -1;
            }
            flat_value_set_list(&items[i], child_dup);
        }
        if (mdim_materialize_list_children(ctx, items, outer) != 0) {
            return -1;
        }
        return flat_list_build(ctx->coll, ctx->pool, items, outer, out_id);
    }
}

static int mdim_op_add(double a, double b, double *out) {
    *out = a + b;
    return 0;
}

static int mdim_op_sub(double a, double b, double *out) {
    *out = a - b;
    return 0;
}

static int mdim_op_mul(double a, double b, double *out) {
    *out = a * b;
    return 0;
}

static int mdim_op_div(double a, double b, double *out) {
    if (b == 0.0) {
        return -2;
    }
    *out = a / b;
    return 0;
}

static int mdim_binary_native(FlatNativeCtx *ctx, MdimBinOpFn op) {
    char inner_buf[512];
    char left_buf[384];
    char right_buf[384];
    char *left;
    char *right;
    FlatValue a_v;
    FlatValue b_v;
    FlatListShapeInfo ia;
    FlatListShapeInfo ib;
    uint32_t out_id;
    int rc = -1;

    memset(&a_v, 0, sizeof a_v);
    memset(&b_v, 0, sizeof b_v);
    if (!ctx->out || !ctx->io_status || !ctx->inner) {
        return -1;
    }
    ctx->panic_detail[0] = '\0';
    *ctx->io_status = FLAT_IO_OK;
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &left, &right) != 0) {
        mdim_panic(ctx, "type.mdim.dim_mismatch");
        return -1;
    }
    snprintf(left_buf, sizeof left_buf, "%s", left);
    snprintf(right_buf, sizeof right_buf, "%s", right);
    if (mdim_parse_list(ctx, left_buf, &a_v) != 0) {
        goto out;
    }
    if (mdim_parse_list(ctx, right_buf, &b_v) != 0) {
        goto out;
    }
    if (mdim_operand_info(ctx, &a_v, 1, &ia) != 0 || mdim_operand_info(ctx, &b_v, 1, &ib) != 0) {
        goto out;
    }
    if (!mdim_dims_equal(&ia, &ib)) {
        mdim_panic(ctx, "type.mdim.dim_mismatch");
        goto out;
    }
    if (mdim_map_binary_shaped(ctx, a_v.u.coll_id, b_v.u.coll_id, &ia, op, &out_id) != 0) {
        goto out;
    }
    mdim_stamp_shape(ctx->coll, out_id, &ia);
    if (flat_value_set_list(ctx->out, out_id) != 0) {
        goto out;
    }
    rc = 0;
out:
    flat_native_parse_rhs_release(ctx, &a_v);
    flat_native_parse_rhs_release(ctx, &b_v);
    return rc;
}

typedef struct {
    int have;
    double acc;
    int is_min;
    int is_max;
} MdimReduceAcc;

static int mdim_reduce_visit(FlatCollectionPool *coll, uint32_t list_id, MdimReduceAcc *acc) {
    int len;
    int i;
    FlatValue el;

    if (!coll || !acc || flat_list_len(coll, list_id, &len) != 0) {
        return -1;
    }
    for (i = 0; i < len; i++) {
        if (flat_list_get(coll, list_id, i, &el) != 0) {
            return -1;
        }
        if (el.tag == VAL_LIST) {
            if (mdim_reduce_visit(coll, el.u.coll_id, acc) != 0) {
                return -1;
            }
        } else if (el.tag == VAL_NUMBER) {
            double d = flat_number_as_f64(&el);
            if (!acc->have) {
                acc->acc = d;
                acc->have = 1;
            } else if (acc->is_min) {
                if (d < acc->acc) {
                    acc->acc = d;
                }
            } else if (acc->is_max) {
                if (d > acc->acc) {
                    acc->acc = d;
                }
            } else {
                acc->acc += d;
            }
        } else {
            return -2;
        }
    }
    return 0;
}

static int mdim_count_numeric_leaves(FlatCollectionPool *coll, uint32_t list_id, int *count_out) {
    int len;
    int i;
    FlatValue el;

    if (!coll || !count_out || flat_list_len(coll, list_id, &len) != 0) {
        return -1;
    }
    for (i = 0; i < len; i++) {
        if (flat_list_get(coll, list_id, i, &el) != 0) {
            return -1;
        }
        if (el.tag == VAL_LIST) {
            if (mdim_count_numeric_leaves(coll, el.u.coll_id, count_out) != 0) {
                return -1;
            }
        } else if (el.tag == VAL_NUMBER) {
            (*count_out)++;
        } else {
            return -2;
        }
    }
    return 0;
}

static int mdim_reduce_native(FlatNativeCtx *ctx, int kind) {
    FlatValue list_v;
    FlatListShapeInfo info;
    MdimReduceAcc acc;
    int count = 0;
    int rc = -1;

    memset(&list_v, 0, sizeof list_v);
    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    ctx->panic_detail[0] = '\0';
    *ctx->io_status = FLAT_IO_OK;
    if (mdim_parse_list(ctx, ctx->inner, &list_v) != 0) {
        goto out;
    }
    if (mdim_operand_info(ctx, &list_v, 1, &info) != 0) {
        goto out;
    }
    memset(&acc, 0, sizeof acc);
    acc.is_min = (kind == 2);
    acc.is_max = (kind == 3);
    {
        int vrc = mdim_reduce_visit(ctx->coll, list_v.u.coll_id, &acc);
        if (vrc == -2) {
            mdim_panic(ctx, "type.mdim.not_numeric");
            goto out;
        }
        if (vrc != 0) {
            mdim_panic(ctx, "type.mdim.dim_mismatch");
            goto out;
        }
    }
    if (!acc.have) {
        mdim_panic(ctx, "type.mdim.dim_mismatch");
        goto out;
    }
    if (kind == 0 || kind == 2 || kind == 3) {
        if (flat_value_set_number(ctx->out, acc.acc) != 0) {
            goto out;
        }
        rc = 0;
        goto out;
    }
    if (mdim_count_numeric_leaves(ctx->coll, list_v.u.coll_id, &count) != 0 || count <= 0) {
        mdim_panic(ctx, "type.mdim.dim_mismatch");
        goto out;
    }
    if (flat_value_set_number(ctx->out, acc.acc / (double)count) != 0) {
        goto out;
    }
    rc = 0;
out:
    flat_native_parse_rhs_release(ctx, &list_v);
    return rc;
}

static int mdim_at2(FlatCollectionPool *coll, uint32_t mat_id, int row, int col, double *out) {
    FlatValue row_v;
    FlatValue cell;
    int got_row;

    if (flat_list_get(coll, mat_id, row, &row_v) != 0 || row_v.tag != VAL_LIST) {
        return -1;
    }
    if (flat_list_len(coll, row_v.u.coll_id, &got_row) != 0) {
        return -1;
    }
    if (col < 0 || col >= got_row) {
        return -1;
    }
    if (flat_list_get(coll, row_v.u.coll_id, col, &cell) != 0 || cell.tag != VAL_NUMBER) {
        return -1;
    }
    *out = flat_number_as_f64(&cell);
    return 0;
}

static int mdim_dot_native(FlatNativeCtx *ctx) {
    char inner_buf[512];
    char left_buf[384];
    char right_buf[384];
    char *left;
    char *right;
    FlatValue a_v;
    FlatValue b_v;
    FlatListShapeInfo ia;
    FlatListShapeInfo ib;
    int i;
    int n;
    int n_b;
    double sum = 0.0;
    int rc = -1;

    memset(&a_v, 0, sizeof a_v);
    memset(&b_v, 0, sizeof b_v);
    if (!ctx->out || !ctx->io_status || !ctx->inner) {
        return -1;
    }
    ctx->panic_detail[0] = '\0';
    *ctx->io_status = FLAT_IO_OK;
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &left, &right) != 0) {
        mdim_panic(ctx, "type.mdim.dim_mismatch");
        return -1;
    }
    snprintf(left_buf, sizeof left_buf, "%s", left);
    snprintf(right_buf, sizeof right_buf, "%s", right);
    if (mdim_parse_list(ctx, left_buf, &a_v) != 0) {
        goto out;
    }
    if (mdim_parse_list(ctx, right_buf, &b_v) != 0) {
        goto out;
    }
    if (mdim_operand_info(ctx, &a_v, 1, &ia) != 0 || mdim_operand_info(ctx, &b_v, 1, &ib) != 0) {
        goto out;
    }
    if (ia.rank != 1 || ib.rank != 1) {
        mdim_panic(ctx, "type.mdim.dim_mismatch");
        goto out;
    }
    if (flat_list_len(ctx->coll, a_v.u.coll_id, &n) != 0 ||
        flat_list_len(ctx->coll, b_v.u.coll_id, &n_b) != 0 || n != n_b || n <= 0) {
        mdim_panic(ctx, "type.mdim.dim_mismatch");
        goto out;
    }
    for (i = 0; i < n; i++) {
        FlatValue ea;
        FlatValue eb;
        double da;
        double db;
        if (flat_list_get(ctx->coll, a_v.u.coll_id, i, &ea) != 0 ||
            flat_list_get(ctx->coll, b_v.u.coll_id, i, &eb) != 0) {
            goto out;
        }
        if (ea.tag != VAL_NUMBER || eb.tag != VAL_NUMBER) {
            mdim_panic(ctx, "type.mdim.not_numeric");
            goto out;
        }
        da = flat_number_as_f64(&ea);
        db = flat_number_as_f64(&eb);
        sum += da * db;
    }
    if (flat_value_set_number(ctx->out, sum) != 0) {
        goto out;
    }
    rc = 0;
out:
    flat_native_parse_rhs_release(ctx, &a_v);
    flat_native_parse_rhs_release(ctx, &b_v);
    return rc;
}

static int mdim_transpose_native(FlatNativeCtx *ctx) {
    FlatValue m_v;
    FlatListShapeInfo info;
    int rows;
    int cols;
    int r;
    int c;
    uint32_t out_id;
    int rc = -1;

    memset(&m_v, 0, sizeof m_v);
    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    ctx->panic_detail[0] = '\0';
    *ctx->io_status = FLAT_IO_OK;
    if (mdim_parse_list(ctx, ctx->inner, &m_v) != 0) {
        goto out;
    }
    if (mdim_operand_info(ctx, &m_v, 1, &info) != 0) {
        goto out;
    }
    if (info.rank != 2) {
        mdim_panic(ctx, "type.mdim.rank");
        goto out;
    }
    rows = (int)info.dims[0];
    cols = (int)info.dims[1];
    if (cols > FLAT_LIST_MAX_ELEMS || rows > FLAT_LIST_MAX_ELEMS) {
        mdim_panic(ctx, "type.mdim.dim_mismatch");
        goto out;
    }
    {
        FlatValue built[FLAT_LIST_MAX_ELEMS];
        for (c = 0; c < cols; c++) {
            FlatValue col_cells[FLAT_LIST_MAX_ELEMS];
            uint32_t row_id;
            uint32_t row_dup;
            for (r = 0; r < rows; r++) {
                double v;
                if (mdim_at2(ctx->coll, m_v.u.coll_id, r, c, &v) != 0) {
                    goto out;
                }
                flat_number_init_legacy_double(&col_cells[r], v);
            }
            if (flat_list_build(ctx->coll, ctx->pool, col_cells, rows, &row_id) != 0) {
                goto out;
            }
            if (mdim_dup_list(ctx, row_id, &row_dup) != 0) {
                goto out;
            }
            flat_value_set_list(&built[c], row_dup);
            flat_value_retain(ctx->coll, ctx->pool, &built[c]);
        }
        if (flat_list_build(ctx->coll, ctx->pool, built, cols, &out_id) != 0) {
            while (c > 0) {
                c--;
                flat_value_release(ctx->coll, ctx->pool, &built[c]);
            }
            goto out;
        }
        for (c = 0; c < cols; c++) {
            flat_value_release(ctx->coll, ctx->pool, &built[c]);
        }
    }
    {
        FlatListShapeInfo out_info;
        memset(&out_info, 0, sizeof out_info);
        out_info.valid = 1;
        out_info.rect = 1;
        out_info.rank = 2;
        out_info.dims_n = 2;
        out_info.dims[0] = (uint16_t)cols;
        out_info.dims[1] = (uint16_t)rows;
        out_info.elem_known = info.elem_known;
        out_info.elem_kind = info.elem_kind;
        mdim_stamp_shape(ctx->coll, out_id, &out_info);
    }
    if (flat_value_set_list(ctx->out, out_id) != 0) {
        goto out;
    }
    rc = 0;
out:
    flat_native_parse_rhs_release(ctx, &m_v);
    return rc;
}

static int mdim_matmul_native(FlatNativeCtx *ctx) {
    char inner_buf[512];
    char left_buf[384];
    char right_buf[384];
    char *left;
    char *right;
    FlatValue a_v;
    FlatValue b_v;
    FlatListShapeInfo ia;
    FlatListShapeInfo ib;
    int a_rows;
    int a_cols;
    int b_rows;
    int b_cols;
    int r;
    uint32_t out_id;
    int rc = -1;

    memset(&a_v, 0, sizeof a_v);
    memset(&b_v, 0, sizeof b_v);
    if (!ctx->out || !ctx->io_status || !ctx->inner) {
        return -1;
    }
    ctx->panic_detail[0] = '\0';
    *ctx->io_status = FLAT_IO_OK;
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &left, &right) != 0) {
        mdim_panic(ctx, "type.mdim.dim_mismatch");
        return -1;
    }
    snprintf(left_buf, sizeof left_buf, "%s", left);
    snprintf(right_buf, sizeof right_buf, "%s", right);
    if (mdim_parse_list(ctx, left_buf, &a_v) != 0) {
        goto out;
    }
    if (mdim_parse_list(ctx, right_buf, &b_v) != 0) {
        goto out;
    }
    if (mdim_operand_info(ctx, &a_v, 1, &ia) != 0 || mdim_operand_info(ctx, &b_v, 1, &ib) != 0) {
        goto out;
    }
    if (ia.rank != 2 || ib.rank != 2) {
        mdim_panic(ctx, "type.mdim.rank");
        goto out;
    }
    a_rows = (int)ia.dims[0];
    a_cols = (int)ia.dims[1];
    b_rows = (int)ib.dims[0];
    b_cols = (int)ib.dims[1];
    if (a_cols != b_rows) {
        mdim_panic(ctx, "type.mdim.dim_mismatch");
        goto out;
    }
    if (a_rows > FLAT_LIST_MAX_ELEMS || b_cols > FLAT_LIST_MAX_ELEMS) {
        mdim_panic(ctx, "type.mdim.dim_mismatch");
        goto out;
    }
    {
        FlatValue built[FLAT_LIST_MAX_ELEMS];
        for (r = 0; r < a_rows; r++) {
            FlatValue cells[FLAT_LIST_MAX_ELEMS];
            uint32_t row_id;
            uint32_t row_dup;
            int c;
            for (c = 0; c < b_cols; c++) {
                double sum = 0.0;
                int k;
                for (k = 0; k < a_cols; k++) {
                    double av;
                    double bv;
                    if (mdim_at2(ctx->coll, a_v.u.coll_id, r, k, &av) != 0 ||
                        mdim_at2(ctx->coll, b_v.u.coll_id, k, c, &bv) != 0) {
                        goto out;
                    }
                    sum += av * bv;
                }
                flat_number_init_legacy_double(&cells[c], sum);
            }
            if (flat_list_build(ctx->coll, ctx->pool, cells, b_cols, &row_id) != 0) {
                goto out;
            }
            if (mdim_dup_list(ctx, row_id, &row_dup) != 0) {
                goto out;
            }
            flat_value_set_list(&built[r], row_dup);
            flat_value_retain(ctx->coll, ctx->pool, &built[r]);
        }
        if (flat_list_build(ctx->coll, ctx->pool, built, a_rows, &out_id) != 0) {
            for (r = 0; r < a_rows; r++) {
                flat_value_release(ctx->coll, ctx->pool, &built[r]);
            }
            goto out;
        }
        for (r = 0; r < a_rows; r++) {
            flat_value_release(ctx->coll, ctx->pool, &built[r]);
        }
    }
    {
        FlatListShapeInfo out_info;
        memset(&out_info, 0, sizeof out_info);
        out_info.valid = 1;
        out_info.rect = 1;
        out_info.rank = 2;
        out_info.dims_n = 2;
        out_info.dims[0] = (uint16_t)a_rows;
        out_info.dims[1] = (uint16_t)b_cols;
        out_info.elem_known = ia.elem_known;
        out_info.elem_kind = ia.elem_kind;
        mdim_stamp_shape(ctx->coll, out_id, &out_info);
    }
    if (flat_value_set_list(ctx->out, out_id) != 0) {
        goto out;
    }
    rc = 0;
out:
    flat_native_parse_rhs_release(ctx, &a_v);
    flat_native_parse_rhs_release(ctx, &b_v);
    return rc;
}

int flat_native_math_mdim_add(FlatNativeCtx *ctx) {
    return mdim_binary_native(ctx, mdim_op_add);
}

int flat_native_math_mdim_subtract(FlatNativeCtx *ctx) {
    return mdim_binary_native(ctx, mdim_op_sub);
}

int flat_native_math_mdim_multiply(FlatNativeCtx *ctx) {
    return mdim_binary_native(ctx, mdim_op_mul);
}

int flat_native_math_mdim_divide(FlatNativeCtx *ctx) {
    return mdim_binary_native(ctx, mdim_op_div);
}

int flat_native_math_mdim_sum(FlatNativeCtx *ctx) {
    return mdim_reduce_native(ctx, 0);
}

int flat_native_math_mdim_min(FlatNativeCtx *ctx) {
    return mdim_reduce_native(ctx, 2);
}

int flat_native_math_mdim_max(FlatNativeCtx *ctx) {
    return mdim_reduce_native(ctx, 3);
}

int flat_native_math_mdim_avg(FlatNativeCtx *ctx) {
    return mdim_reduce_native(ctx, 1);
}

int flat_native_math_mdim_dot(FlatNativeCtx *ctx) {
    return mdim_dot_native(ctx);
}

int flat_native_math_mdim_transpose(FlatNativeCtx *ctx) {
    return mdim_transpose_native(ctx);
}

int flat_native_math_mdim_matmul(FlatNativeCtx *ctx) {
    return mdim_matmul_native(ctx);
}