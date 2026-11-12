#include "flat_number.h"

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_number_meta(FlatValue *v, FlatNumClass nc, FlatNumStor ns) {
    v->tag = VAL_NUMBER;
    v->u.number.num_class = (uint8_t)nc;
    v->u.number.num_stor = (uint8_t)ns;
    v->u.number._pad = 0;
}

int flat_number_is_number(const FlatValue *v) {
    return v && v->tag == VAL_NUMBER;
}

FlatNumClass flat_number_class(const FlatValue *v) {
    if (!flat_number_is_number(v)) {
        return FLAT_NUM_CLASS_DECIMAL;
    }
    return (FlatNumClass)v->u.number.num_class;
}

FlatNumStor flat_number_stor(const FlatValue *v) {
    if (!flat_number_is_number(v)) {
        return FLAT_NUM_STOR_F64;
    }
    return (FlatNumStor)v->u.number.num_stor;
}

void flat_number_set_decimal_f64(FlatValue *v, double d) {
    if (!v) {
        return;
    }
    set_number_meta(v, FLAT_NUM_CLASS_DECIMAL, FLAT_NUM_STOR_F64);
    v->u.number.payload.f64 = d;
}

void flat_number_set_decimal_f32(FlatValue *v, float f) {
    if (!v) {
        return;
    }
    set_number_meta(v, FLAT_NUM_CLASS_DECIMAL, FLAT_NUM_STOR_F32);
    v->u.number.payload.f32 = f;
}

void flat_number_set_whole_u64(FlatValue *v, uint64_t n) {
    if (!v) {
        return;
    }
    set_number_meta(v, FLAT_NUM_CLASS_WHOLE, FLAT_NUM_STOR_U64);
    v->u.number.payload.u64 = n;
}

void flat_number_set_whole_i64(FlatValue *v, int64_t n) {
    if (!v) {
        return;
    }
    set_number_meta(v, FLAT_NUM_CLASS_WHOLE, FLAT_NUM_STOR_S64);
    v->u.number.payload.i64 = n;
}

void flat_number_init_legacy_double(FlatValue *v, double d) {
    flat_number_set_decimal_f64(v, d);
}

int flat_number_set_for_slot_kind(FlatValue *v, FlatSlotTypeKind kind, uint64_t u, int64_t i,
                                  double frac) {
    if (!v) {
        return -1;
    }
    switch (kind) {
    case FLAT_SLOT_TYPE_U8:
        set_number_meta(v, FLAT_NUM_CLASS_WHOLE, FLAT_NUM_STOR_U8);
        v->u.number.payload.u64 = u & 0xffu;
        return 0;
    case FLAT_SLOT_TYPE_U16:
        set_number_meta(v, FLAT_NUM_CLASS_WHOLE, FLAT_NUM_STOR_U16);
        v->u.number.payload.u64 = u & 0xffffu;
        return 0;
    case FLAT_SLOT_TYPE_U32:
        set_number_meta(v, FLAT_NUM_CLASS_WHOLE, FLAT_NUM_STOR_U32);
        v->u.number.payload.u64 = u & 0xffffffffu;
        return 0;
    case FLAT_SLOT_TYPE_U64:
        set_number_meta(v, FLAT_NUM_CLASS_WHOLE, FLAT_NUM_STOR_U64);
        v->u.number.payload.u64 = u;
        return 0;
    case FLAT_SLOT_TYPE_S8:
        set_number_meta(v, FLAT_NUM_CLASS_WHOLE, FLAT_NUM_STOR_S8);
        v->u.number.payload.i64 = i;
        return 0;
    case FLAT_SLOT_TYPE_S16:
        set_number_meta(v, FLAT_NUM_CLASS_WHOLE, FLAT_NUM_STOR_S16);
        v->u.number.payload.i64 = i;
        return 0;
    case FLAT_SLOT_TYPE_S32:
        set_number_meta(v, FLAT_NUM_CLASS_WHOLE, FLAT_NUM_STOR_S32);
        v->u.number.payload.i64 = i;
        return 0;
    case FLAT_SLOT_TYPE_S64:
        set_number_meta(v, FLAT_NUM_CLASS_WHOLE, FLAT_NUM_STOR_S64);
        v->u.number.payload.i64 = i;
        return 0;
    case FLAT_SLOT_TYPE_F32:
        set_number_meta(v, FLAT_NUM_CLASS_DECIMAL, FLAT_NUM_STOR_F32);
        v->u.number.payload.f32 = (float)frac;
        return 0;
    case FLAT_SLOT_TYPE_F64:
        set_number_meta(v, FLAT_NUM_CLASS_DECIMAL, FLAT_NUM_STOR_F64);
        v->u.number.payload.f64 = frac;
        return 0;
    default:
        return -1;
    }
}

FlatSlotTypeKind flat_number_stor_to_slot_kind(FlatNumStor stor) {
    switch (stor) {
    case FLAT_NUM_STOR_F32:
        return FLAT_SLOT_TYPE_F32;
    case FLAT_NUM_STOR_F64:
        return FLAT_SLOT_TYPE_F64;
    case FLAT_NUM_STOR_U8:
        return FLAT_SLOT_TYPE_U8;
    case FLAT_NUM_STOR_U16:
        return FLAT_SLOT_TYPE_U16;
    case FLAT_NUM_STOR_U32:
        return FLAT_SLOT_TYPE_U32;
    case FLAT_NUM_STOR_U64:
        return FLAT_SLOT_TYPE_U64;
    case FLAT_NUM_STOR_S8:
        return FLAT_SLOT_TYPE_S8;
    case FLAT_NUM_STOR_S16:
        return FLAT_SLOT_TYPE_S16;
    case FLAT_NUM_STOR_S32:
        return FLAT_SLOT_TYPE_S32;
    case FLAT_NUM_STOR_S64:
        return FLAT_SLOT_TYPE_S64;
    default:
        return FLAT_SLOT_TYPE_NONE;
    }
}

int flat_number_kind_to_stor(FlatSlotTypeKind kind, FlatNumStor *out) {
    switch (kind) {
    case FLAT_SLOT_TYPE_F32:
        *out = FLAT_NUM_STOR_F32;
        return 0;
    case FLAT_SLOT_TYPE_F64:
        *out = FLAT_NUM_STOR_F64;
        return 0;
    case FLAT_SLOT_TYPE_U8:
        *out = FLAT_NUM_STOR_U8;
        return 0;
    case FLAT_SLOT_TYPE_U16:
        *out = FLAT_NUM_STOR_U16;
        return 0;
    case FLAT_SLOT_TYPE_U32:
        *out = FLAT_NUM_STOR_U32;
        return 0;
    case FLAT_SLOT_TYPE_U64:
        *out = FLAT_NUM_STOR_U64;
        return 0;
    case FLAT_SLOT_TYPE_S8:
        *out = FLAT_NUM_STOR_S8;
        return 0;
    case FLAT_SLOT_TYPE_S16:
        *out = FLAT_NUM_STOR_S16;
        return 0;
    case FLAT_SLOT_TYPE_S32:
        *out = FLAT_NUM_STOR_S32;
        return 0;
    case FLAT_SLOT_TYPE_S64:
        *out = FLAT_NUM_STOR_S64;
        return 0;
    default:
        return -1;
    }
}

int flat_number_descriptor_equal(const FlatValue *a, const FlatValue *b) {
    if (!flat_number_is_number(a) || !flat_number_is_number(b)) {
        return 0;
    }
    return a->u.number.num_class == b->u.number.num_class && a->u.number.num_stor == b->u.number.num_stor;
}

static int whole_u64(const FlatValue *v, uint64_t *out) {
    if (flat_number_class(v) != FLAT_NUM_CLASS_WHOLE) {
        return -1;
    }
    switch (flat_number_stor(v)) {
    case FLAT_NUM_STOR_U8:
    case FLAT_NUM_STOR_U16:
    case FLAT_NUM_STOR_U32:
    case FLAT_NUM_STOR_U64:
        *out = v->u.number.payload.u64;
        return 0;
    default:
        return -1;
    }
}

static int whole_i64(const FlatValue *v, int64_t *out) {
    if (flat_number_class(v) != FLAT_NUM_CLASS_WHOLE) {
        return -1;
    }
    switch (flat_number_stor(v)) {
    case FLAT_NUM_STOR_S8:
    case FLAT_NUM_STOR_S16:
    case FLAT_NUM_STOR_S32:
    case FLAT_NUM_STOR_S64:
        *out = v->u.number.payload.i64;
        return 0;
    default:
        return -1;
    }
}

static int is_exact_int(double d, int64_t *out_i) {
    /* A double can represent -2^63 exactly, but +2^63 is already outside
       the signed 64-bit range.  Never use a rounded INT64_MAX-as-double
       comparison here: it is equal to 2^63. */
    if (!isfinite(d) || trunc(d) != d || d < -0x1p63 || d >= 0x1p63) {
        return 0;
    }
    *out_i = (int64_t)d;
    return 1;
}

static int is_exact_uint(double d, uint64_t *out_u) {
    /* 2^64 is exactly representable as double and is the exclusive limit. */
    if (!isfinite(d) || trunc(d) != d || d < 0.0 || d >= 0x1p64) {
        return 0;
    }
    *out_u = (uint64_t)d;
    return 1;
}

double flat_number_as_f64(const FlatValue *v) {
    uint64_t u;
    int64_t i;
    if (!flat_number_is_number(v)) {
        return 0.0;
    }
    if (flat_number_class(v) == FLAT_NUM_CLASS_DECIMAL) {
        if (flat_number_stor(v) == FLAT_NUM_STOR_F32) {
            return (double)v->u.number.payload.f32;
        }
        return v->u.number.payload.f64;
    }
    if (whole_u64(v, &u) == 0) {
        return (double)u;
    }
    if (whole_i64(v, &i) == 0) {
        return (double)i;
    }
    return 0.0;
}

int flat_number_has_nan(const FlatValue *v) {
    if (!flat_number_is_number(v) || flat_number_class(v) != FLAT_NUM_CLASS_DECIMAL) {
        return 0;
    }
    if (flat_number_stor(v) == FLAT_NUM_STOR_F32) {
        return isnan((double)v->u.number.payload.f32) ? 1 : 0;
    }
    return isnan(v->u.number.payload.f64) ? 1 : 0;
}

/* Exact numeric ordering without routing 64-bit integers through double. */
static int compare_u64_double(uint64_t u, double d) {
    uint64_t whole;
    if (isnan(d)) return 0;
    if (d < 0.0) return 1;
    if (d >= 0x1p64) return -1;
    whole = (uint64_t)d; /* safe: 0 <= d < 2^64 */
    if (u < whole) return -1;
    if (u > whole) return 1;
    if (d > (double)whole) return -1; /* same integer part, positive fraction */
    return 0;
}

static int compare_i64_double(int64_t i, double d) {
    int64_t whole;
    if (isnan(d)) return 0;
    if (d < -0x1p63) return 1;
    if (d >= 0x1p63) return -1;
    whole = (int64_t)d; /* safe: -2^63 <= d < 2^63 */
    if (i < whole) return -1;
    if (i > whole) return 1;
    if (d > (double)whole) return -1;
    if (d < (double)whole) return 1;
    return 0;
}

static int compare_whole(const FlatValue *a, const FlatValue *b) {
    uint64_t au, bu;
    int64_t ai, bi;
    int a_unsigned = whole_u64(a, &au) == 0;
    int b_unsigned = whole_u64(b, &bu) == 0;
    if (a_unsigned && b_unsigned) return au < bu ? -1 : au > bu ? 1 : 0;
    if (!a_unsigned && !b_unsigned) {
        (void)whole_i64(a, &ai);
        (void)whole_i64(b, &bi);
        return ai < bi ? -1 : ai > bi ? 1 : 0;
    }
    if (a_unsigned) {
        (void)whole_i64(b, &bi);
        if (bi < 0) return 1;
        bu = (uint64_t)bi;
        return au < bu ? -1 : au > bu ? 1 : 0;
    }
    (void)whole_i64(a, &ai);
    if (ai < 0) return -1;
    au = (uint64_t)ai;
    return au < bu ? -1 : au > bu ? 1 : 0;
}

/* Returns -1/0/1 for a<b/a==b/a>b. */
static int flat_number_compare_value(const FlatValue *a, const FlatValue *b) {
    uint64_t u;
    int64_t i;
    double d;
    int a_whole = flat_number_class(a) == FLAT_NUM_CLASS_WHOLE;
    int b_whole = flat_number_class(b) == FLAT_NUM_CLASS_WHOLE;
    if (a_whole && b_whole) return compare_whole(a, b);
    if (!a_whole && !b_whole) {
        double da = flat_number_as_f64(a);
        double db = flat_number_as_f64(b);
        return da < db ? -1 : da > db ? 1 : 0;
    }
    if (a_whole) {
        d = flat_number_as_f64(b);
        if (whole_u64(a, &u) == 0) return compare_u64_double(u, d);
        (void)whole_i64(a, &i);
        return compare_i64_double(i, d);
    }
    d = flat_number_as_f64(a);
    if (whole_u64(b, &u) == 0) return -compare_u64_double(u, d);
    (void)whole_i64(b, &i);
    return -compare_i64_double(i, d);
}

int flat_number_equals(const FlatValue *a, const FlatValue *b) {
    if (!flat_number_is_number(a) || !flat_number_is_number(b) ||
        flat_number_has_nan(a) || flat_number_has_nan(b)) {
        return 0;
    }
    return flat_number_compare_value(a, b) == 0;
}

int flat_number_compare_rel(const FlatValue *a, const FlatValue *b, int op, char *panic,
                            size_t panic_sz) {
    int cmp;
    if (!flat_number_is_number(a) || !flat_number_is_number(b)) return 0;
    if (flat_number_has_nan(a) || flat_number_has_nan(b)) {
        if (panic && panic_sz > 0) snprintf(panic, panic_sz, "type.compare.nan");
        return -2;
    }
    cmp = flat_number_compare_value(a, b);
    switch (op) {
    case 0: return cmp == 0;
    case 1: return cmp >= 0;
    case 2: return cmp <= 0;
    case 3: return cmp > 0;
    case 4: return cmp < 0;
    default: return 0;
    }
}

static int fits_unsigned(uint64_t u, FlatSlotTypeKind kind) {
    switch (kind) {
    case FLAT_SLOT_TYPE_U8:
        return u <= 255u;
    case FLAT_SLOT_TYPE_U16:
        return u <= 65535u;
    case FLAT_SLOT_TYPE_U32:
        return u <= 4294967295u;
    case FLAT_SLOT_TYPE_U64:
        return 1;
    default:
        return 0;
    }
}

static int fits_signed(int64_t i, FlatSlotTypeKind kind) {
    switch (kind) {
    case FLAT_SLOT_TYPE_S8:
        return i >= -128 && i <= 127;
    case FLAT_SLOT_TYPE_S16:
        return i >= -32768 && i <= 32767;
    case FLAT_SLOT_TYPE_S32:
        return i >= (int64_t)INT32_MIN && i <= (int64_t)INT32_MAX;
    case FLAT_SLOT_TYPE_S64:
        return 1;
    default:
        return 0;
    }
}

static int f64_exact_in_f32(double d, float *out_f) {
    float f = (float)d;
    if (!isfinite(d)) {
        return 0;
    }
    if ((double)f != d) {
        return 0;
    }
    *out_f = f;
    return 1;
}

int flat_number_coerce_for_slot(const FlatValue *src, FlatSlotTypeKind kind, FlatValue *out,
                                char *panic, size_t panic_sz) {
    uint64_t u;
    int64_t i;
    double d;
    float f32;
    int64_t as_i;

    if (!src || !out || src->tag != VAL_NUMBER) {
        if (panic && panic_sz) {
            snprintf(panic, panic_sz, "type.assign.incompatible");
        }
        return -1;
    }

    switch (kind) {
    case FLAT_SLOT_TYPE_F64:
        flat_number_set_decimal_f64(out, flat_number_as_f64(src));
        return 0;
    case FLAT_SLOT_TYPE_F32:
        d = flat_number_as_f64(src);
        if (!f64_exact_in_f32(d, &f32)) {
            snprintf(panic, panic_sz, "type.assign.loss");
            return -1;
        }
        flat_number_set_decimal_f32(out, f32);
        return 0;
    case FLAT_SLOT_TYPE_U8:
    case FLAT_SLOT_TYPE_U16:
    case FLAT_SLOT_TYPE_U32:
    case FLAT_SLOT_TYPE_U64:
        if (flat_number_class(src) == FLAT_NUM_CLASS_WHOLE && whole_u64(src, &u) == 0) {
            if (!fits_unsigned(u, kind)) {
                snprintf(panic, panic_sz, "type.assign.range");
                return -1;
            }
            return flat_number_set_for_slot_kind(out, kind, u, 0, 0.0);
        }
        if (flat_number_class(src) == FLAT_NUM_CLASS_WHOLE && whole_i64(src, &i) == 0) {
            if (i < 0) {
                snprintf(panic, panic_sz, "type.assign.range");
                return -1;
            }
            u = (uint64_t)i;
            if (!fits_unsigned(u, kind)) {
                snprintf(panic, panic_sz, "type.assign.range");
                return -1;
            }
            return flat_number_set_for_slot_kind(out, kind, u, 0, 0.0);
        }
        d = flat_number_as_f64(src);
        if (flat_number_class(src) == FLAT_NUM_CLASS_DECIMAL) {
            if (!is_exact_uint(d, &u)) {
                snprintf(panic, panic_sz, "type.assign.incompatible");
                return -1;
            }
            /* Preserve the language's safe-integer rule for untyped decimal
               inputs. Full-width values must arrive as typed whole literals. */
            if (d > 9007199254740991.0) {
                snprintf(panic, panic_sz, "type.assign.range.i64");
                return -1;
            }
        } else {
            snprintf(panic, panic_sz, "type.assign.incompatible");
            return -1;
        }
        if (!fits_unsigned(u, kind)) {
            snprintf(panic, panic_sz, "type.assign.range");
            return -1;
        }
        return flat_number_set_for_slot_kind(out, kind, u, 0, 0.0);
    case FLAT_SLOT_TYPE_S8:
    case FLAT_SLOT_TYPE_S16:
    case FLAT_SLOT_TYPE_S32:
    case FLAT_SLOT_TYPE_S64:
        if (flat_number_class(src) == FLAT_NUM_CLASS_WHOLE && whole_i64(src, &i) == 0) {
            if (!fits_signed(i, kind)) {
                snprintf(panic, panic_sz, "type.assign.range");
                return -1;
            }
            return flat_number_set_for_slot_kind(out, kind, 0, i, 0.0);
        }
        if (flat_number_class(src) == FLAT_NUM_CLASS_WHOLE && whole_u64(src, &u) == 0) {
            if (u > (uint64_t)INT64_MAX) {
                snprintf(panic, panic_sz, "type.assign.range");
                return -1;
            }
            i = (int64_t)u;
            if (!fits_signed(i, kind)) {
                snprintf(panic, panic_sz, "type.assign.range");
                return -1;
            }
            return flat_number_set_for_slot_kind(out, kind, 0, i, 0.0);
        }
        d = flat_number_as_f64(src);
        if (flat_number_class(src) == FLAT_NUM_CLASS_DECIMAL) {
            if (!is_exact_int(d, &as_i)) {
                snprintf(panic, panic_sz, "type.assign.incompatible");
                return -1;
            }
            if (fabs(d) > 9007199254740991.0) {
                snprintf(panic, panic_sz, "type.assign.range.i64");
                return -1;
            }
            i = as_i;
        } else {
            snprintf(panic, panic_sz, "type.assign.incompatible");
            return -1;
        }
        if (!fits_signed(i, kind)) {
            snprintf(panic, panic_sz, "type.assign.range");
            return -1;
        }
        return flat_number_set_for_slot_kind(out, kind, 0, i, 0.0);
    default:
        snprintf(panic, panic_sz, "type.assign.incompatible");
        return -1;
    }
}

static int is_unsigned_kind(FlatSlotTypeKind k) {
    return k >= FLAT_SLOT_TYPE_U8 && k <= FLAT_SLOT_TYPE_U64;
}

static int parse_digits_u64(const char *s, uint64_t *out) {
    uint64_t v = 0;
    if (!s || !*s) {
        return -1;
    }
    if (*s == '-') {
        return -1;
    }
    while (*s) {
        if (!isdigit((unsigned char)*s)) {
            return -1;
        }
        {
            uint64_t digit = (uint64_t)(*s - '0');
            if (v > (UINT64_MAX - digit) / 10u) {
                return -1;
            }
            v = v * 10u + digit;
        }
        s++;
    }
    *out = v;
    return 0;
}

static int parse_digits_i64(const char *s, int64_t *out) {
    int neg = 0;
    uint64_t u;
    const char *p = s;
    if (!p || !*p) {
        return -1;
    }
    if (*p == '-') {
        neg = 1;
        p++;
        if (!*p) {
            return -1;
        }
    } else if (*p == '+') {
        p++;
    }
    if (parse_digits_u64(p, &u) != 0) {
        return -1;
    }
    if (!neg) {
        if (u > (uint64_t)INT64_MAX) {
            return -1;
        }
        *out = (int64_t)u;
        return 0;
    }
    if (u > (uint64_t)INT64_MAX + 1u) {
        return -1;
    }
    if (u == (uint64_t)INT64_MAX + 1u) {
        *out = INT64_MIN;
    } else {
        *out = -(int64_t)u;
    }
    return 0;
}

static int token_has_decimal_point(const char *token) {
    const char *p;
    for (p = token; *p; p++) {
        if (*p == '.') {
            return 1;
        }
        if (*p == 'e' || *p == 'E') {
            return 1;
        }
    }
    return 0;
}

int flat_number_parse_untyped(const char *token, FlatValue *out) {
    char *end = NULL;
    double d;
    if (!token || !out) {
        return -1;
    }
    while (isspace((unsigned char)*token)) {
        token++;
    }
    d = strtod(token, &end);
    if (end == token || (end && *end != '\0')) {
        return -1;
    }
    flat_number_set_decimal_f64(out, d);
    return 0;
}

int flat_number_parse_for_slot_kind(const char *token, FlatSlotTypeKind kind, FlatValue *out,
                                    char *panic, size_t panic_sz) {
    uint64_t u;
    int64_t i;
    char *end = NULL;
    double d;
    const char *p = token;

    if (!token || !out) {
        return -1;
    }
    while (isspace((unsigned char)*p)) {
        p++;
    }

    if (is_unsigned_kind(kind)) {
        if (token_has_decimal_point(p)) {
            snprintf(panic, panic_sz, "type.io.expect");
            return -1;
        }
        if (parse_digits_u64(p, &u) != 0) {
            snprintf(panic, panic_sz, "type.io.expect");
            return -1;
        }
        if (!fits_unsigned(u, kind)) {
            snprintf(panic, panic_sz, "type.assign.range");
            return -1;
        }
        return flat_number_set_for_slot_kind(out, kind, u, 0, 0.0);
    }

    switch (kind) {
    case FLAT_SLOT_TYPE_S8:
    case FLAT_SLOT_TYPE_S16:
    case FLAT_SLOT_TYPE_S32:
    case FLAT_SLOT_TYPE_S64:
        if (token_has_decimal_point(p)) {
            snprintf(panic, panic_sz, "type.io.expect");
            return -1;
        }
        if (parse_digits_i64(p, &i) != 0) {
            snprintf(panic, panic_sz, "type.io.expect");
            return -1;
        }
        if (!fits_signed(i, kind)) {
            snprintf(panic, panic_sz, "type.assign.range");
            return -1;
        }
        return flat_number_set_for_slot_kind(out, kind, 0, i, 0.0);
    case FLAT_SLOT_TYPE_F32:
    case FLAT_SLOT_TYPE_F64:
        d = strtod(p, &end);
        if (end == p || (end && *end != '\0')) {
            snprintf(panic, panic_sz, "type.io.expect");
            return -1;
        }
        if (kind == FLAT_SLOT_TYPE_F32) {
            float f;
            if (!isfinite(d) || d > (double)FLT_MAX || d < -(double)FLT_MAX) {
                snprintf(panic, panic_sz, "type.assign.range");
                return -1;
            }
            f = (float)d;
            flat_number_set_decimal_f32(out, f);
        } else {
            flat_number_set_decimal_f64(out, d);
        }
        return 0;
    default:
        snprintf(panic, panic_sz, "type.io.expect");
        return -1;
    }
}

static int parse_suffix_kind(const char *spec, FlatSlotTypeKind *out) {
    FlatSlotType tmp;
    if (strcmp(spec, "f32") == 0) {
        *out = FLAT_SLOT_TYPE_F32;
        return 0;
    }
    if (strcmp(spec, "f64") == 0) {
        *out = FLAT_SLOT_TYPE_F64;
        return 0;
    }
    if (strcmp(spec, "u8") == 0) {
        *out = FLAT_SLOT_TYPE_U8;
        return 0;
    }
    if (strcmp(spec, "u16") == 0) {
        *out = FLAT_SLOT_TYPE_U16;
        return 0;
    }
    if (strcmp(spec, "u32") == 0) {
        *out = FLAT_SLOT_TYPE_U32;
        return 0;
    }
    if (strcmp(spec, "u64") == 0) {
        *out = FLAT_SLOT_TYPE_U64;
        return 0;
    }
    if (strcmp(spec, "s8") == 0) {
        *out = FLAT_SLOT_TYPE_S8;
        return 0;
    }
    if (strcmp(spec, "s16") == 0) {
        *out = FLAT_SLOT_TYPE_S16;
        return 0;
    }
    if (strcmp(spec, "s32") == 0) {
        *out = FLAT_SLOT_TYPE_S32;
        return 0;
    }
    if (strcmp(spec, "s64") == 0) {
        *out = FLAT_SLOT_TYPE_S64;
        return 0;
    }
    (void)tmp;
    return -1;
}

int flat_number_parse_literal_with_optional_suffix(const char *lit, FlatValue *out,
                                                   FlatSlotTypeKind *suffix_kind, int *had_suffix,
                                                   char *panic, size_t panic_sz) {
    char numbuf[128];
    char suffix[32];
    const char *colon;
    size_t nlen;
    FlatSlotTypeKind sk;

    if (had_suffix) {
        *had_suffix = 0;
    }
    if (!lit || !out) {
        return -1;
    }

    colon = strrchr(lit, ':');
    if (!colon || colon == lit) {
        return flat_number_parse_untyped(lit, out);
    }

    /* Reject schema-like or dynamic suffixes */
    if (colon[1] == '/') {
        snprintf(panic, panic_sz, "invalid.literal.suffix");
        return -1;
    }
    if (colon[1] == '<') {
        snprintf(panic, panic_sz, "invalid.literal.suffix");
        return -1;
    }

    nlen = (size_t)(colon - lit);
    if (nlen >= sizeof numbuf) {
        return -1;
    }
    memcpy(numbuf, lit, nlen);
    numbuf[nlen] = '\0';
    snprintf(suffix, sizeof suffix, "%s", colon + 1);

    if (strcmp(suffix, "b") == 0 || suffix[0] == 't' || strcmp(suffix, "o") == 0 ||
        strcmp(suffix, "l") == 0) {
        snprintf(panic, panic_sz, "invalid.literal.suffix");
        return -1;
    }

    if (parse_suffix_kind(suffix, &sk) != 0) {
        snprintf(panic, panic_sz, "invalid.literal.suffix");
        return -1;
    }

    if (had_suffix) {
        *had_suffix = 1;
    }
    if (suffix_kind) {
        *suffix_kind = sk;
    }

    return flat_number_parse_for_slot_kind(numbuf, sk, out, panic, panic_sz);
}

int flat_number_literal_suffix_matches_slot(const FlatSlotType *contract,
                                            FlatSlotTypeKind suffix_kind) {
    if (!contract || contract->kind == FLAT_SLOT_TYPE_NONE) {
        return 1;
    }
    return contract->kind == suffix_kind;
}

int flat_number_is_negative_literal_token(const char *token) {
    char *end = NULL;
    double d;
    if (!token) {
        return 0;
    }
    while (isspace((unsigned char)*token)) {
        token++;
    }
    if (token[0] == '-' && isdigit((unsigned char)token[1])) {
        return 1;
    }
    d = strtod(token, &end);
    if (end != token && *end == '\0' && d < 0.0) {
        return 1;
    }
    return 0;
}

int flat_number_is_negative_value(const FlatValue *v) {
    return flat_number_as_f64(v) < 0.0;
}

void flat_number_fprint_decimal(FILE *out, const FlatValue *v) {
    char buf[96];
    double d;

    if (!out || !flat_number_is_number(v)) {
        fputs("0", out);
        return;
    }
    if (flat_number_class(v) == FLAT_NUM_CLASS_WHOLE) {
        if (flat_number_stor(v) >= FLAT_NUM_STOR_S8 && flat_number_stor(v) <= FLAT_NUM_STOR_S64) {
            snprintf(buf, sizeof buf, "%" PRId64, v->u.number.payload.i64);
        } else {
            snprintf(buf, sizeof buf, "%" PRIu64, v->u.number.payload.u64);
        }
        fputs(buf, out);
        return;
    }
    if (flat_number_stor(v) == FLAT_NUM_STOR_F32) {
        snprintf(buf, sizeof buf, "%.9g", (double)v->u.number.payload.f32);
    } else {
        d = v->u.number.payload.f64;
        if (isnan(d)) {
            fputs("NaN", out);
            return;
        }
        if (isinf(d)) {
            fputs(d > 0 ? "inf" : "-inf", out);
            return;
        }
        snprintf(buf, sizeof buf, "%.17g", d);
    }
    fputs(buf, out);
}
