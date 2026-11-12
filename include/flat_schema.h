#ifndef FLAT_SCHEMA_H
#define FLAT_SCHEMA_H

#include "flat_value.h"
#include "flat_slot_type.h"
#include "fgir.h"

#include <stddef.h>
#include <stdint.h>

struct FlatVcxRuntime;

#define FLAT_SCHEMA_MAX 64
#define FLAT_SCHEMA_NAME_MAX 63
#define FLAT_SCHEMA_FIELD_MAX 128
#define FLAT_SCHEMA_TYPE_SPEC_MAX 63

typedef enum {
    FLAT_SCH_FIELD_NONE = 0,
    FLAT_SCH_FIELD_LIST = 1
} FlatSchemaFieldFlags;

typedef struct FlatSchemaField {
    char key[FLAT_OBJ_KEY_MAX + 1];
    FlatSlotType slot; /* kind + aux + schema_id + list_max for nested/list */
    uint8_t flags;   /* FLAT_SCH_FIELD_LIST when field is t[] or <s>[] */
} FlatSchemaField;

typedef struct FlatSchemaDef {
    char name[FLAT_SCHEMA_NAME_MAX + 1];
    int field_count;
    FlatSchemaField fields[FLAT_SCHEMA_FIELD_MAX];
} FlatSchemaDef;

typedef struct FlatSchemaCatalog {
    int count;
    FlatSchemaDef defs[FLAT_SCHEMA_MAX];
} FlatSchemaCatalog;

/* Parse `<name>:` header into name (without brackets). */
int flat_schema_parse_header_line(const char *line, char *name_out, size_t name_sz);

/* Non-zero when line is `<name>` with valid name but missing trailing `:`. */
int flat_schema_header_missing_colon(const char *line, char *name_out, size_t name_sz);

/* Trimmed schema body line (no leading indent): blank/--, ident-only, or empty spec. */
int flat_schema_body_line_ignorable(const char *trimmed);

/* Parse field line `key: type` (non-indented rejected). */
int flat_schema_parse_field_line(const char *line, char *key_out, size_t key_sz, char *spec_out,
                                 size_t spec_sz);

int flat_schema_find_id(const FlatSchemaCatalog *cat, const char *name);

/* Build catalog from FGIR decls; detects unknown refs and cycles. */
int flat_schema_build_catalog(const FgirSchemaDecl *decls, int decl_count, FlatSchemaCatalog *out,
                              char *err, size_t err_sz);

int flat_bind_parse_lhs_catalog(const char *lhs, char *name_out, size_t name_sz, FlatSlotType *type_out,
                                int *has_type_out, const FlatSchemaCatalog *cat);

int flat_bind_validate_lhs(const char *lhs, const FlatSchemaCatalog *cat, char *err, size_t err_sz);

/* Resolve ASSIGN lhs (static or :/expr); *is_dynamic when STRING type read at runtime. */
int flat_bind_resolve_lhs_runtime(const char *lhs, char *name_out, size_t name_sz,
                                  FlatSlotType *type_out, int *has_type_out, int *is_dynamic_out,
                                  FlatValue *vars, int var_count, const char (*var_names)[64],
                                  FlatStringPool *pool, FlatCollectionPool *coll,
                                  const FlatSchemaCatalog *cat, struct FlatVcxRuntime *vcx,
                                  char *panic, size_t panic_sz);

/* Runtime: normalize an object to a schema. The input value is borrowed and is never
 * consumed/released. On success io_val references a newly-created, currently-unbound
 * object; the caller must retain it exactly once when storing/binding the result. */
int flat_schema_bind_value(const FlatSchemaCatalog *cat, uint16_t schema_id, FlatValue *io_val,
                           FlatStringPool *pool, FlatCollectionPool *coll, char *panic,
                           size_t panic_sz);

/* NULL def or key → 0; on match *out_field set if non-NULL. */
int flat_schema_field_lookup(const FlatSchemaDef *def, const char *key,
                             const FlatSchemaField **out_field);

/* Validate a value against a schema field (incl. <s>[N] list fields and element types). */
int flat_schema_check_field_value(const FlatSchemaField *field, const FlatValue *val,
                                  FlatStringPool *pool, FlatCollectionPool *coll,
                                  const FlatSchemaCatalog *cat, FlatValue *inout_val,
                                  char *panic, size_t panic_sz);

const FlatSchemaDef *flat_schema_def_for_contract(const FlatSchemaCatalog *cat,
                                                  const FlatSlotType *contract);

#define FLAT_FB_SCHM_MAGIC 0x4d484353u /* 'SCHM' LE */

size_t flat_fb_schm_section_size(const FlatSchemaCatalog *cat);
int flat_fb_write_schm_section(FILE *f, const FlatSchemaCatalog *cat);
int flat_fb_parse_schm_section(const uint8_t *sec, size_t sec_len, FlatSchemaCatalog *out);

#endif
