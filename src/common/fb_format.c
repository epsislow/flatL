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

static int write_bytes(FILE *f, const void *p, size_t n) {
    return fwrite(p, 1, n, f) == n ? 0 : -1;
}

size_t flat_fb_native_section_size(int decl_count, const FlatOptionalNativeDecl *decls) {
    size_t n = 2 + 4;
    int i;
    if (decl_count <= 0 || !decls) {
        return 0;
    }
    for (i = 0; i < decl_count; i++) {
        size_t ml = strlen(decls[i].module);
        size_t al = strlen(decls[i].alias);
        if (ml == 0 || ml > 255 || al > 255) {
            return 0;
        }
        n += 2 + ml + al;
    }
    return n;
}

int flat_fb_write_native_section(FILE *f, int decl_count, const FlatOptionalNativeDecl *decls) {
    uint16_t count;
    uint32_t tail_magic = FLAT_FB_TAIL_MAGIC;
    int i;

    if (decl_count <= 0 || !decls || !f) {
        return -1;
    }
    count = (uint16_t)decl_count;
    if (write_bytes(f, &count, 2) != 0) {
        return -1;
    }
    for (i = 0; i < decl_count; i++) {
        uint8_t ml = (uint8_t)strlen(decls[i].module);
        uint8_t al = (uint8_t)strlen(decls[i].alias);
        if (write_bytes(f, &ml, 1) != 0 || write_bytes(f, decls[i].module, ml) != 0 ||
            write_bytes(f, &al, 1) != 0 ||
            (al > 0 && write_bytes(f, decls[i].alias, al) != 0)) {
            return -1;
        }
    }
    if (write_bytes(f, &tail_magic, 4) != 0) {
        return -1;
    }
    return 0;
}

int flat_fb_parse_native_section(const uint8_t *sec, size_t sec_len,
                                 FlatOptionalNativeDecl *out, int out_max, int *out_count) {
    size_t pos = 0;
    uint16_t count;
    int i;

    if (!out || !out_count) {
        return -1;
    }
    *out_count = 0;
    if (!sec || sec_len < 6) {
        return -1;
    }
    if (read_u32_le(sec + sec_len - 4) != FLAT_FB_TAIL_MAGIC) {
        return -1;
    }
    count = read_u16_le(sec);
    pos = 2;
    if (count > (uint16_t)out_max) {
        return -1;
    }
    for (i = 0; i < (int)count; i++) {
        uint8_t ml;
        uint8_t al;
        if (pos + 2 > sec_len - 4) {
            return -1;
        }
        ml = sec[pos++];
        if (pos + ml + 1 > sec_len - 4) {
            return -1;
        }
        if (ml >= FGIR_NATIVE_MODULE_LEN) {
            return -1;
        }
        memcpy(out[i].module, sec + pos, ml);
        out[i].module[ml] = '\0';
        pos += ml;
        al = sec[pos++];
        if (pos + al > sec_len - 4) {
            return -1;
        }
        if (al >= FGIR_NATIVE_ALIAS_LEN) {
            return -1;
        }
        if (al > 0) {
            memcpy(out[i].alias, sec + pos, al);
            out[i].alias[al] = '\0';
            pos += al;
        } else {
            out[i].alias[0] = '\0';
        }
    }
    if (pos + 4 != sec_len) {
        return -1;
    }
    *out_count = (int)count;
    return 0;
}

int flat_fb_load_file(const char *path, FlatBytecode *out) {
    FILE *f;
    long fsize;
    uint8_t *buf = NULL;
    size_t dir_bytes;
    size_t header_size;
    size_t dir_offset;
    uint16_t count;
    uint32_t native_off = 0;
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

    if (buf[4] == FLAT_FB_SENTINEL && (size_t)fsize >= 12 && buf[5] == FLAT_FB_FORMAT_VERSION) {
        out->format_version = (int)FLAT_FB_FORMAT_VERSION;
        count = read_u16_le(buf + 6);
        native_off = read_u32_le(buf + 8);
        header_size = 12;
        dir_offset = 12;
    } else {
        out->format_version = 0;
        count = read_u16_le(buf + FLAT_FB_MAGIC_SIZE);
        header_size = 6;
        dir_offset = 6;
    }

    if (count == 0) {
        free(buf);
        fprintf(stderr, "flvm: block_count is zero\n");
        return -1;
    }

    dir_bytes = (size_t)count * sizeof(FlatBlockEntry);
    if (header_size + dir_bytes > (size_t)fsize) {
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
        const uint8_t *ent = buf + dir_offset + i * 8;
        blocks[i].block_id = read_u32_le(ent);
        blocks[i].offset = read_u32_le(ent + 4);
        if (blocks[i].offset >= (size_t)fsize) {
            free(blocks);
            free(buf);
            fprintf(stderr, "flvm: block offset out of range\n");
            return -1;
        }
    }

    if (native_off != 0) {
        if (native_off >= (size_t)fsize) {
            free(blocks);
            free(buf);
            fprintf(stderr, "flvm: native section offset out of range\n");
            return -1;
        }
        if (flat_fb_parse_native_section(buf + native_off, (size_t)fsize - native_off,
                                         out->optional_natives, FGIR_MAX_OPTIONAL_NATIVES,
                                         &out->optional_native_count) != 0) {
            free(blocks);
            free(buf);
            fprintf(stderr, "flvm: invalid optional native manifest\n");
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

int flat_fb_write_min_ok(const char *path) {
    FILE *f;
    const uint8_t magic[4] = { 'F', 'L', 'A', 'T' };
    const uint8_t sentinel[1] = { FLAT_FB_SENTINEL };
    const uint8_t version[1] = { FLAT_FB_FORMAT_VERSION };
    const uint8_t count[2] = { 1, 0 };
    const uint8_t native_off[4] = { 0, 0, 0, 0 };
    const uint8_t dir[8] = {
        0, 0, 0, 0, /* block_id main = 0 */
        20, 0, 0, 0  /* offset = 12 + 8 */
    };
    const uint8_t code[2] = { OP_OK, 0 };

    f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "mkfb: cannot write %s\n", path);
        return -1;
    }
    if (write_bytes(f, magic, 4) != 0 || write_bytes(f, sentinel, 1) != 0 ||
        write_bytes(f, version, 1) != 0 || write_bytes(f, count, 2) != 0 ||
        write_bytes(f, native_off, 4) != 0 || write_bytes(f, dir, 8) != 0 ||
        write_bytes(f, code, 2) != 0) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

