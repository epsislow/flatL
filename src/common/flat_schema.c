#include "flat_schema.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *trim_inplace(char *s) {
    char *e;
    while (*s && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return s;
    }
    e = s + strlen(s) - 1;
    while (e > s && isspace((unsigned char)*e)) {
        *e-- = '\0';
    }
    return s;
}

int flat_schema_parse_header_line(const char *line, char *name_out, size_t name_sz) {
    const char *p;
    const char *end;
    size_t nlen;

    if (!line || !name_out || name_sz == 0) {
        return -1;
    }
    p = line;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != '<') {
        return -1;
    }
    p++;
    end = strchr(p, '>');
    if (!end) {
        return -1;
    }
    nlen = (size_t)(end - p);
    if (nlen == 0 || nlen >= name_sz) {
        return -1;
    }
    memcpy(name_out, p, nlen);
    name_out[nlen] = '\0';
    {
        const char *rest = end + 1;
        while (*rest && isspace((unsigned char)*rest)) {
            rest++;
        }
        if (*rest != ':') {
            return -1;
        }
        rest++;
        while (*rest && (isspace((unsigned char)*rest) || *rest == '\r')) {
            rest++;
        }
        if (*rest != '\0') {
            return -1;
        }
    }
    return 0;
}

int flat_schema_header_missing_colon(const char *line, char *name_out, size_t name_sz) {
    const char *p;
    const char *end;
    size_t nlen;
    char name_buf[FLAT_SCHEMA_NAME_MAX + 1];

    if (!line) {
        return 0;
    }
    p = line;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != '<') {
        return 0;
    }
    p++;
    end = strchr(p, '>');
    if (!end) {
        return 0;
    }
    nlen = (size_t)(end - p);
    if (nlen == 0 || nlen >= sizeof name_buf) {
        return 0;
    }
    memcpy(name_buf, p, nlen);
    name_buf[nlen] = '\0';
    {
        const char *rest = end + 1;
        while (*rest && isspace((unsigned char)*rest)) {
            rest++;
        }
        if (*rest == ':') {
            return 0;
        }
        if (*rest != '\0') {
            return 0;
        }
    }
    if (name_out && name_sz > 0) {
        snprintf(name_out, name_sz, "%s", name_buf);
    }
    return 1;
}

int flat_schema_body_line_ignorable(const char *trimmed) {
    char buf[FGIR_MAX_LINE + 8];
    char *colon;
    char *spec;

    if (!trimmed || !trimmed[0]) {
        return 1;
    }
    snprintf(buf, sizeof buf, "%s", trimmed);
    trimmed = trim_inplace(buf);
    if (!trimmed[0]) {
        return 1;
    }
    if (trimmed[0] == '-' && trimmed[1] == '-') {
        return 1;
    }
    colon = strchr(trimmed, ':');
    if (!colon) {
        return 1;
    }
    spec = trim_inplace(colon + 1);
    if (!spec[0]) {
        return 1;
    }
    return 0;
}

int flat_schema_parse_field_line(const char *line, char *key_out, size_t key_sz, char *spec_out,
                                 size_t spec_sz) {
    char buf[256];
    char *colon;
    char *key;
    char *spec;

    if (!line || !key_out || !spec_out) {
        return -1;
    }
    if (line[0] != ' ' && line[0] != '\t') {
        return -1;
    }
    snprintf(buf, sizeof buf, "%s", line);
    key = trim_inplace(buf);
    colon = strchr(key, ':');
    if (!colon) {
        return -1;
    }
    *colon = '\0';
    key = trim_inplace(key);
    spec = trim_inplace(colon + 1);
    if (!key[0] || !spec[0]) {
        return -1;
    }
    if (strlen(key) > key_sz - 1 || strlen(spec) > spec_sz - 1) {
        return -1;
    }
    snprintf(key_out, key_sz, "%s", key);
    snprintf(spec_out, spec_sz, "%s", spec);
    return 0;
}

int flat_schema_find_id(const FlatSchemaCatalog *cat, const char *name) {
    int i;
    if (!cat || !name) {
        return -1;
    }
    for (i = 0; i < cat->count; i++) {
        if (strcmp(cat->defs[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int parse_schema_ref_name(const char *spec, char *name_out, size_t name_sz, const char **rest) {
    const char *p;
    const char *end;
    size_t nlen;

    if (!spec || spec[0] != '<') {
        return -1;
    }
    p = spec + 1;
    end = strchr(p, '>');
    if (!end) {
        return -1;
    }
    nlen = (size_t)(end - p);
    if (nlen == 0 || nlen >= name_sz) {
        return -1;
    }
    memcpy(name_out, p, nlen);
    name_out[nlen] = '\0';
  if (rest) {
        *rest = end + 1;
    }
    return 0;
}

static int parse_field_spec(const char *spec, FlatSlotType *out, uint8_t *list_flag,
                            const FlatSchemaCatalog *cat, int self_schema_ix, char *err,
                            size_t err_sz) {
    char sname[FLAT_SCHEMA_NAME_MAX + 1];
    const char *rest;

    if (!spec || !spec[0] || !out) {
        snprintf(err, err_sz, "invalid field type");
        return -1;
    }
    if (list_flag) {
        *list_flag = 0;
    }
    flat_slot_type_clear(out);
    if (spec[0] == '<') {
        unsigned long maxn = 0;
        int is_list = 0;
        int sid;

        if (parse_schema_ref_name(spec, sname, sizeof sname, &rest) != 0) {
            snprintf(err, err_sz, "invalid field type: %s", spec);
            return -1;
        }
        if (*rest == '[') {
            char *endptr;
            const char *p = rest + 1;
            is_list = 1;
            if (*p == ']') {
                p++;
                maxn = 0;
            } else {
                if (!isdigit((unsigned char)*p)) {
                    snprintf(err, err_sz, "invalid list bound in: %s", spec);
                    return -1;
                }
                maxn = strtoul(p, &endptr, 10);
                if (endptr == p || maxn == 0 || maxn > UINT16_MAX || *endptr != ']') {
                    snprintf(err, err_sz, "invalid list bound in: %s", spec);
                    return -1;
                }
                p = endptr + 1;
            }
            if (*p != '\0') {
                snprintf(err, err_sz, "invalid list syntax: %s", spec);
                return -1;
            }
        } else if (*rest != '\0') {
            snprintf(err, err_sz, "invalid field type: %s", spec);
            return -1;
        }

        sid = flat_schema_find_id(cat, sname);
        if (sid < 0) {
            snprintf(err, err_sz, "type.schema.unknown: %s", sname);
            return -1;
        }
        if (!is_list && sid == self_schema_ix) {
            snprintf(err, err_sz, "type.schema.cycle: %s", sname);
            return -1;
        }
        out->kind = FLAT_SLOT_TYPE_SCHEMA;
        out->schema_id = (uint16_t)sid;
        out->list_max = (uint16_t)maxn;
        if (is_list && list_flag) {
            *list_flag = FLAT_SCH_FIELD_LIST;
        }
        return 0;
    }
    return flat_slot_type_parse_spec(spec, out, cat, err, err_sz);
}

static int schema_dep_dfs(const FlatSchemaCatalog *cat, int *state, int id, char *err,
                          size_t err_sz) {
    int i;
    const FlatSchemaDef *def;
    if (id < 0 || id >= cat->count) {
        return -1;
    }
    if (state[id] == 2) {
        return 0;
    }
    if (state[id] == 1) {
        snprintf(err, err_sz, "type.schema.cycle");
        return -1;
    }
    state[id] = 1;
    def = &cat->defs[id];
    for (i = 0; i < def->field_count; i++) {
        const FlatSchemaField *f = &def->fields[i];
        if (f->slot.kind == FLAT_SLOT_TYPE_SCHEMA && f->slot.schema_id < (uint16_t)cat->count) {
            if (schema_dep_dfs(cat, state, (int)f->slot.schema_id, err, err_sz) != 0) {
                return -1;
            }
        }
    }
    state[id] = 2;
    return 0;
}

int flat_schema_build_catalog(const FgirSchemaDecl *decls, int decl_count, FlatSchemaCatalog *out,
                              char *err, size_t err_sz) {
    int di, fi;

    if (!out) {
        return -1;
    }
    memset(out, 0, sizeof *out);
    if (decl_count <= 0) {
        return 0;
    }
    if (decl_count > FLAT_SCHEMA_MAX) {
        snprintf(err, err_sz, "too many schemas");
        return -1;
    }
    for (di = 0; di < decl_count; di++) {
        int prev;
        FlatSchemaDef *def = &out->defs[out->count];
        if (!decls[di].name[0]) {
            snprintf(err, err_sz, "type.schema.name.invalid");
            return -1;
        }
        for (prev = 0; prev < di; prev++) {
            if (strcmp(decls[prev].name, decls[di].name) == 0) {
                snprintf(err, err_sz, "type.schema.duplicate: %s", decls[di].name);
                return -1;
            }
        }
        snprintf(def->name, sizeof def->name, "%s", decls[di].name);
        def->field_count = 0;
        out->count++;
    }
    for (di = 0; di < decl_count; di++) {
        FlatSchemaDef *def = &out->defs[di];
        for (fi = 0; fi < decls[di].field_count; fi++) {
            FlatSchemaField *fld;
            uint8_t list_flag = 0;
            if (def->field_count >= FLAT_SCHEMA_FIELD_MAX) {
                snprintf(err, err_sz, "too many fields in schema %s", def->name);
                return -1;
            }
            {
                int prev;
                for (prev = 0; prev < def->field_count; prev++) {
                    if (strcmp(def->fields[prev].key, decls[di].field_keys[fi]) == 0) {
                        snprintf(err, err_sz, "type.schema.field.duplicate: %s.%s", def->name,
                                 decls[di].field_keys[fi]);
                        return -1;
                    }
                }
            }
            fld = &def->fields[def->field_count];
            snprintf(fld->key, sizeof fld->key, "%s", decls[di].field_keys[fi]);
            if (parse_field_spec(decls[di].field_specs[fi], &fld->slot, &list_flag, out, di, err,
                                 err_sz) != 0) {
                return -1;
            }
            fld->flags = list_flag;
            def->field_count++;
        }
    }
    {
        int *st = calloc((size_t)out->count, sizeof *st);
        int i;
        if (!st) {
            snprintf(err, err_sz, "out of memory");
            return -1;
        }
        for (i = 0; i < out->count; i++) {
            if (schema_dep_dfs(out, st, i, err, err_sz) != 0) {
                free(st);
                return -1;
            }
        }
        free(st);
    }
    return 0;
}

int flat_bind_parse_lhs_catalog(const char *lhs, char *name_out, size_t name_sz,
                                FlatSlotType *type_out, int *has_type_out,
                                const FlatSchemaCatalog *cat) {
    const char *colon;
    size_t nlen;
    char err[96];

    if (!lhs || !type_out || !has_type_out) {
        return -1;
    }
    *has_type_out = 0;
    flat_slot_type_clear(type_out);
    while (*lhs && isspace((unsigned char)*lhs)) {
        lhs++;
    }
    colon = strchr(lhs, ':');
    if (!colon) {
        if (!name_out || name_sz == 0) {
            return -1;
        }
        snprintf(name_out, name_sz, "%s", lhs);
        {
            char *t = name_out + strlen(name_out);
            while (t > name_out && isspace((unsigned char)t[-1])) {
                *--t = '\0';
            }
        }
        return name_out[0] ? 0 : -1;
    }
    if (!name_out || name_sz == 0) {
        return -1;
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
        if (flat_slot_type_parse_spec(spec, type_out, cat, err, sizeof err) != 0) {
            return -1;
        }
        *has_type_out = 1;
    }
    return 0;
}

int flat_bind_validate_lhs(const char *lhs, const FlatSchemaCatalog *cat, char *err, size_t err_sz) {
    const char *colon;
    const char *spec;
    char path_tmp[256];
    char root_bind[128];

    if (!lhs) {
        return -1;
    }
    if (flat_assign_lhs_has_path(lhs)) {
        if (flat_assign_path_build_expr(lhs, path_tmp, sizeof path_tmp, root_bind,
                                        sizeof root_bind) != 0) {
            snprintf(err, err_sz, "invalid typed bind lhs: %s", lhs);
            return -1;
        }
        return flat_bind_validate_lhs(root_bind, cat, err, err_sz);
    }
    colon = strchr(lhs, ':');
    if (!colon) {
        return 0;
    }
    spec = colon + 1;
    while (*spec && isspace((unsigned char)*spec)) {
        spec++;
    }
    if (*spec == '/') {
        FlatSlotType ty;
        char name[FLAT_VAR_NAME_MAX + 1];
        int has;
        const char *tyspec = spec;

        if (flat_slot_type_parse_spec(tyspec, &ty, cat, err, err_sz) != 0) {
            snprintf(err, err_sz, "invalid dynamic type bind: %s", lhs);
            return -1;
        }
        if (ty.kind != FLAT_SLOT_TYPE_DYNAMIC) {
            snprintf(err, err_sz, "invalid dynamic type bind: %s", lhs);
            return -1;
        }
        if (flat_bind_parse_lhs_catalog(lhs, name, sizeof name, &ty, &has, cat) != 0 || !has) {
            snprintf(err, err_sz, "invalid typed bind lhs: %s", lhs);
            return -1;
        }
        return 0;
    }
    {
        FlatSlotType ty;
        char name[FLAT_VAR_NAME_MAX + 1];
        int has;
        if (flat_bind_parse_lhs_catalog(lhs, name, sizeof name, &ty, &has, cat) != 0 || !has) {
            FlatSlotType dummy;
            char suberr[128];
            const char *tyspec = colon + 1;
            while (*tyspec && isspace((unsigned char)*tyspec)) {
                tyspec++;
            }
            if (flat_slot_type_parse_spec(tyspec, &dummy, cat, suberr, sizeof suberr) != 0) {
                snprintf(err, err_sz, "%s", suberr);
            } else {
                snprintf(err, err_sz, "invalid typed bind lhs: %s", lhs);
            }
            return -1;
        }
    }
    return 0;
}

int flat_schema_field_lookup(const FlatSchemaDef *def, const char *key,
                             const FlatSchemaField **out_field) {
    int i;
    if (!def || !key) {
        return 0;
    }
    for (i = 0; i < def->field_count; i++) {
        if (strcmp(def->fields[i].key, key) == 0) {
            if (out_field) {
                *out_field = &def->fields[i];
            }
            return 1;
        }
    }
    return 0;
}

const FlatSchemaDef *flat_schema_def_for_contract(const FlatSchemaCatalog *cat,
                                                  const FlatSlotType *contract) {
    if (!cat || !contract || contract->kind != FLAT_SLOT_TYPE_SCHEMA) {
        return NULL;
    }
    if (contract->schema_id >= (uint16_t)cat->count) {
        return NULL;
    }
    return &cat->defs[contract->schema_id];
}

int flat_schema_check_field_value(const FlatSchemaField *field, const FlatValue *val,
                                  FlatStringPool *pool, FlatCollectionPool *coll,
                                  const FlatSchemaCatalog *cat, FlatValue *inout_val,
                                  char *panic, size_t panic_sz) {
    FlatValue checked;
    int len;
    int i;

    if (!field || !val) {
        snprintf(panic, panic_sz, "type.assign.incompatible");
        return -1;
    }
    if (val->tag == VAL_NULL) {
        if (inout_val) {
            *inout_val = *val;
        }
        return 0;
    }
    if (field->flags & FLAT_SCH_FIELD_LIST) {
        FlatValue items[FLAT_LIST_MAX_ELEMS];
        uint32_t new_id;

        if (val->tag != VAL_LIST || !coll ||
            flat_list_len(coll, val->u.coll_id, &len) != 0) {
            snprintf(panic, panic_sz, "type.assign.incompatible");
            return -1;
        }
        if (field->slot.list_max > 0 && len > (int)field->slot.list_max) {
            snprintf(panic, panic_sz, "type.assign.list.len");
            return -1;
        }
        if (len > FLAT_LIST_MAX_ELEMS) {
            snprintf(panic, panic_sz, "type.assign.list.len");
            return -1;
        }
        for (i = 0; i < len; i++) {
            FlatValue el;
            if (flat_list_get(coll, val->u.coll_id, i, &el) != 0) {
                snprintf(panic, panic_sz, "type.assign.incompatible");
                return -1;
            }
            checked = el;
            if (flat_slot_type_check_value(&field->slot, &el, pool, coll, cat, &checked, panic,
                                           panic_sz) != 0) {
                return -1;
            }
            items[i] = checked;
        }
        if (flat_list_build(coll, pool, items, len, &new_id) != 0) {
            snprintf(panic, panic_sz, "type.assign.incompatible");
            return -1;
        }
        if (inout_val) {
            flat_value_set_list(inout_val, new_id);
        }
        return 0;
    }

    checked = *val;
    if (flat_slot_type_check_value(&field->slot, val, pool, coll, cat, &checked, panic,
                                   panic_sz) != 0) {
        return -1;
    }
    if (inout_val) {
        *inout_val = checked;
    }
    return 0;
}

int flat_schema_bind_value(const FlatSchemaCatalog *cat, uint16_t schema_id, FlatValue *io_val,
                           FlatStringPool *pool, FlatCollectionPool *coll, char *panic,
                           size_t panic_sz) {
    const FlatSchemaDef *def;
    FlatValue vals[FLAT_SCHEMA_FIELD_MAX];
    char keybuf[FLAT_SCHEMA_FIELD_MAX][FLAT_OBJ_KEY_MAX + 1];
    int present[FLAT_SCHEMA_FIELD_MAX] = {0};
    int n;
    int i;
    int olen;
    uint32_t new_id;

    if (!cat || schema_id >= (uint16_t)cat->count || !io_val || !pool || !coll) {
        snprintf(panic, panic_sz, "type.assign.incompatible");
        return -1;
    }
    def = &cat->defs[schema_id];
    if (io_val->tag != VAL_OBJECT) {
        snprintf(panic, panic_sz, "type.assign.incompatible");
        return -1;
    }
    if (def->field_count < 0 || def->field_count > FLAT_SCHEMA_FIELD_MAX ||
        flat_obj_len(coll, io_val->u.coll_id, &olen) != 0) {
        snprintf(panic, panic_sz, "type.assign.incompatible");
        return -1;
    }

    n = def->field_count;
    for (i = 0; i < n; i++) {
        snprintf(keybuf[i], sizeof keybuf[i], "%s", def->fields[i].key);
        flat_value_set_null(&vals[i]);
    }

    /* Validate and normalize each supplied field without consuming the input object. */
    for (i = 0; i < olen; i++) {
        const char *key;
        FlatValue value;
        const FlatSchemaField *field;
        int field_ix;

        if (flat_obj_entry_at(coll, io_val->u.coll_id, i, &key, &value) != 0) {
            snprintf(panic, panic_sz, "type.assign.incompatible");
            return -1;
        }
        if (!flat_schema_field_lookup(def, key, &field)) {
            snprintf(panic, panic_sz, "type.schema.field.unknown");
            return -1;
        }
        field_ix = (int)(field - def->fields);
        if (field_ix < 0 || field_ix >= n || present[field_ix]) {
            snprintf(panic, panic_sz, "type.assign.incompatible");
            return -1;
        }
        if (flat_schema_check_field_value(field, &value, pool, coll, cat, &vals[field_ix], panic,
                                          panic_sz) != 0) {
            return -1;
        }
        present[field_ix] = 1;
    }

    if (flat_obj_build(coll, pool, keybuf, vals, n, &new_id) != 0) {
        snprintf(panic, panic_sz, "type.assign.incompatible");
        return -1;
    }

    /* The caller owns the returned, currently-unbound value and retains it when binding it. */
    return flat_value_set_object(io_val, new_id);
}

static int read_u16(const uint8_t *p, uint16_t *out) {
    *out = (uint16_t)((unsigned)p[0] | ((unsigned)p[1] << 8));
    return 0;
}

size_t flat_fb_schm_section_size(const FlatSchemaCatalog *cat) {
    size_t n = 6;
    int i, j;
    if (!cat) {
        return n;
    }
    for (i = 0; i < cat->count; i++) {
        n += 2 + strlen(cat->defs[i].name);
        n += 2;
        for (j = 0; j < cat->defs[i].field_count; j++) {
            const FlatSchemaField *f = &cat->defs[i].fields[j];
            n += 2 + strlen(f->key) + 1 + 1 + 2 + 2 + 1;
        }
    }
    return n;
}

int flat_fb_write_schm_section(FILE *f, const FlatSchemaCatalog *cat) {
    uint32_t magic = FLAT_FB_SCHM_MAGIC;
    uint16_t count;
    int i, j;

    if (!f) {
        return -1;
    }
    count = cat ? (uint16_t)cat->count : 0;
    if (fwrite(&magic, 4, 1, f) != 1 || fwrite(&count, 2, 1, f) != 1) {
        return -1;
    }
    for (i = 0; i < (int)count; i++) {
        const FlatSchemaDef *d = &cat->defs[i];
        uint16_t nl = (uint16_t)strlen(d->name);
        uint16_t fc = (uint16_t)d->field_count;
        if (fwrite(&nl, 2, 1, f) != 1 || fwrite(d->name, 1, nl, f) != nl ||
            fwrite(&fc, 2, 1, f) != 1) {
            return -1;
        }
        for (j = 0; j < d->field_count; j++) {
            const FlatSchemaField *fld = &d->fields[j];
            uint16_t kl = (uint16_t)strlen(fld->key);
            uint8_t kind = (uint8_t)fld->slot.kind;
            uint8_t aux = fld->slot.aux;
            uint16_t sid = fld->slot.schema_id;
            uint16_t lmax = fld->slot.list_max;
            uint8_t flags = fld->flags;
            if (fwrite(&kl, 2, 1, f) != 1 || fwrite(fld->key, 1, kl, f) != kl) {
                return -1;
            }
            if (fwrite(&kind, 1, 1, f) != 1 || fwrite(&aux, 1, 1, f) != 1 ||
                fwrite(&sid, 2, 1, f) != 1 || fwrite(&lmax, 2, 1, f) != 1 ||
                fwrite(&flags, 1, 1, f) != 1) {
                return -1;
            }
        }
    }
    return 0;
}

static int schm_slot_kind_valid(FlatSlotTypeKind kind) {
    return kind >= FLAT_SLOT_TYPE_U8 && kind <= FLAT_SLOT_TYPE_SCHEMA;
}

static int schm_validate_catalog(const FlatSchemaCatalog *cat) {
    int i;
    int j;
    int k;

    if (!cat || cat->count < 0 || cat->count > FLAT_SCHEMA_MAX) {
        return -1;
    }
    for (i = 0; i < cat->count; i++) {
        const FlatSchemaDef *def = &cat->defs[i];
        if (def->name[0] == '\0') {
            return -1;
        }
        if (def->field_count < 0 || def->field_count > FLAT_SCHEMA_FIELD_MAX) {
            return -1;
        }
        for (j = 0; j < def->field_count; j++) {
            const FlatSchemaField *fld = &def->fields[j];
            if (fld->key[0] == '\0') {
                return -1;
            }
            if (!schm_slot_kind_valid(fld->slot.kind)) {
                return -1;
            }
            if (fld->flags != FLAT_SCH_FIELD_NONE && fld->flags != FLAT_SCH_FIELD_LIST) {
                return -1;
            }
            if (fld->slot.kind == FLAT_SLOT_TYPE_TN) {
                if (fld->slot.aux == 0 || fld->slot.aux > FLAT_SLOT_TYPE_AUX_T_DEFAULT) {
                    return -1;
                }
            } else if (fld->slot.kind == FLAT_SLOT_TYPE_T) {
                if (fld->slot.aux != 0 && fld->slot.aux != FLAT_SLOT_TYPE_AUX_T_DEFAULT) {
                    return -1;
                }
            } else if (fld->slot.aux != 0) {
                return -1;
            }
            if (fld->slot.kind == FLAT_SLOT_TYPE_SCHEMA &&
                fld->slot.schema_id >= (uint16_t)cat->count) {
                return -1;
            }
            for (k = j + 1; k < def->field_count; k++) {
                if (strcmp(fld->key, def->fields[k].key) == 0) {
                    return -1;
                }
            }
        }
    }
    return 0;
}

int flat_fb_parse_schm_section(const uint8_t *sec, size_t sec_len, FlatSchemaCatalog *out) {
    size_t pos = 0;
    uint32_t magic;
    uint16_t scount;
    int si;

    if (!sec || !out) {
        return -1;
    }
    memset(out, 0, sizeof *out);
    if (sec_len < 6) {
        return -1;
    }
    magic = (uint32_t)sec[0] | ((uint32_t)sec[1] << 8) | ((uint32_t)sec[2] << 16) |
            ((uint32_t)sec[3] << 24);
    if (magic != FLAT_FB_SCHM_MAGIC) {
        return -1;
    }
    pos = 4;
    read_u16(sec + pos, &scount);
    pos += 2;
    if (scount > FLAT_SCHEMA_MAX) {
        return -1;
    }
    for (si = 0; si < (int)scount; si++) {
        uint16_t nl;
        uint16_t fc;
        int fi;
        FlatSchemaDef *def;
        if (pos + 2 > sec_len) {
            return -1;
        }
        read_u16(sec + pos, &nl);
        pos += 2;
        if (pos + nl + 2 > sec_len || nl > FLAT_SCHEMA_NAME_MAX) {
            return -1;
        }
        def = &out->defs[out->count++];
        memcpy(def->name, sec + pos, nl);
        def->name[nl] = '\0';
        pos += nl;
        read_u16(sec + pos, &fc);
        pos += 2;
        if (fc > FLAT_SCHEMA_FIELD_MAX) {
            return -1;
        }
        def->field_count = (int)fc;
        for (fi = 0; fi < (int)fc; fi++) {
            uint16_t kl;
            FlatSchemaField *fld = &def->fields[fi];
            if (pos + 2 > sec_len) {
                return -1;
            }
            read_u16(sec + pos, &kl);
            pos += 2;
            if (pos + kl + 7 > sec_len || kl > FLAT_OBJ_KEY_MAX) {
                return -1;
            }
            memcpy(fld->key, sec + pos, kl);
            fld->key[kl] = '\0';
            pos += kl;
            fld->slot.kind = (FlatSlotTypeKind)sec[pos++];
            fld->slot.aux = sec[pos++];
            read_u16(sec + pos, &fld->slot.schema_id);
            pos += 2;
            read_u16(sec + pos, &fld->slot.list_max);
            pos += 2;
            fld->flags = sec[pos++];
        }
    }
    if (pos != sec_len) {
        memset(out, 0, sizeof *out);
        return -1;
    }
    if (out->count != (int)scount) {
        memset(out, 0, sizeof *out);
        return -1;
    }
    if (schm_validate_catalog(out) != 0) {
        memset(out, 0, sizeof *out);
        return -1;
    }
    return 0;
}
