#include "flat_obj_path.h"
#include "flat_schema.h"
#include "flat_slot_type.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *skip_ws(const char *p) {
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    return p;
}

static int path_has_whitespace(const char *path) {
    const char *p;
    if (!path) {
        return 1;
    }
    for (p = path; *p; p++) {
        if (isspace((unsigned char)*p)) {
            return 1;
        }
    }
    return 0;
}

static int parse_quoted_segment(const char **p, char *out, size_t out_max) {
    char q;
    size_t n = 0;
    const char *s = *p;

    if (*s != '\'' && *s != '"') {
        return -1;
    }
    q = *s++;
    while (*s && *s != q) {
        if (n + 1 >= out_max) {
            return -1;
        }
        out[n++] = *s++;
    }
    if (*s != q) {
        return -1;
    }
    s++;
    out[n] = '\0';
    *p = s;
    return 0;
}

static int parse_ident_segment(const char **p, char *out, size_t out_max) {
    const char *s = *p;
    size_t n = 0;

    if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || *s == '_')) {
        return -1;
    }
    while ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') ||
           *s == '_') {
        if (n + 1 >= out_max) {
            return -1;
        }
        out[n++] = *s++;
    }
    out[n] = '\0';
    *p = s;
    return 0;
}

int flat_obj_path_parse_cstr(const char *path, FlatObjPathSegs *out) {
    const char *p;
    int seg = 0;

    if (!path || !out) {
        return -1;
    }
    if (path[0] == '\0' || path_has_whitespace(path)) {
        return -1;
    }
    out->n = 0;
    p = path;
    for (;;) {
        if (seg >= FLAT_OBJ_PATH_MAX_SEG) {
            return -1;
        }
        if (*p == '\'' || *p == '"') {
            if (parse_quoted_segment(&p, out->keys[seg], sizeof out->keys[seg]) != 0) {
                return -1;
            }
        } else {
            if (parse_ident_segment(&p, out->keys[seg], sizeof out->keys[seg]) != 0) {
                return -1;
            }
        }
        seg++;
        if (*p == '\0') {
            out->n = seg;
            return 0;
        }
        if (*p != '.') {
            return -1;
        }
        p++;
        if (*p == '\0' || *p == '.') {
            return -1;
        }
    }
}

int flat_obj_path_from_value(const FlatValue *v, const FlatStringPool *pool, FlatObjPathSegs *out) {
    char buf[4096];

    if (!v || !out) {
        return -1;
    }
    if (flat_value_get_cstr(v, pool, buf, sizeof buf) != 0) {
        return -1;
    }
    return flat_obj_path_parse_cstr(buf, out);
}

int flat_obj_get_path(const FlatCollectionPool *pool, uint32_t obj_id, const FlatObjPathSegs *path,
                      FlatValue *out) {
    FlatValue cur;
    int i;

    if (!pool || !path || !out || path->n <= 0) {
        return -1;
    }
    flat_value_set_object(&cur, obj_id);
    for (i = 0; i < path->n; i++) {
        FlatValue next;
        if (cur.tag != VAL_OBJECT) {
            return -1;
        }
        if (flat_obj_get(pool, cur.u.coll_id, path->keys[i], &next) != 0) {
            return -1;
        }
        cur = next;
    }
    *out = cur;
    return 0;
}

int flat_obj_has_path(const FlatCollectionPool *pool, uint32_t obj_id, const FlatObjPathSegs *path,
                      int *out_has) {
    FlatValue cur;
    int i;

    if (!pool || !path || !out_has || path->n <= 0) {
        return -1;
    }
    flat_value_set_object(&cur, obj_id);
    for (i = 0; i < path->n; i++) {
        FlatValue next;
        if (cur.tag != VAL_OBJECT) {
            *out_has = 0;
            return 0;
        }
        if (flat_obj_get(pool, cur.u.coll_id, path->keys[i], &next) != 0) {
            *out_has = 0;
            return 0;
        }
        cur = next;
    }
    *out_has = 1;
    return 0;
}

static int set_path_rec(FlatCollectionPool *pool, FlatStringPool *str_pool, uint32_t obj_id,
                        const FlatObjPathSegs *path, int depth, const FlatValue *val,
                        uint32_t *out_id) {
    if (depth == path->n - 1) {
        return flat_obj_set_copy(pool, str_pool, obj_id, path->keys[depth], val, out_id);
    }
    FlatValue child;
    uint32_t child_id;
    FlatValue child_wrap;
    int rc;

    if (flat_obj_get(pool, obj_id, path->keys[depth], &child) != 0) {
        if (flat_obj_empty(pool, str_pool, &child_id) != 0) {
            return -1;
        }
        flat_value_set_object(&child, child_id);
    } else if (child.tag != VAL_OBJECT) {
        return -2;
    }
    rc = set_path_rec(pool, str_pool, child.u.coll_id, path, depth + 1, val, &child_id);
    if (rc != 0) {
        return rc;
    }
    flat_value_set_object(&child_wrap, child_id);
    return flat_obj_set_copy(pool, str_pool, obj_id, path->keys[depth], &child_wrap, out_id);
}

int flat_obj_set_path_copy(FlatCollectionPool *pool, FlatStringPool *str_pool, uint32_t obj_id,
                           const FlatObjPathSegs *path, const FlatValue *val, uint32_t *out_id) {
    int rc;

    if (!pool || !str_pool || !path || !val || !out_id || path->n <= 0) {
        return -1;
    }
    if (path->n == 1) {
        return flat_obj_set_copy(pool, str_pool, obj_id, path->keys[0], val, out_id);
    }
    rc = set_path_rec(pool, str_pool, obj_id, path, 0, val, out_id);
    if (rc == -2) {
        return -2;
    }
    return rc;
}

static int remove_path_rec(FlatCollectionPool *pool, FlatStringPool *str_pool, uint32_t obj_id,
                           const FlatObjPathSegs *path, int depth, uint32_t *out_id) {
    FlatValue child;
    uint32_t child_new;
    FlatValue child_wrap;
    int rc;

    if (depth == path->n - 1) {
        return flat_obj_remove_copy(pool, str_pool, obj_id, path->keys[depth], out_id);
    }

    if (flat_obj_get(pool, obj_id, path->keys[depth], &child) != 0) {
        return flat_obj_clone_copy(pool, str_pool, obj_id, out_id);
    }
    if (child.tag != VAL_OBJECT) {
        return -2;
    }
    rc = remove_path_rec(pool, str_pool, child.u.coll_id, path, depth + 1, &child_new);
    if (rc == -2) {
        return -2;
    }
    if (rc != 0) {
        return -1;
    }
    flat_value_set_object(&child_wrap, child_new);
    return flat_obj_set_copy(pool, str_pool, obj_id, path->keys[depth], &child_wrap, out_id);
}

int flat_obj_remove_path_copy(FlatCollectionPool *pool, FlatStringPool *str_pool, uint32_t obj_id,
                              const FlatObjPathSegs *path, uint32_t *out_id) {
    if (!pool || !str_pool || !path || !out_id || path->n <= 0) {
        return -1;
    }
    if (path->n == 1) {
        return flat_obj_remove_copy(pool, str_pool, obj_id, path->keys[0], out_id);
    }
    return remove_path_rec(pool, str_pool, obj_id, path, 0, out_id);
}

static int find_var_index(const char *name, int var_count, const char (*names)[64]) {
    int i;
    for (i = 0; i < var_count; i++) {
        if (strcmp(names[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

static int parse_bracket_index(const char **p, FlatStringPool *pool, FlatCollectionPool *coll,
                               FlatValue *vars, int var_count, const char (*var_names)[64],
                               int *out_index, FlatPathErr *err_out) {
    char buf[512];
    const char *s;
    const char *end;
    FlatValue idx_v;
    double n;

    if (err_out) {
        *err_out = FLAT_PATH_OK;
    }
    s = skip_ws(*p);
    end = strchr(s, ']');
    if (!end || (size_t)(end - s) >= sizeof buf) {
        if (err_out) {
            *err_out = FLAT_PATH_SYNTAX;
        }
        return -1;
    }
    memcpy(buf, s, (size_t)(end - s));
    buf[end - s] = '\0';
    if (flat_value_parse_assign_rhs(buf, &idx_v, pool, coll, vars, var_count, var_names) != 0) {
        if (err_out) {
            *err_out = FLAT_PATH_SYNTAX;
        }
        return -1;
    }
    if (idx_v.tag != VAL_NUMBER) {
        if (err_out) {
            *err_out = FLAT_PATH_TYPE;
        }
        return -1;
    }
    n = floor(idx_v.u.number);
    if (n < 0) {
        if (err_out) {
            *err_out = FLAT_PATH_INDEX;
        }
        return -1;
    }
    *out_index = (int)n;
    *p = end + 1;
    return 0;
}

static int parse_field_suffix(const char **p, char *key, size_t key_max) {
    *p = skip_ws(*p);
    if (**p != '.') {
        return -1;
    }
    (*p)++;
    *p = skip_ws(*p);
    if (**p == '\'' || **p == '"') {
        return parse_quoted_segment(p, key, key_max);
    }
    return parse_ident_segment(p, key, key_max);
}

int flat_path_read_suffixes(const char **p, FlatValue *cur, FlatStringPool *pool,
                            FlatCollectionPool *coll, FlatValue *vars, int var_count,
                            const char (*var_names)[64], FlatPathErr *err_out) {
    if (err_out) {
        *err_out = FLAT_PATH_OK;
    }
    if (!p || !*p || !cur) {
        if (err_out) {
            *err_out = FLAT_PATH_SYNTAX;
        }
        return -1;
    }
    for (;;) {
        *p = skip_ws(*p);
        if (**p == '.') {
            char key[FLAT_OBJ_KEY_MAX + 1];
            FlatValue next;
            if (cur->tag != VAL_OBJECT) {
                if (err_out) {
                    *err_out = FLAT_PATH_TYPE;
                }
                return -1;
            }
            if (parse_field_suffix(p, key, sizeof key) != 0) {
                if (err_out) {
                    *err_out = FLAT_PATH_SYNTAX;
                }
                return -1;
            }
            if (!coll || flat_obj_get(coll, cur->u.coll_id, key, &next) != 0) {
                if (err_out) {
                    *err_out = FLAT_PATH_MISSING;
                }
                return -1;
            }
            *cur = next;
            continue;
        }
        if (**p == '[') {
            int ix;
            FlatValue next;
            (*p)++;
            if (parse_bracket_index(p, pool, coll, vars, var_count, var_names, &ix, err_out) != 0) {
                return -1;
            }
            if (cur->tag != VAL_LIST) {
                if (err_out) {
                    *err_out = FLAT_PATH_TYPE;
                }
                return -1;
            }
            if (!coll || flat_list_get(coll, cur->u.coll_id, ix, &next) != 0) {
                if (err_out) {
                    *err_out = FLAT_PATH_INDEX;
                }
                return -1;
            }
            *cur = next;
            continue;
        }
        break;
    }
    return 0;
}

static int parse_lvalue_steps(const char *expr, FlatStringPool *pool, FlatCollectionPool *coll,
                              FlatValue *vars, int var_count, const char (*var_names)[64],
                              int *out_root_vi, FlatPathStep *steps, int *out_nsteps,
                              FlatPathErr *err_out) {
    char ident[64];
    const char *p;
    int n = 0;
    int vi;

    if (err_out) {
        *err_out = FLAT_PATH_OK;
    }
    p = skip_ws(expr);
    if (parse_ident_segment(&p, ident, sizeof ident) != 0) {
        if (err_out) {
            *err_out = FLAT_PATH_SYNTAX;
        }
        return -1;
    }
    vi = find_var_index(ident, var_count, var_names);
    if (vi < 0 || vars[vi].tag != VAL_OBJECT) {
        if (err_out) {
            *err_out = FLAT_PATH_ROOT;
        }
        return -1;
    }
    *out_root_vi = vi;
    for (;;) {
        p = skip_ws(p);
        if (*p == '\0') {
            if (n == 0) {
                if (err_out) {
                    *err_out = FLAT_PATH_SYNTAX;
                }
                return -1;
            }
            *out_nsteps = n;
            return 0;
        }
        if (*p == '.') {
            if (n >= FLAT_OBJ_PATH_MAX_SEG) {
                if (err_out) {
                    *err_out = FLAT_PATH_SYNTAX;
                }
                return -1;
            }
            steps[n].kind = FLAT_PATH_STEP_FIELD;
            if (parse_field_suffix(&p, steps[n].key, sizeof steps[n].key) != 0) {
                if (err_out) {
                    *err_out = FLAT_PATH_SYNTAX;
                }
                return -1;
            }
            n++;
            continue;
        }
        if (*p == '[') {
            p++;
            if (n >= FLAT_OBJ_PATH_MAX_SEG) {
                if (err_out) {
                    *err_out = FLAT_PATH_SYNTAX;
                }
                return -1;
            }
            steps[n].kind = FLAT_PATH_STEP_INDEX;
            if (parse_bracket_index(&p, pool, coll, vars, var_count, var_names, &steps[n].index,
                                    err_out) != 0) {
                return -1;
            }
            n++;
            continue;
        }
        if (err_out) {
            *err_out = FLAT_PATH_SYNTAX;
        }
        return -1;
    }
}

static int materialize_child_field(const FlatSchemaField *field, FlatCollectionPool *pool,
                                   FlatStringPool *str_pool, const FlatSchemaCatalog *cat,
                                   FlatValue *out, char *panic, size_t panic_sz) {
    uint32_t id;

    if (!field || !out || !pool || !str_pool) {
        return -1;
    }
    if (field->flags & FLAT_SCH_FIELD_LIST) {
        if (flat_list_empty(pool, str_pool, &id) != 0) {
            return -1;
        }
        return flat_value_set_list(out, id);
    }
    if (field->slot.kind == FLAT_SLOT_TYPE_SCHEMA && cat) {
        FlatValue obj;
        if (flat_obj_empty(pool, str_pool, &id) != 0) {
            return -1;
        }
        flat_value_set_object(&obj, id);
        if (flat_schema_bind_value(cat, field->slot.schema_id, &obj, str_pool, pool, panic,
                                   panic_sz) != 0) {
            return -1;
        }
        *out = obj;
        return 0;
    }
    if (field->slot.kind == FLAT_SLOT_TYPE_O) {
        if (flat_obj_empty(pool, str_pool, &id) != 0) {
            return -1;
        }
        return flat_value_set_object(out, id);
    }
    if (field->slot.kind == FLAT_SLOT_TYPE_L) {
        if (flat_list_empty(pool, str_pool, &id) != 0) {
            return -1;
        }
        return flat_value_set_list(out, id);
    }
    return -1;
}

static int type_panic_write(const FlatPathTypedCtx *typed, const char *code) {
    if (typed && typed->type_panic && typed->type_panic_sz > 0 && code) {
        snprintf(typed->type_panic, typed->type_panic_sz, "%s", code);
    }
    return -4;
}

static int write_at_step(FlatCollectionPool *pool, FlatStringPool *str_pool, FlatValue cur,
                         const FlatPathStep *steps, int step_i, int nsteps, const FlatValue *val,
                         FlatValue *out_cur, const FlatSlotType *cur_contract,
                         const FlatPathTypedCtx *typed) {
    const FlatValue *write_val = val;

    if (step_i == nsteps - 1) {
        if (steps[step_i].kind == FLAT_PATH_STEP_FIELD) {
            const FlatSchemaCatalog *cat =
                typed && typed->schemas ? (const FlatSchemaCatalog *)typed->schemas : NULL;
            const FlatSchemaDef *schema_def = flat_schema_def_for_contract(cat, cur_contract);
            const FlatSchemaField *field = NULL;
            FlatValue checked = *val;
            char panic[64];
            uint32_t new_id;

            if (cur.tag != VAL_OBJECT) {
                return -2;
            }
            if (schema_def) {
                if (!flat_schema_field_lookup(schema_def, steps[step_i].key, &field)) {
                    return type_panic_write(typed, "type.schema.field.unknown");
                }
                if (field &&
                    flat_schema_check_field_value(field, val, str_pool, pool, cat, &checked, panic,
                                                sizeof panic) != 0) {
                    return type_panic_write(typed, panic[0] ? panic : "type.assign.incompatible");
                }
                write_val = &checked;
            }
            if (flat_obj_set_copy(pool, str_pool, cur.u.coll_id, steps[step_i].key, write_val,
                                  &new_id) != 0) {
                return -1;
            }
            return flat_value_set_object(out_cur, new_id);
        }
        if (steps[step_i].kind == FLAT_PATH_STEP_INDEX) {
            uint32_t new_id;
            FlatValue checked = *val;
            const FlatValue *write_val_idx = val;
            char panic[64];

            if (cur.tag != VAL_LIST) {
                return -2;
            }
            if (cur_contract && cur_contract->kind != FLAT_SLOT_TYPE_NONE &&
                flat_slot_type_check_value(
                    cur_contract, val, str_pool, pool,
                    typed && typed->schemas ? (const FlatSchemaCatalog *)typed->schemas : NULL,
                    &checked, panic, sizeof panic) != 0) {
                return type_panic_write(typed, panic[0] ? panic : "type.assign.incompatible");
            }
            if (cur_contract && cur_contract->kind != FLAT_SLOT_TYPE_NONE) {
                write_val_idx = &checked;
            }
            if (flat_list_set_copy(pool, str_pool, cur.u.coll_id, steps[step_i].index, write_val_idx,
                                   &new_id) != 0) {
                return -1;
            }
            return flat_value_set_list(out_cur, new_id);
        }
        return -1;
    }
    if (steps[step_i].kind == FLAT_PATH_STEP_FIELD) {
        FlatValue child;
        FlatValue child_new;
        uint32_t parent_new;
        int rc;

        if (cur.tag != VAL_OBJECT) {
            return -2;
        }
        {
            const FlatSchemaCatalog *cat =
                typed && typed->schemas ? (const FlatSchemaCatalog *)typed->schemas : NULL;
            const FlatSchemaDef *schema_def = flat_schema_def_for_contract(cat, cur_contract);
            const FlatSchemaField *field = NULL;
            FlatSlotType next_contract;
            char mat_panic[64];
            int has_child = 0;

            flat_slot_type_clear(&next_contract);
            if (schema_def) {
                if (!flat_schema_field_lookup(schema_def, steps[step_i].key, &field)) {
                    return type_panic_write(typed, "type.schema.field.unknown");
                }
                next_contract = field->slot;
            }
            has_child = (flat_obj_get(pool, cur.u.coll_id, steps[step_i].key, &child) == 0);
            if (!has_child) {
                if (schema_def && field &&
                    materialize_child_field(field, pool, str_pool, cat, &child, mat_panic,
                                            sizeof mat_panic) != 0) {
                    return type_panic_write(typed,
                                            mat_panic[0] ? mat_panic : "type.assign.incompatible");
                } else if (!schema_def) {
                    uint32_t empty_id;
                    if (flat_obj_empty(pool, str_pool, &empty_id) != 0) {
                        return -1;
                    }
                    flat_value_set_object(&child, empty_id);
                } else {
                    return type_panic_write(typed, "type.schema.field.unknown");
                }
            } else if (child.tag == VAL_NULL && field) {
                if (materialize_child_field(field, pool, str_pool, cat, &child, mat_panic,
                                            sizeof mat_panic) != 0) {
                    return type_panic_write(typed,
                                            mat_panic[0] ? mat_panic : "type.assign.incompatible");
                }
            }
            rc = write_at_step(pool, str_pool, child, steps, step_i + 1, nsteps, val, &child_new,
                               field ? &next_contract : cur_contract, typed);
        }
        if (rc != 0) {
            return rc;
        }
        if (flat_obj_set_copy(pool, str_pool, cur.u.coll_id, steps[step_i].key, &child_new,
                              &parent_new) != 0) {
            return -1;
        }
        return flat_value_set_object(out_cur, parent_new);
    }
    if (steps[step_i].kind == FLAT_PATH_STEP_INDEX) {
        FlatValue elem;
        FlatValue elem_new;
        uint32_t list_new;
        int rc;

        if (cur.tag != VAL_LIST) {
            return -2;
        }
        if (flat_list_get(pool, cur.u.coll_id, steps[step_i].index, &elem) != 0) {
            return -3;
        }
        rc = write_at_step(pool, str_pool, elem, steps, step_i + 1, nsteps, val, &elem_new,
                           cur_contract, typed);
        if (rc != 0) {
            return rc;
        }
        if (flat_list_set_copy(pool, str_pool, cur.u.coll_id, steps[step_i].index, &elem_new,
                               &list_new) != 0) {
            return -1;
        }
        return flat_value_set_list(out_cur, list_new);
    }
    return -1;
}

int flat_path_set2(FlatCollectionPool *coll, FlatStringPool *pool, FlatValue *vars, int var_count,
                   const char (*var_names)[64], const char *path_expr, const FlatValue *val,
                   uint32_t *out_root_obj, FlatPathErr *err_out, const FlatPathTypedCtx *typed) {
    FlatPathStep steps[FLAT_OBJ_PATH_MAX_SEG];
    int nsteps = 0;
    int root_vi;
    FlatValue root;
    FlatValue new_root;
    int rc;

    if (err_out) {
        *err_out = FLAT_PATH_OK;
    }
    if (!coll || !pool || !vars || !path_expr || !val || !out_root_obj) {
        if (err_out) {
            *err_out = FLAT_PATH_SYNTAX;
        }
        return -1;
    }
    if (parse_lvalue_steps(path_expr, pool, coll, vars, var_count, var_names, &root_vi, steps,
                           &nsteps, err_out) != 0) {
        return -1;
    }
    root = vars[root_vi];
    {
        FlatSlotType root_contract;

        flat_slot_type_clear(&root_contract);
        if (typed && typed->var_has_slot_type && typed->var_slot_types &&
            typed->var_has_slot_type[root_vi]) {
            root_contract = typed->var_slot_types[root_vi];
        }
        if (typed && typed->type_panic && typed->type_panic_sz > 0) {
            typed->type_panic[0] = '\0';
        }
        rc = write_at_step(coll, pool, root, steps, 0, nsteps, val, &new_root, &root_contract,
                           typed);
    }
    if (rc == -4) {
        return -1;
    }
    if (rc == -2) {
        if (err_out) {
            *err_out = FLAT_PATH_SET;
        }
        return -1;
    }
    if (rc == -3) {
        if (err_out) {
            *err_out = FLAT_PATH_INDEX;
        }
        return -1;
    }
    if (rc != 0) {
        if (err_out) {
            *err_out = FLAT_PATH_SET;
        }
        return -1;
    }
    if (new_root.tag != VAL_OBJECT) {
        if (err_out) {
            *err_out = FLAT_PATH_SET;
        }
        return -1;
    }
    *out_root_obj = new_root.u.coll_id;
    return 0;
}

double flat_path_resolve_number(const char *name, const char **rest,
                                const FlatCollectionPool *coll, FlatStringPool *pool,
                                const FlatValue *vars, int var_count,
                                const char (*var_names)[64], FlatPathErr *err_out) {
    FlatValue cur;
    int vi;
    FlatPathErr local = FLAT_PATH_OK;
    const char *p;

    if (err_out) {
        *err_out = FLAT_PATH_OK;
    }
    vi = find_var_index(name, var_count, var_names);
    if (vi < 0) {
        if (err_out) {
            *err_out = FLAT_PATH_MISSING;
        }
        return 0.0;
    }
    cur = vars[vi];
    p = skip_ws(*rest);
    if (*p != '.' && *p != '[') {
        if (cur.tag != VAL_NUMBER) {
            if (err_out) {
                *err_out = FLAT_PATH_TYPE;
            }
            return 0.0;
        }
        *rest = p;
        return cur.u.number;
    }
    if (flat_path_read_suffixes(&p, &cur, pool, (FlatCollectionPool *)coll, (FlatValue *)vars,
                                var_count, var_names, &local) != 0) {
        if (err_out) {
            *err_out = local;
        }
        return 0.0;
    }
    if (cur.tag != VAL_NUMBER) {
        if (err_out) {
            *err_out = FLAT_PATH_TYPE;
        }
        return 0.0;
    }
    *rest = p;
    return cur.u.number;
}

const char *flat_path_reason(FlatPathErr err, const char *ctx, char *buf, size_t buf_sz) {
    const char *sub = "obj.path";

    (void)ctx;
    switch (err) {
    case FLAT_PATH_SYNTAX:
        sub = "obj.path.syntax";
        break;
    case FLAT_PATH_ARG_TYPE:
        sub = "obj.get.arg";
        break;
    case FLAT_PATH_MISSING:
        sub = "obj.get.missing";
        break;
    case FLAT_PATH_TYPE:
        sub = "obj.path.type";
        break;
    case FLAT_PATH_INDEX:
        sub = "obj.path.index";
        break;
    case FLAT_PATH_SET:
        sub = "obj.set.path";
        break;
    case FLAT_PATH_ROOT:
        sub = "obj.set2.root";
        break;
    default:
        break;
    }
    if (ctx && ctx[0] != '\0') {
        if (strcmp(ctx, "set") == 0 && err == FLAT_PATH_ARG_TYPE) {
            sub = "obj.set.arg";
        } else if (strcmp(ctx, "has") == 0 && err == FLAT_PATH_ARG_TYPE) {
            sub = "obj.has.arg";
        } else if (strcmp(ctx, "remove") == 0 && err == FLAT_PATH_ARG_TYPE) {
            sub = "obj.remove.arg";
        } else if (strcmp(ctx, "remove") == 0 && err == FLAT_PATH_TYPE) {
            sub = "obj.remove.path";
        } else if (strcmp(ctx, "set2") == 0 && err != FLAT_PATH_ROOT) {
            sub = "obj.set2.path";
        }
    }
    if (buf && buf_sz > 0) {
        snprintf(buf, buf_sz, "%s", sub);
    }
    return sub;
}
