#ifndef FLAT_FB_FORMAT_H
#define FLAT_FB_FORMAT_H

#include "fgir.h"
#include "flat_schema.h"

#include <stddef.h>
#include <stdint.h>

#define FLAT_FB_MAGIC_SIZE 4
#define FLAT_FB_BLOCK_ID_MAIN 0u
#define FLAT_FB_SENTINEL 0xffu
#define FLAT_FB_FORMAT_VERSION 12u
#define FLAT_FB_HEADER_SIZE_V11 36 /* v11: jbmx_off at bytes 32–35 */
#define FLAT_FB_HEADER_SIZE 32     /* v3–v10 header (load) */
#define FLAT_FB_JBMX_MAGIC 0x584d424au /* 'JBMX' LE */
#define FLAT_FB_HEADER_SIZE_V5 28
#define FLAT_FB_HEADER_SIZE_V4 24
#define FLAT_FB_HEADER_SIZE_V3 20
#define FLAT_FB_FFUN_MAGIC 0x4e554646u /* 'FFUN' LE */
#define FLAT_FB_LABEL_MAGIC 0x424c424cu /* 'BLBL' LE */
#define FLAT_FB_FNUN_MAGIC 0x4e554e46u /* 'FNUN' LE */
#define FLAT_FB_TAIL_MAGIC 0x54414e46u /* "FNAT" little-endian */

typedef struct {
    FlatOptionalNativeDecl decls[FGIR_MAX_OPTIONAL_NATIVES];
    int decl_count;
} FlatFbNativeUnit;

typedef struct {
    uint32_t block_id;
    uint32_t offset;
} FlatBlockEntry;

typedef struct {
    char name[FGIR_MAX_FUNC_NAME];
    uint32_t impl_block_id;
    uint32_t executor_block_id;
    uint8_t param_count;
    char return_name[64];
    uint8_t return_count;
    char return_names[FGIR_MAX_FUNC_RETURNS][64];
    uint32_t func_vcx_slots;
    uint8_t param_has_type[FGIR_MAX_FUNC_PARAMS];
    char param_type_spec[FGIR_MAX_FUNC_PARAMS][48];
    uint8_t return_has_type;
    char return_type_spec[48];
    uint8_t return_has_type_arr[FGIR_MAX_FUNC_RETURNS];
    char return_type_spec_arr[FGIR_MAX_FUNC_RETURNS][48];
    uint8_t def_tag_has_section;
    char def_tag_canonical[256];
} FlatFbFunctionEntry;

typedef struct {
    uint8_t *data;
    size_t size;
    size_t code_size; /* bytecode stream only (excludes BLBL / FNAT tail) */
    FlatBlockEntry *blocks;
    uint16_t block_count;
    int format_version;
    uint32_t var_slots; /* static upper bound on distinct variable names (<= FLAT_MAX_VARS) */
    FlatOptionalNativeDecl optional_natives[FGIR_MAX_OPTIONAL_NATIVES];
    int optional_native_count;
    char (*block_labels)[FGIR_MAX_LABEL]; /* heap; length block_count; from BLBL section */
    FlatFbNativeUnit *native_units;
    int native_unit_count;
    uint8_t *block_native_unit; /* heap; length block_count; FNUN section */
    FlatFbFunctionEntry *functions;
    uint16_t function_count;
    FlatSchemaCatalog schemas;
    uint32_t *jump_block_entry_max; /* heap, length block_count; 0 = unlimited per target */
} FlatBytecode;

size_t flat_fb_jbmx_section_size(uint16_t block_count);
int flat_fb_write_jbmx_section(FILE *f, uint16_t block_count, const uint32_t *limits);
int flat_fb_parse_jbmx_section(const uint8_t *sec, size_t sec_len, uint16_t block_count,
                                 uint32_t **limits_out);

size_t flat_fb_native_section_size(int decl_count, const FlatOptionalNativeDecl *decls);
int flat_fb_write_label_section(FILE *f, uint16_t count, const char labels[][FGIR_MAX_LABEL]);
size_t flat_fb_label_section_size(uint16_t count);
int flat_fb_write_native_section(FILE *f, int decl_count, const FlatOptionalNativeDecl *decls);
size_t flat_fb_fnun_section_size(int unit_count, const FlatFbNativeUnit *units,
                               uint16_t block_count);
int flat_fb_write_fnun_section(FILE *f, int unit_count, const FlatFbNativeUnit *units,
                               uint16_t block_count, const uint8_t *block_unit);
int flat_fb_parse_native_section(const uint8_t *sec, size_t sec_len,
                                 FlatOptionalNativeDecl *out, int out_max, int *out_count);

int flat_fb_load_file(const char *path, FlatBytecode *out);
/* Takes ownership of a heap copy of data (same layout as flat_fb_load_file). */
int flat_fb_load_buffer(const uint8_t *data, size_t len, FlatBytecode *out);
void flat_fb_free(FlatBytecode *bc);

int flat_fb_write_min_ok(const char *path);
size_t flat_fb_ffun_section_size(uint16_t count, const FlatFbFunctionEntry *entries);
int flat_fb_write_ffun_section(FILE *f, uint16_t count, const FlatFbFunctionEntry *entries);

#endif
