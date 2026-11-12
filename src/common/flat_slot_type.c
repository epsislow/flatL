#include "flat_number.h"
#include "flat_slot_type.h"
#include "flat_schema.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
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

static int parse_bracket_list_max(const char *rest, uint16_t *max_out, const char **rest_out, char *err,
                                 size_t err_sz) {
    char *endptr;
    const char *p;
    unsigned long maxn;

    if (!rest || rest[0] != '[') {
        return -1;
    }
    p = rest + 1;
    if (*p == ']') {
        p++;
        maxn = 0;
    } else {
        if (!isdigit((unsigned char)*p)) {
            if (err && err_sz) {
                snprintf(err, err_sz, "invalid list bound");
            }
            return -1;
        }
        maxn = strtoul(p, &endptr, 10);
        if (endptr == p || maxn == 0 || maxn > UINT16_MAX || *endptr != ']') {
            if (err && err_sz) {
                snprintf(err, err_sz, "invalid list bound");
            }
            return -1;
        }
        p = endptr + 1;
    }
    if (*p != '\0') {
        if (err && err_sz) {
            snprintf(err, err_sz, "invalid list syntax");
        }
        return -1;
    }
    *max_out = (uint16_t)maxn;
    if (rest_out) {
        *rest_out = p;
    }
    return 0;
}

static int parse_type_spec(const char *spec, FlatSlotType *out) {
    if (!spec || !spec[0] || !out) {
        return -1;
    }
    if (strchr(spec, '[') != NULL) {
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
        unsigned long maxn;
        char *endp;

        maxn = strtoul(spec + 1, &endp, 10);
        if (endp == spec + 1 || *endp != '\0' || maxn == 0 || maxn > FLAT_SLOT_TYPE_AUX_T_MAX) {
            return -1;
        }
        out->kind = FLAT_SLOT_TYPE_TN;
        out->aux = (uint16_t)maxn;
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
        while (*rest && isspace((unsigned char)*rest)) {
            rest++;
        }
        if (*rest == '[') {
            uint16_t lmax = 0;
            if (parse_bracket_list_max(rest, &lmax, &rest, err, err_sz) != 0) {
                if (err && err_sz && !err[0]) {
                    snprintf(err, err_sz, "invalid schema list type: %s", spec);
                }
                return -1;
            }
            out->list_slot = 1;
            out->list_max = lmax;
        } else if (*rest != '\0') {
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
        const char *ex = spec + 1;
        while (*ex && isspace((unsigned char)*ex)) {
            ex++;
        }
        if (*ex == '\0') {
            if (err && err_sz) {
                snprintf(err, err_sz, "invalid dynamic type bind: %s", spec);
            }
            return -1;
        }
        if (strlen(ex) >= sizeof out->dynamic_expr) {
            if (err && err_sz) {
                snprintf(err, err_sz, "invalid dynamic type bind: %s", spec);
            }
            return -1;
        }
        snprintf(out->dynamic_expr, sizeof out->dynamic_expr, "%s", ex);
        out->kind = FLAT_SLOT_TYPE_DYNAMIC;
        return 0;
    }
    if (parse_type_spec(spec, out) != 0) {
        if (err && err_sz) {
            snprintf(err, err_sz, "invalid type: %s", spec);
        }
        return -1;
    }
    return 0;
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
        return a->schema_id == b->schema_id && a->list_max == b->list_max && a->list_slot == b->list_slot;
    }
    if (a->kind == FLAT_SLOT_TYPE_DYNAMIC) {
        return strcmp(a->dynamic_expr, b->dynamic_expr) == 0;
    }
    return 1;
}

void flat_slot_type_clear(FlatSlotType *t) {
    if (t) {
        memset(t, 0, sizeof *t);
    }
}

int flat_slot_type_format_spec(const FlatSlotType *t, const FlatSchemaCatalog *cat, char *out,
                               size_t out_sz) {
    const char *scalar = NULL;

    if (!out || out_sz == 0) {
        return -1;
    }
    if (!t || t->kind == FLAT_SLOT_TYPE_NONE) {
        snprintf(out, out_sz, "none");
        return 0;
    }
    if (t->kind == FLAT_SLOT_TYPE_DYNAMIC) {
        snprintf(out, out_sz, "/%s", t->dynamic_expr);
        return 0;
    }
    if (t->kind == FLAT_SLOT_TYPE_SCHEMA) {
        const char *sname = "<?>";
        if (cat && t->schema_id < (uint16_t)cat->count) {
            sname = cat->defs[t->schema_id].name;
        }
        if (t->list_slot || t->list_max > 0) {
            if (t->list_max > 0) {
                snprintf(out, out_sz, "<%s>[%u]", sname, (unsigned)t->list_max);
            } else {
                snprintf(out, out_sz, "<%s>[]", sname);
            }
        } else {
            snprintf(out, out_sz, "<%s>", sname);
        }
        return 0;
    }
    switch (t->kind) {
    case FLAT_SLOT_TYPE_U8:
        scalar = "u8";
        break;
    case FLAT_SLOT_TYPE_U16:
        scalar = "u16";
        break;
    case FLAT_SLOT_TYPE_U32:
        scalar = "u32";
        break;
    case FLAT_SLOT_TYPE_U64:
        scalar = "u64";
        break;
    case FLAT_SLOT_TYPE_S8:
        scalar = "s8";
        break;
    case FLAT_SLOT_TYPE_S16:
        scalar = "s16";
        break;
    case FLAT_SLOT_TYPE_S32:
        scalar = "s32";
        break;
    case FLAT_SLOT_TYPE_S64:
        scalar = "s64";
        break;
    case FLAT_SLOT_TYPE_F32:
        scalar = "f32";
        break;
    case FLAT_SLOT_TYPE_F64:
        scalar = "f64";
        break;
    case FLAT_SLOT_TYPE_B:
        scalar = "b";
        break;
    case FLAT_SLOT_TYPE_T:
        snprintf(out, out_sz, "t%u", (unsigned)FLAT_SLOT_TYPE_AUX_T_DEFAULT);
        return 0;
    case FLAT_SLOT_TYPE_TN:
        snprintf(out, out_sz, "t%u", (unsigned)t->aux);
        return 0;
    case FLAT_SLOT_TYPE_L:
        scalar = "l";
        break;
    case FLAT_SLOT_TYPE_O:
        scalar = "o";
        break;
    default:
        snprintf(out, out_sz, "none");
        return 0;
    }
    snprintf(out, out_sz, "%s", scalar);
    return 0;
}

/* Interim until VAL_I64/VAL_U64: integers must be exact in IEEE double (±(2^53−1)). */
#define FLAT_I64_SAFE_MAX 9007199254740991.0
#define FLAT_I64_SAFE_MIN (-9007199254740991.0)

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

static int is_u64_interim_value(double d) {
    int64_t qi;
    double ip;

    if (isnan(d) || isinf(d)) {
        return 0;
    }
    if (modf(d, &ip) != 0.0) {
        return 0;
    }
    if (d < 0.0 || d > FLAT_I64_SAFE_MAX) {
        return 0;
    }
    qi = (int64_t)d;
    if ((double)qi != d || qi < 0) {
        return 0;
    }
    return 1;
}

static int is_s64_interim_value(double d) {
    int64_t qi;
    double ip;

    if (isnan(d) || isinf(d)) {
        return 0;
    }
    if (modf(d, &ip) != 0.0) {
        return 0;
    }
    if (d < FLAT_I64_SAFE_MIN || d > FLAT_I64_SAFE_MAX) {
        return 0;
    }
    qi = (int64_t)d;
    if ((double)qi != d) {
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

int flat_slot_type_is_numeric(const FlatSlotType *t) {
    if (!t) {
        return 0;
    }
    switch (t->kind) {
    case FLAT_SLOT_TYPE_U8:
    case FLAT_SLOT_TYPE_U16:
    case FLAT_SLOT_TYPE_U32:
    case FLAT_SLOT_TYPE_U64:
    case FLAT_SLOT_TYPE_S8:
    case FLAT_SLOT_TYPE_S16:
    case FLAT_SLOT_TYPE_S32:
    case FLAT_SLOT_TYPE_S64:
    case FLAT_SLOT_TYPE_F32:
    case FLAT_SLOT_TYPE_F64:
        return 1;
    default:
        return 0;
    }
}

int flat_slot_type_is_scalar(const FlatSlotType *t) {
    if (!t) {
        return 0;
    }
    switch (t->kind) {
    case FLAT_SLOT_TYPE_U8:
    case FLAT_SLOT_TYPE_U16:
    case FLAT_SLOT_TYPE_U32:
    case FLAT_SLOT_TYPE_U64:
    case FLAT_SLOT_TYPE_S8:
    case FLAT_SLOT_TYPE_S16:
    case FLAT_SLOT_TYPE_S32:
    case FLAT_SLOT_TYPE_S64:
    case FLAT_SLOT_TYPE_F32:
    case FLAT_SLOT_TYPE_F64:
    case FLAT_SLOT_TYPE_B:
    case FLAT_SLOT_TYPE_T:
    case FLAT_SLOT_TYPE_TN:
        return 1;
    default:
        return 0;
    }
}

int flat_slot_type_check_value(const FlatSlotType *contract, const FlatValue *val,
                               FlatStringPool *pool, FlatCollectionPool *coll,
                               const FlatSchemaCatalog *schemas, FlatValue *inout_val,
                               char *panic, size_t panic_sz) {
    size_t slen;
    FlatSchemaField list_field;

    if (!contract || contract->kind == FLAT_SLOT_TYPE_NONE) {
        return 0;
    }
    if (contract->list_slot && contract->kind == FLAT_SLOT_TYPE_SCHEMA && schemas) {
        memset(&list_field, 0, sizeof list_field);
        list_field.flags = FLAT_SCH_FIELD_LIST;
        list_field.slot.kind = FLAT_SLOT_TYPE_SCHEMA;
        list_field.slot.schema_id = contract->schema_id;
        list_field.slot.list_max = contract->list_max;
        return flat_schema_check_field_value(&list_field, val, pool, coll, schemas, inout_val,
                                             panic, panic_sz);
    }
    if (contract->kind == FLAT_SLOT_TYPE_DYNAMIC) {
        snprintf(panic, panic_sz, "type.assign.incompatible");
        return -1;
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
        if (val->tag != VAL_NUMBER || !inout_val) {
            snprintf(panic, panic_sz, "type.assign.incompatible");
            return -1;
        }
        return flat_number_coerce_for_slot(val, contract->kind, inout_val, panic, panic_sz);
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
    case FLAT_SLOT_TYPE_U16:
    case FLAT_SLOT_TYPE_U32:
    case FLAT_SLOT_TYPE_U64:
    case FLAT_SLOT_TYPE_S8:
    case FLAT_SLOT_TYPE_S16:
    case FLAT_SLOT_TYPE_S32:
    case FLAT_SLOT_TYPE_S64:
        if (val->tag != VAL_NUMBER || !inout_val) {
            snprintf(panic, panic_sz, "type.assign.incompatible");
            return -1;
        }
        return flat_number_coerce_for_slot(val, contract->kind, inout_val, panic, panic_sz);
    default:
        snprintf(panic, panic_sz, "type.assign.incompatible");
        return -1;
    }
}
