#include "fb_format.h"
#include "flat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t read_u16_le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int check_magic(const uint8_t *data, size_t size) {
    if (size < FLAT_FB_MAGIC_SIZE) {
        return -1;
    }
    return memcmp(data, FLAT_MAGIC, FLAT_FB_MAGIC_SIZE) != 0 ? -1 : 0;
}

int flat_fb_load_file(const char *path, FlatBytecode *out) {
    FILE *f;
    long fsize;
    uint8_t *buf = NULL;
    size_t dir_bytes;
    uint16_t count;
    FlatBlockEntry *blocks = NULL;
    size_t i;

    if (!path || !out) {
        return -1;
    }
    memset(out, 0, sizeof *out);

    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "flvm: cannot open %s\n", path);
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    fsize = ftell(f);
    if (fsize < 0) {
        fclose(f);
        return -1;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    if ((size_t)fsize < FLAT_FB_MAGIC_SIZE + 2) {
        fprintf(stderr, "flvm: file too small (%s)\n", path);
        fclose(f);
        return -1;
    }
    buf = malloc((size_t)fsize);
    if (!buf) {
        fclose(f);
        return -1;
    }
    if (fread(buf, 1, (size_t)fsize, f) != (size_t)fsize) {
        free(buf);
        fclose(f);
        fprintf(stderr, "flvm: read error (%s)\n", path);
        return -1;
    }
    fclose(f);

    if (check_magic(buf, (size_t)fsize) != 0) {
        free(buf);
        fprintf(stderr, "flvm: bad magic (expected FLAT) in %s\n", path);
        return -1;
    }

    count = read_u16_le(buf + FLAT_FB_MAGIC_SIZE);
    if (count == 0) {
        free(buf);
        fprintf(stderr, "flvm: block_count is zero\n");
        return -1;
    }

    dir_bytes = (size_t)count * sizeof(FlatBlockEntry);
    if (FLAT_FB_MAGIC_SIZE + 2 + dir_bytes > (size_t)fsize) {
        free(buf);
        fprintf(stderr, "flvm: truncated block directory\n");
        return -1;
    }

    blocks = calloc(count, sizeof(FlatBlockEntry));
    if (!blocks) {
        free(buf);
        return -1;
    }

    for (i = 0; i < count; i++) {
        const uint8_t *ent = buf + FLAT_FB_MAGIC_SIZE + 2 + i * 8;
        blocks[i].block_id = read_u32_le(ent);
        blocks[i].offset = read_u32_le(ent + 4);
        if (blocks[i].offset >= (size_t)fsize) {
            free(blocks);
            free(buf);
            fprintf(stderr, "flvm: block offset out of range\n");
            return -1;
        }
    }

    out->data = buf;
    out->size = (size_t)fsize;
    out->blocks = blocks;
    out->block_count = count;
    return 0;
}

void flat_fb_free(FlatBytecode *bc) {
    if (!bc) {
        return;
    }
    free(bc->data);
    free(bc->blocks);
    memset(bc, 0, sizeof *bc);
}

static int write_bytes(FILE *f, const void *p, size_t n) {
    return fwrite(p, 1, n, f) == n ? 0 : -1;
}

int flat_fb_write_min_ok(const char *path) {
    FILE *f;
    const uint8_t magic[4] = { 'F', 'L', 'A', 'T' };
    const uint8_t count[2] = { 1, 0 };
    const uint8_t dir[8] = {
        0, 0, 0, 0, /* block_id main = 0 */
        14, 0, 0, 0  /* offset = 4+2+8 = 14 */
    };
    const uint8_t code[2] = { OP_OK, 0 };

    f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "mkfb: cannot write %s\n", path);
        return -1;
    }
    if (write_bytes(f, magic, 4) != 0 || write_bytes(f, count, 2) != 0 ||
        write_bytes(f, dir, 8) != 0 || write_bytes(f, code, 2) != 0) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

int flat_vm_run(const FlatBytecode *bc) {
    uint32_t ip;
    size_t entry_index = 0;
    int running = 1;

    if (!bc || !bc->data || bc->block_count == 0) {
        return 1;
    }

    for (entry_index = 0; entry_index < bc->block_count; entry_index++) {
        if (bc->blocks[entry_index].block_id == FLAT_FB_BLOCK_ID_MAIN) {
            break;
        }
    }
    if (entry_index >= bc->block_count) {
        entry_index = 0;
    }

    ip = bc->blocks[entry_index].offset;

    while (running) {
        uint8_t opcode;
        uint8_t payload_size;

        if ((size_t)ip + 2 > bc->size) {
            fprintf(stderr, "flvm: truncated instruction at %u\n", ip);
            return 1;
        }
        opcode = bc->data[ip];
        payload_size = bc->data[ip + 1];
        if ((size_t)ip + 2 + payload_size > bc->size) {
            fprintf(stderr, "flvm: truncated payload at %u\n", ip);
            return 1;
        }

        if (opcode == OP_OK) {
            return 0;
        }
        if (opcode == OP_FAIL) {
            return 2;
        }
        fprintf(stderr, "flvm: unsupported opcode 0x%02x at %u\n", opcode, ip);
        return 1;
    }
    return 1;
}
