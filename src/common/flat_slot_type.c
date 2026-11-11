#include "flat_slot_type.h"
#include "flat_schema.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_unsigned_type(const char *spec, FlatSlotType *out) {
    if (strncmp(spec, "u8", 3) == 0 && spec[2] == '\0') {
        out->kind = FLAT_SLOT_TYPE_U8;
        return 0;
    }
    if (strncmp(spec, "u16", 4) == 0 && spec[3] == '\0') {
        out->kind = FLAT_SLOT_TYPE_U16;
        return 0;
    }
    if (strncmp(spec, "u32", 4) == 0 && spec[3] == '\0') {
        out->kind = FLAT_SLOT_TYPE_U32;
        return 0;
    }
    if (strncmp(spec, "u64", 4) == 0 && spec[3] == '\0') {
        out->kind = FLAT_SLOT_TYPE_U64;
        return 0;
    }
    if (strncmp(spec, "s8", 3) == 0 && spec[2] == '\0') {
        out->kind = FLAT_SLOT_TYPE_S8;
        return 0;
    }
    if (strncmp(spec, "s16", 4) == 0 && spec[3] == '\0') {
        out->kind = FLAT_SLOT_TYPE_S16;
        return 0;
    }
    if (strncmp(spec, "s32", 4) == 0 && spec[3] == '\0') {
        out->kind = FLAT_SLOT_TYPE_S32;
        return 0;
    }
    if (strncmp(spec, "s64", 4) == 0 && spec[3] == '\0') {
        out->kind = FLAT_SLOT_TYPE_S64;
        return 0;
    }
  return -1;
}

static int parse_type_spec(const char *spec, FlatSlotType *out) {
    const char *p;
    int n;

    if (!spec || !spec[0] || !out) {
        return -1;
    }
    memset(out, 0, sizeof *out);
    if (strcmp(spec, "o") == 0) {
        out->kind = FLAT_SLOT_TYPE_O;
        return 0;
    }
    if (strcmp(spec, "l") == 0) {
        out->kind = FLAT_SLOT_TYPE_L;
        return 0;
    }
    if (strcmp(spec, "b") == 0) {
        out->kind = FLAT_SLOT_TYPE_B;
        return 0;
    }
    if (strcmp(spec, "f32") == 0) {
        out->kind = FLAT_SLOT_TYPE_F32;
        return 0;
    }
    if (strcmp(spec, "f64") == 0) {
        out->kind = FLAT_SLOT_TYPE_F64;
        return 0;
    }
    if (strcmp(spec, "t") == 0) {
        out->kind = FLAT_SLOT_TYPE_T;
        out->aux = FLAT_SLOT_TYPE_AUX_T_DEFAULT;
        return 0;
    }
    if (spec[0] == 't' && isdigit((unsigned char)spec[1])) {
        n = atoi(spec + 1);
        if (n <= 0 || n > 127) {
            return -1;
        }
        p = spec + 1;
        while (*p && isdigit((unsigned char)*p)) {
            p++;
        }
        if (*p != '\0') {
            return -1;
        }
        out->kind = FLAT_SLOT_TYPE_TN;
        out->aux = (uint8_t)n;
        return 0;
    }
    return parse_unsigned_type(spec, out);
}

int flat_bind_parse_lhs(const char *lhs, char *name_out, size_t name_sz, FlatSlotType *type_out,
                        int *has_type_out) {
    const char *colon;
    size_t nlen;

    if (!lhs || !name_out || name_sz == 0 || !type_out || !has_type_out) {
        return -1;
    }
    *has_type_out = 0;
    flat_slot_type_clear(type_out);
    while (*lhs && isspace((unsigned char)*lhs)) {
        lhs++;
    }
    colon = strchr(lhs, ':');
    if (!colon) {
        snprintf(name_out, name_sz, "%s", lhs);
        {
            char *t = name_out + strlen(name_out);
            while (t > name_out && isspace((unsigned char)t[-1])) {
                *--t = '\0';
            }
        }
        return name_out[0] ? 0 : -1;
    }
    nlen = (size_t)(colon - lhs);
    while (nlen > 0 && isspace((unsigned char)lhs[nlen - 1])) {
        nlen--;
    }
    if (nlen == 0 || nlen >= name_sz) {
        return -1;
    }
    memcpy(name_out, lhs, nlen);
    name_out[nlen] = '\0';
    {
        const char *spec = colon + 1;
        while (*spec && isspace((unsigned char)*spec)) {
            spec++;
        }
        if (*spec == '\0') {
            return -1;
        }
        if (flat_slot_type_parse_spec(spec, type_out, NULL, NULL, 0) != 0) {
            return -1;
        }
        *has_type_out = 1;
    }
    return 0;
}

int flat_slot_type_parse_spec(const char *spec, FlatSlotType *out, const FlatSchemaCatalog *cat,
                              char *err, size_t err_sz) {
    char sname[FLAT_SCHEMA_NAME_MAX + 1];
    const char *rest;
    int sid;

    if (!spec || !out) {
        return -1;
    }
    flat_slot_type_clear(out);
    if (spec[0] == '<') {
        const char *p = spec + 1;
        const char *end = strchr(p, '>');
        if (!end) {
            if (err && err_sz) {
                snprintf(err, err_sz, "invalid schema type: %s", spec);
            }
            return -1;
        }
        if ((size_t)(end - p) >= sizeof sname) {
            return -1;
        }
        memcpy(sname, p, (size_t)(end - p));
        sname[end - p] = '\0';
        rest = end + 1;
        if (!cat) {
            if (err && err_sz) {
                snprintf(err, err_sz, "schema type requires catalog: %s", spec);
            }
            return -1;
        }
        sid = flat_schema_find_id(cat, sname);
        if (sid < 0) {
            if (err && err_sz) {
                snprintf(err, err_sz, "type.schema.unknown: %s", sname);
            }
            return -1;
        }
        if (rest[0] == '[') {
            if (err && err_sz) {
                snprintf(err, err_sz, "list schema bind not supported: %s", spec);
            }
            return -1;
        }
        while (*rest && isspace((unsigned char)*rest)) {
            rest++;
        }
        if (*rest != '\0') {
            if (err && err_sz) {
                snprintf(err, err_sz, "invalid schema type: %s", spec);
            }
            return -1;
        }
        out->kind = FLAT_SLOT_TYPE_SCHEMA;
        out->schema_id = (uint16_t)sid;
        return 0;
    }
    if (spec[0] == '/') {
        if (err && err_sz) {
            snprintf(err, err_sz, "typed dynamic bind not supported: %s", spec);
        }
        return -1;
    }
    return parse_type_spec(spec, out) != 0 ? -1 : 0;
}

int flat_bind_validate_t1_lhs(const char *lhs, char *err, size_t err_sz) {
    const char *colon;
    const char *spec;

    if (!lhs) {
        return -1;
    }
    colon = strchr(lhs, ':');
    if (!colon) {
        return 0;
    }
    spec = colon + 1;
    while (*spec && isspace((unsigned char)*spec)) {
        spec++;
    }
    if (*spec == '/' || *spec == '<') {
        snprintf(err, err_sz, "typed schema/dynamic bind not supported in T1: %s", lhs);
        return -1;
    }
    {
        FlatSlotType ty;
        char name[FLAT_VAR_NAME_MAX + 1];
        int has;

        if (flat_bind_parse_lhs(lhs, name, sizeof name, &ty, &has) != 0 || !has) {
            snprintf(err, err_sz, "invalid typed bind lhs: %s", lhs);
            return -1;
        }
    }
    return 0;
}

int flat_slot_type_equals(const FlatSlotType *a, const FlatSlotType *b) {
    if (!a || !b) {
        return 0;
    }
    if (a->kind != b->kind) {
        return 0;
    }
    if (a->kind == FLAT_SLOT_TYPE_TN || a->kind == FLAT_SLOT_TYPE_T) {
        return a->aux == b->aux;
    }
    if (a->kind == FLAT_SLOT_TYPE_SCHEMA) {
        return a->schema_id == b->schema_id;
    }
    return 1;
}

void flat_slot_type_clear(FlatSlotType *t) {
    if (t) {
        memset(t, 0, sizeof *t);
    }
}

static int is_int_in_range(double d, long long minv, long long maxv) {
    double ip;
    if (isnan(d) || isinf(d)) {
        return 0;
    }
    if (modf(d, &ip) != 0.0) {
        return 0;
    }
    if (ip < (double)minv || ip > (double)maxv) {
        return 0;
    }
    return 1;
}

static size_t value_string_bytes(const FlatValue *val, const FlatStringPool *pool,
                                 const uint8_t **out_ptr) {
    if (!val) {
        return 0;
    }
    if (val->tag == VAL_STR_INLINE) {
        if (out_ptr) {
            *out_ptr = (const uint8_t *)val->u.ins.bytes;
        }
        return val->u.ins.len;
    }
    if (val->tag == VAL_STR_REF && pool) {
        size_t len = 0;
        const uint8_t *p = flat_pool_read(pool, val->u.ref_id, &len);
        if (out_ptr) {
            *out_ptr = p;
        }
        return p ? len : 0;
    }
    return 0;
}

int flat_slot_type_check_value(const FlatSlotType *contract, const FlatValue *val,
                               FlatStringPool *pool, FlatCollectionPool *coll,
                               const FlatSchemaCatalog *schemas, FlatValue *inout_val,
                               char *panic, size_t panic_sz) {
    double d;
    size_t slen;

    if (!contract || contract->kind == FLAT_SLOT_TYPE_NONE) {
        return 0;
    }
    if (!val) {
        snprintf(panic, panic_sz, "type.assign.incompatible");
        return -1;
    }
    switch (contract->kind) {
    case FLAT_SLOT_TYPE_B:
        if (val->tag != VAL_BOOL) {
            snprintf(panic, panic_sz, "type.assign.incompatible");
            return -1;
        }
        return 0;
    case FLAT_SLOT_TYPE_L:
        if (val->tag != VAL_LIST) {
            snprintf(panic, panic_sz, "type.assign.incompatible");
            return -1;
        }
        return 0;
    case FLAT_SLOT_TYPE_O:
        if (val->tag != VAL_OBJECT) {
            snprintf(panic, panic_sz, "type.assign.incompatible");
            return -1;
        }
        return 0;
    case FLAT_SLOT_TYPE_SCHEMA:
        if (!schemas || !coll || !inout_val) {
            snprintf(panic, panic_sz, "type.assign.incompatible");
            return -1;
        }
        *inout_val = *val;
        return flat_schema_bind_value((const FlatSchemaCatalog *)schemas, contract->schema_id,
                                      inout_val, pool, coll, panic, panic_sz);
    case FLAT_SLOT_TYPE_F32:
    case FLAT_SLOT_TYPE_F64:
        if (val->tag != VAL_NUMBER) {
            snprintf(panic, panic_sz, "type.assign.incompatible");
            return -1;
        }
        return 0;
    case FLAT_SLOT_TYPE_T:
    case FLAT_SLOT_TYPE_TN:
        slen = value_string_bytes(val, pool, NULL);
        if (val->tag != VAL_STR_INLINE && val->tag != VAL_STR_REF) {
            snprintf(panic, panic_sz, "type.assign.incompatible");
            return -1;
        }
        if (slen > (size_t)contract->aux) {
            snprintf(panic, panic_sz, "type.assign.string.len");
            return -1;
        }
        return 0;
    case FLAT_SLOT_TYPE_U8:
        if (val->tag != VAL_NUMBER) {
            snprintf(panic, panic_sz, "type.assign.incompatible");
            return -1;
        }
        d = val->u.number;
        if (!is_int_in_range(d, 0, 255)) {
            snprintf(panic, panic_sz, "type.assign.range");
            return -1;
        }
        return 0;
    case FLAT_SLOT_TYPE_U16:
        if (val->tag != VAL_NUMBER) {
            snprintf(panic, panic_sz, "type.assign.incompatible");
            return -1;
        }
        if (!is_int_in_range(val->u.number, 0, 65535)) {
            snprintf(panic, panic_sz, "type.assign.range");
            return -1;
        }
        return 0;
    case FLAT_SLOT_TYPE_U32:
        if (val->tag != VAL_NUMBER) {
            snprintf(panic, panic_sz, "type.assign.incompatible");
            return -1;
        }
        if (!is_int_in_range(val->u.number, 0, 4294967295LL)) {
            snprintf(panic, panic_sz, "type.assign.range");
            return -1;
        }
        return 0;
    case FLAT_SLOT_TYPE_U64:
        if (val->tag != VAL_NUMBER) {
            snprintf(panic, panic_sz, "type.assign.incompatible");
            return -1;
        }
        if (val->u.number < 0.0 || val->u.number > 18446744073709551615.0) {
            snprintf(panic, panic_sz, "type.assign.range");
            return -1;
        }
        if (modf(val->u.number, &d) != 0.0) {
            snprintf(panic, panic_sz, "type.assign.range");
            return -1;
        }
        return 0;
    case FLAT_SLOT_TYPE_S8:
        if (val->tag != VAL_NUMBER) {
            snprintf(panic, panic_sz, "type.assign.incompatible");
            return -1;
        }
        if (!is_int_in_range(val->u.number, -128, 127)) {
            snprintf(panic, panic_sz, "type.assign.range");
            return -1;
        }
        return 0;
    case FLAT_SLOT_TYPE_S16:
        if (val->tag != VAL_NUMBER) {
            snprintf(panic, panic_sz, "type.assign.incompatible");
            return -1;
        }
        if (!is_int_in_range(val->u.number, -32768, 32767)) {
            snprintf(panic, panic_sz, "type.assign.range");
            return -1;
        }
        return 0;
    case FLAT_SLOT_TYPE_S32:
        if (val->tag != VAL_NUMBER) {
            snprintf(panic, panic_sz, "type.assign.incompatible");
            return -1;
        }
        if (!is_int_in_range(val->u.number, -2147483648LL, 2147483647LL)) {
            snprintf(panic, panic_sz, "type.assign.range");
            return -1;
        }
        return 0;
    case FLAT_SLOT_TYPE_S64:
        if (val->tag != VAL_NUMBER) {
            snprintf(panic, panic_sz, "type.assign.incompatible");
            return -1;
        }
        if (!is_int_in_range(val->u.number, (long long)INT64_MIN, (long long)INT64_MAX)) {
            snprintf(panic, panic_sz, "type.assign.range");
            return -1;
        }
        return 0;
    default:
        snprintf(panic, panic_sz, "type.assign.incompatible");
        return -1;
    }
}
