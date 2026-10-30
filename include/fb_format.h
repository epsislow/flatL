#ifndef FLAT_FB_FORMAT_H
#define FLAT_FB_FORMAT_H

#include "fgir.h"

#include <stddef.h>
#include <stdint.h>

#define FLAT_FB_MAGIC_SIZE 4
#define FLAT_FB_BLOCK_ID_MAIN 0u
#define FLAT_FB_SENTINEL 0xffu
#define FLAT_FB_FORMAT_VERSION 2u
#define FLAT_FB_HEADER_SIZE 16 /* magic4 sentinel1 version1 count2 native_off4 var_slots4 */
#define FLAT_FB_TAIL_MAGIC 0x54414e46u /* "FNAT" little-endian */

typedef struct {
    uint32_t block_id;
    uint32_t offset;
} FlatBlockEntry;

typedef struct {
    uint8_t *data;
    size_t size;
    FlatBlockEntry *blocks;
    uint16_t block_count;
    int format_version;
    uint32_t var_slots; /* static upper bound on distinct variable names (<= FLAT_MAX_VARS) */
    FlatOptionalNativeDecl optional_natives[FGIR_MAX_OPTIONAL_NATIVES];
    int optional_native_count;
} FlatBytecode;

size_t flat_fb_native_section_size(int decl_count, const FlatOptionalNativeDecl *decls);
int flat_fb_write_native_section(FILE *f, int decl_count, const FlatOptionalNativeDecl *decls);
int flat_fb_parse_native_section(const uint8_t *sec, size_t sec_len,
                                 FlatOptionalNativeDecl *out, int out_max, int *out_count);

int flat_fb_load_file(const char *path, FlatBytecode *out);
void flat_fb_free(FlatBytecode *bc);

int flat_fb_write_min_ok(const char *path);

#endif
