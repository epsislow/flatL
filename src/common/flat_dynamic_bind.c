#include "flat_schema.h"
#include "flat_vcx.h"

#include <stdio.h>
#include <string.h>

static int type_spec_from_value(const FlatValue *v, const FlatStringPool *pool, char *buf,
                                size_t buf_sz) {
    if (!v || !buf || buf_sz == 0) {
        return -1;
    }
    if (v->tag == VAL_STR_INLINE) {
        if (v->u.ins.len + 1 > buf_sz) {
            return -1;
        }
        memcpy(buf, v->u.ins.bytes, v->u.ins.len);
        buf[v->u.ins.len] = '\0';
        return 0;
    }
    if (v->tag == VAL_STR_REF && pool) {
        size_t len = 0;
        const uint8_t *p = flat_pool_read(pool, v->u.ref_id, &len);
        if (!p || len + 1 > buf_sz) {
            return -1;
        }
        memcpy(buf, p, len);
        buf[len] = '\0';
        return 0;
    }
    return -1;
}

int flat_bind_resolve_lhs_runtime(const char *lhs, char *name_out, size_t name_sz,
                                  FlatSlotType *type_out, int *has_type_out, int *is_dynamic_out,
                                  FlatValue *vars, int var_count, const char (*var_names)[64],
                                  FlatStringPool *pool, FlatCollectionPool *coll,
                                  const FlatSchemaCatalog *cat, FlatVcxRuntime *vcx,
                                  char *panic, size_t panic_sz) {
    FlatSlotType raw;
    char spec_buf[256];
    FlatValue type_v;
    char type_str[256];
    char schema_name[FLAT_SCHEMA_NAME_MAX + 1];

    if (!lhs || !type_out || !has_type_out || !is_dynamic_out) {
        return -1;
    }
    *is_dynamic_out = 0;
    if (flat_bind_parse_lhs_catalog(lhs, name_out, name_sz, &raw, has_type_out, cat) != 0) {
        snprintf(panic, panic_sz, "type.assign.incompatible");
        return -1;
    }
    if (!*has_type_out) {
        flat_slot_type_clear(type_out);
        return 0;
    }
    if (raw.kind != FLAT_SLOT_TYPE_DYNAMIC) {
        *type_out = raw;
        return 0;
    }
    *is_dynamic_out = 1;
    snprintf(spec_buf, sizeof spec_buf, "%s", raw.dynamic_expr);
    if (flat_value_parse_assign_rhs(spec_buf, &type_v, pool, coll, vars, var_count, var_names) !=
        0) {
        snprintf(panic, panic_sz, "type.assign.schema.unknown");
        return -1;
    }
    if (type_spec_from_value(&type_v, pool, type_str, sizeof type_str) != 0 ||
        type_str[0] == '\0') {
        snprintf(panic, panic_sz, "type.assign.schema.unknown");
        return -1;
    }
    if (flat_slot_type_parse_spec(type_str, type_out, cat, panic, panic_sz) != 0) {
        if (panic[0] == '\0') {
            snprintf(panic, panic_sz, "type.assign.schema.unknown");
        } else if (strncmp(panic, "type.schema.unknown", 19) == 0) {
            snprintf(panic, panic_sz, "type.assign.schema.unknown");
        }
        return -1;
    }
    if (type_out->kind == FLAT_SLOT_TYPE_SCHEMA && cat && vcx && type_str[0] == '<') {
        const char *p = type_str + 1;
        const char *end = strchr(p, '>');
        size_t n;

        if (end && end > p) {
            n = (size_t)(end - p);
            if (n < sizeof schema_name) {
                memcpy(schema_name, p, n);
                schema_name[n] = '\0';
                if (flat_vcx_dynamic_schema_permit(vcx, schema_name, panic, panic_sz) != 0) {
                    return -1;
                }
            }
        }
    }
    return 0;
}
