#ifndef FLAT_FB_FORMAT_H
#define FLAT_FB_FORMAT_H

#include <stddef.h>
#include <stdint.h>

#define FLAT_FB_MAGIC_SIZE 4
#define FLAT_FB_BLOCK_ID_MAIN 0u

typedef struct {
    uint32_t block_id;
    uint32_t offset;
} FlatBlockEntry;

typedef struct {
    uint8_t *data;
    size_t size;
    FlatBlockEntry *blocks;
    uint16_t block_count;
} FlatBytecode;

int flat_fb_load_file(const char *path, FlatBytecode *out);
void flat_fb_free(FlatBytecode *bc);

int flat_vm_run(const FlatBytecode *bc);

int flat_fb_write_min_ok(const char *path);

#endif
