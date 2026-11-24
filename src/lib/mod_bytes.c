#include "flat_native.h"
#include "flat_native_mod.h"
#include "flat_native_util.h"
#include "flat_number.h"
#include "flat_slot_type.h"
#include "flat_value.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BYTES_MAX_OUTPUT (16u * 1024u * 1024u)

typedef struct {
    FlatSlotTypeKind kind;
    size_t size;
    int little;
    int is_text;
} BytesFormat;

static int bytes_fail(FlatNativeCtx *ctx, const char *why) {
    if (ctx && ctx->io_status) *ctx->io_status = FLAT_IO_ERR;
    if (ctx && why) snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", why);
    return -1;
}
static int bytes_ok(FlatNativeCtx *ctx) {
    if (ctx && ctx->io_status) *ctx->io_status = FLAT_IO_OK;
    return 0;
}
static void release_values(FlatNativeCtx *ctx, FlatValue *v, int n) {
    int i; for (i = 0; i < n; ++i) flat_native_parse_rhs_release(ctx, &v[i]);
}
static int split_top(char *s, char delim, char **parts, int max_parts) {
    int depth = 0, n = 0; char q = 0; char *start = s, *p;
    for (p = s; *p; ++p) {
        if (q) { if (*p == q && (p == s || p[-1] != '\\')) q = 0; continue; }
        if (*p == '\'' || *p == '"') { q = *p; continue; }
        if (*p == '(' || *p == '[' || *p == '{') depth++;
        else if ((*p == ')' || *p == ']' || *p == '}') && depth) depth--;
        else if (*p == delim && depth == 0) {
            *p = '\0'; if (n < max_parts) parts[n] = flat_native_trim(start); n++;
            start = p + 1;
        }
    }
    if (n < max_parts) parts[n] = flat_native_trim(start);
    return n + 1;
}
static int parse_value(FlatNativeCtx *ctx, const char *expr, FlatValue *out) {
    char buf[4096];
    if (!ctx || !expr || !out) return -1;
    snprintf(buf, sizeof buf, "%s", expr);
    flat_native_trim(buf);
    if (flat_native_eval_call(ctx, buf, out) == 0) return 0;
    return flat_native_parse_rhs(ctx, buf, out);
}
static int get_bytes(FlatNativeCtx *ctx, const FlatValue *v, const uint8_t **p, size_t *n) {
    if (!ctx || !v || !p || !n) return -1;
    if (v->tag == VAL_STR_INLINE) { *p = (const uint8_t *)v->u.ins.bytes; *n = v->u.ins.len; return 0; }
    if (v->tag == VAL_STR_REF) { *p = flat_pool_read(ctx->pool, v->u.ref_id, n); return *p ? 0 : -1; }
    return -1;
}
static int out_bytes(FlatNativeCtx *ctx, const void *p, size_t n) {
    uint32_t id;
    if (!ctx || !ctx->out || n > BYTES_MAX_OUTPUT) return bytes_fail(ctx, "bytes.size");
    if (n <= FLAT_INLINE_STR_MAX) { bytes_ok(ctx); return flat_value_set_str_inline(ctx->out, p ? (const char *)p : "", n); }
    if (flat_pool_alloc_copy(ctx->pool, p, n, &id) != 0) return bytes_fail(ctx, "bytes.memory");
    bytes_ok(ctx); return flat_value_set_str_ref(ctx->out, id);
}
static int parse_format(FlatNativeCtx *ctx, BytesFormat *f) {
    FlatValue v; const uint8_t *p; size_t n; char b[96]; char *tok, *save = NULL; int parts = 0;
    memset(&v, 0, sizeof v); memset(f, 0, sizeof *f); f->little = 1;
    if (!ctx->tag_expr || parse_value(ctx, ctx->tag_expr, &v) != 0 || get_bytes(ctx, &v, &p, &n) != 0 || n == 0 || n >= sizeof b) {
        flat_native_parse_rhs_release(ctx, &v); return -1;
    }
    memcpy(b, p, n); b[n] = '\0'; flat_native_parse_rhs_release(ctx, &v);
    for (tok = strtok_r(b, " \t", &save); tok; tok = strtok_r(NULL, " \t", &save)) {
        if (parts == 0) {
            if (tok[0] == 't' && tok[1]) {
                char *end = NULL; unsigned long x = strtoul(tok + 1, &end, 10);
                if (!end || *end || x == 0 || x > FLAT_SLOT_TYPE_AUX_T_MAX) return -1;
                f->kind = FLAT_SLOT_TYPE_TN; f->size = (size_t)x; f->is_text = 1;
            } else {
                FlatSlotType ty;
                if (flat_slot_type_parse_spec(tok, &ty, NULL, NULL, 0) != 0 || !flat_slot_type_is_numeric(&ty)) return -1;
                f->kind = ty.kind;
                switch (ty.kind) {
                    case FLAT_SLOT_TYPE_U8: case FLAT_SLOT_TYPE_S8: f->size = 1; break;
                    case FLAT_SLOT_TYPE_U16: case FLAT_SLOT_TYPE_S16: f->size = 2; break;
                    case FLAT_SLOT_TYPE_U32: case FLAT_SLOT_TYPE_S32: case FLAT_SLOT_TYPE_F32: f->size = 4; break;
                    case FLAT_SLOT_TYPE_U64: case FLAT_SLOT_TYPE_S64: case FLAT_SLOT_TYPE_F64: f->size = 8; break;
                    default: return -1;
                }
            }
        } else if (parts == 1 && !f->is_text && (strcmp(tok, "le") == 0 || strcmp(tok, "be") == 0)) {
            f->little = strcmp(tok, "le") == 0;
        } else return -1;
        parts++;
    }
    return (parts == 1 || (parts == 2 && !f->is_text)) ? 0 : -1;
}
static int parse_num_arg(FlatNativeCtx *ctx, const char *expr, FlatValue *v) {
    memset(v, 0, sizeof *v);
    return parse_value(ctx, expr, v) == 0 && v->tag == VAL_NUMBER ? 0 : -1;
}
static int exact_index(const FlatValue *v, size_t *out) {
    double d; if (!v || v->tag != VAL_NUMBER) return -1;
    d = flat_number_as_f64(v);
    if (!isfinite(d) || d < 0 || d > (double)SIZE_MAX || floor(d) != d || d > 9007199254740991.0) return -1;
    *out = (size_t)d; return 0;
}
static uint64_t read_uint(const uint8_t *p, size_t n, int little) {
    uint64_t x = 0; size_t i;
    if (little) for (i = 0; i < n; ++i) x |= (uint64_t)p[i] << (8u * i);
    else for (i = 0; i < n; ++i) x = (x << 8) | p[i];
    return x;
}
static void write_uint(uint8_t *p, size_t n, int little, uint64_t x) {
    size_t i; for (i = 0; i < n; ++i) { size_t j = little ? i : n - 1 - i; p[j] = (uint8_t)(x >> (8u * i)); }
}
static int numeric_from_bits(FlatValue *out, const BytesFormat *f, uint64_t bits) {
    uint64_t u = bits; int64_t si = 0; double d = 0.0;
    switch (f->kind) {
        case FLAT_SLOT_TYPE_U8: case FLAT_SLOT_TYPE_U16: case FLAT_SLOT_TYPE_U32: case FLAT_SLOT_TYPE_U64:
            return flat_number_set_for_slot_kind(out, f->kind, u, 0, 0.0);
        case FLAT_SLOT_TYPE_S8: case FLAT_SLOT_TYPE_S16: case FLAT_SLOT_TYPE_S32: case FLAT_SLOT_TYPE_S64: {
            unsigned bits_n = (unsigned)(f->size * 8); uint64_t sign = UINT64_C(1) << (bits_n - 1);
            if (bits_n < 64 && (u & sign)) u |= (~UINT64_C(0)) << bits_n;
            si = (int64_t)u; return flat_number_set_for_slot_kind(out, f->kind, 0, si, 0.0);
        }
        case FLAT_SLOT_TYPE_F32: { uint32_t b = (uint32_t)bits; float x; memcpy(&x, &b, sizeof x); d = x; return flat_number_set_for_slot_kind(out, f->kind, 0, 0, d); }
        case FLAT_SLOT_TYPE_F64: memcpy(&d, &bits, sizeof d); return flat_number_set_for_slot_kind(out, f->kind, 0, 0, d);
        default: return -1;
    }
}
static int numeric_to_bits(FlatNativeCtx *ctx, const FlatValue *src, const BytesFormat *f, uint64_t *bits) {
    FlatValue v; FlatNumStor st; memset(&v, 0, sizeof v);
    if (!flat_number_is_number(src)) return -1;
    if (f->kind == FLAT_SLOT_TYPE_F32) {
        double d = flat_number_as_f64(src); uint32_t b; float x;
        if (!isfinite(d) || d > (double)FLT_MAX || d < -(double)FLT_MAX) return -1;
        x = (float)d; memcpy(&b, &x, sizeof b); *bits = b; return 0;
    }
    if (f->kind == FLAT_SLOT_TYPE_F64) {
        double x = flat_number_as_f64(src); if (!isfinite(x)) return -1;
        memcpy(bits, &x, sizeof x); return 0;
    }
    if (flat_number_coerce_for_slot(src, f->kind, &v, ctx->panic_detail, sizeof ctx->panic_detail) != 0) return -1;
    st = flat_number_stor(&v);
    switch (f->kind) {
        case FLAT_SLOT_TYPE_U8: case FLAT_SLOT_TYPE_U16: case FLAT_SLOT_TYPE_U32: case FLAT_SLOT_TYPE_U64:
            if (st < FLAT_NUM_STOR_U8 || st > FLAT_NUM_STOR_U64) return -1;
            *bits = v.u.number.payload.u64; return 0;
        case FLAT_SLOT_TYPE_S8: case FLAT_SLOT_TYPE_S16: case FLAT_SLOT_TYPE_S32: case FLAT_SLOT_TYPE_S64:
            if (st < FLAT_NUM_STOR_S8 || st > FLAT_NUM_STOR_S64) return -1;
            *bits = (uint64_t)v.u.number.payload.i64; return 0;
        default: return -1;
    }
}
int flat_native_bytes_read(FlatNativeCtx *ctx) {
    char buf[4096], *a[3]; int argc; FlatValue v[2]; const uint8_t *p; size_t n, off; BytesFormat f; uint64_t bits; int rc;
    memset(v, 0, sizeof v); if (!ctx || !ctx->inner || !ctx->out) return -1;
    snprintf(buf, sizeof buf, "%s", ctx->inner); argc = split_top(buf, ',', a, 3);
    if (argc != 2) { release_values(ctx, v, 2); return bytes_fail(ctx, "bytes.read.args"); }
    if (parse_format(ctx, &f) != 0) { release_values(ctx, v, 2); return bytes_fail(ctx, "bytes.read.format"); }
    if (parse_value(ctx, a[0], &v[0]) != 0 || get_bytes(ctx, &v[0], &p, &n) != 0) { release_values(ctx, v, 2); return bytes_fail(ctx, "bytes.read.data"); }
    if (parse_num_arg(ctx, a[1], &v[1]) != 0 || exact_index(&v[1], &off) != 0) { release_values(ctx, v, 2); return bytes_fail(ctx, "bytes.read.offset"); }
    if (off > n || f.size > n - off) { release_values(ctx, v, 2); return bytes_fail(ctx, "bytes.read.range"); }
    if (f.is_text) rc = out_bytes(ctx, p + off, f.size);
    else { bits = read_uint(p + off, f.size, f.little); rc = numeric_from_bits(ctx->out, &f, bits); if (rc == 0) bytes_ok(ctx); }
    release_values(ctx, v, 2); return rc;
}
int flat_native_bytes_write(FlatNativeCtx *ctx) {
    FlatValue v; const uint8_t *p; size_t n; BytesFormat f; uint8_t out[8192]; uint64_t bits; int rc;
    memset(&v, 0, sizeof v); if (!ctx || !ctx->inner || !ctx->out || parse_format(ctx, &f) != 0 || parse_value(ctx, ctx->inner, &v) != 0) { flat_native_parse_rhs_release(ctx, &v); return bytes_fail(ctx, "bytes.write.args"); }
    if (f.is_text) {
        if (get_bytes(ctx, &v, &p, &n) != 0 || n != f.size) { flat_native_parse_rhs_release(ctx, &v); return bytes_fail(ctx, "bytes.write.length"); }
        rc = out_bytes(ctx, p, n);
    } else {
        if (numeric_to_bits(ctx, &v, &f, &bits) != 0) { flat_native_parse_rhs_release(ctx, &v); return bytes_fail(ctx, "bytes.write.range"); }
        write_uint(out, f.size, f.little, bits); rc = out_bytes(ctx, out, f.size);
    }
    flat_native_parse_rhs_release(ctx, &v); return rc;
}
int flat_native_bytes_concat(FlatNativeCtx *ctx) {
    char buf[4096], *a[3]; int argc; FlatValue v[2]; const uint8_t *p[2]; size_t n[2]; uint8_t *out; int rc;
    memset(v, 0, sizeof v); if (!ctx || !ctx->inner) return -1; snprintf(buf, sizeof buf, "%s", ctx->inner); argc = split_top(buf, ',', a, 3);
    if (argc != 2 || parse_value(ctx, a[0], &v[0]) != 0 || parse_value(ctx, a[1], &v[1]) != 0 || get_bytes(ctx, &v[0], &p[0], &n[0]) != 0 || get_bytes(ctx, &v[1], &p[1], &n[1]) != 0) { release_values(ctx, v, 2); return bytes_fail(ctx, "bytes.concat.args"); }
    if (n[1] > BYTES_MAX_OUTPUT || n[0] > BYTES_MAX_OUTPUT - n[1]) { release_values(ctx, v, 2); return bytes_fail(ctx, "bytes.size"); }
    out = (uint8_t *)malloc(n[0] + n[1] ? n[0] + n[1] : 1); if (!out) { release_values(ctx, v, 2); return bytes_fail(ctx, "bytes.memory"); }
    memcpy(out, p[0], n[0]); memcpy(out + n[0], p[1], n[1]); rc = out_bytes(ctx, out, n[0] + n[1]); free(out); release_values(ctx, v, 2); return rc;
}
int flat_native_bytes_slice(FlatNativeCtx *ctx) {
    char buf[4096], *a[4]; int argc; FlatValue v[3]; const uint8_t *p; size_t n, off, len; int rc;
    memset(v, 0, sizeof v); if (!ctx || !ctx->inner) return -1; snprintf(buf, sizeof buf, "%s", ctx->inner); argc = split_top(buf, ',', a, 4);
    if (argc != 3 || parse_value(ctx, a[0], &v[0]) != 0 || get_bytes(ctx, &v[0], &p, &n) != 0 || parse_num_arg(ctx, a[1], &v[1]) != 0 || exact_index(&v[1], &off) != 0 || parse_num_arg(ctx, a[2], &v[2]) != 0 || exact_index(&v[2], &len) != 0 || off > n || len > n - off) { release_values(ctx, v, 3); return bytes_fail(ctx, "bytes.slice.range"); }
    rc = out_bytes(ctx, p + off, len); release_values(ctx, v, 3); return rc;
}
int flat_native_bytes_len(FlatNativeCtx *ctx) {
    FlatValue v; const uint8_t *p; size_t n; int rc; memset(&v, 0, sizeof v);
    if (!ctx || !ctx->inner || parse_value(ctx, ctx->inner, &v) != 0 || get_bytes(ctx, &v, &p, &n) != 0) { flat_native_parse_rhs_release(ctx, &v); return bytes_fail(ctx, "bytes.len.args"); }
    rc = flat_value_set_number(ctx->out, (double)n); bytes_ok(ctx); flat_native_parse_rhs_release(ctx, &v); return rc;
}
