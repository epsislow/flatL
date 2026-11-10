#include "fb_format.h"
#include "flat.h"
#include "flat_value.h"

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

size_t flat_fb_label_section_size(uint16_t count) {
    return 4u + 2u + (size_t)count * 2u; /* magic + count; per label u16 len + bytes added separately */
}

int flat_fb_write_label_section(FILE *f, uint16_t count, const char labels[][FGIR_MAX_LABEL]) {
    uint32_t magic = FLAT_FB_LABEL_MAGIC;
    uint16_t i;

    if (!f || count == 0) {
        return -1;
    }
    if (write_bytes(f, &magic, 4) != 0 || write_bytes(f, &count, 2) != 0) {
        return -1;
    }
    for (i = 0; i < count; i++) {
        size_t len = strlen(labels[i]);
        uint16_t ulen;
        if (len == 0 || len >= FGIR_MAX_LABEL) {
            return -1;
        }
        ulen = (uint16_t)len;
        if (write_bytes(f, &ulen, 2) != 0 || write_bytes(f, labels[i], len) != 0) {
            return -1;
        }
    }
    return 0;
}

static int parse_label_section(const uint8_t *sec, size_t sec_len, uint16_t expect_count,
                               char (*out_labels)[FGIR_MAX_LABEL]) {
    uint32_t magic;
    uint16_t count;
    size_t pos = 0;
    int i;

    if (!sec || sec_len < 6 || !out_labels) {
        return -1;
    }
    magic = read_u32_le(sec);
    if (magic != FLAT_FB_LABEL_MAGIC) {
        return -1;
    }
    count = read_u16_le(sec + 4);
    if (count != expect_count) {
        return -1;
    }
    pos = 6;
    for (i = 0; i < (int)count; i++) {
        uint16_t ulen;
        if (pos + 2 > sec_len) {
            return -1;
        }
        ulen = read_u16_le(sec + pos);
        pos += 2;
        if (ulen == 0 || ulen >= FGIR_MAX_LABEL || pos + ulen > sec_len) {
            return -1;
        }
        memcpy(out_labels[i], sec + pos, ulen);
        out_labels[i][ulen] = '\0';
        pos += ulen;
    }
    return pos == sec_len ? 0 : -1;
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

size_t flat_fb_fnun_section_size(int unit_count, const FlatFbNativeUnit *units,
                                 uint16_t block_count) {
    size_t n = 4 + 2;
    int ui;

    if (unit_count <= 0 || !units || block_count == 0) {
        return 0;
    }
    for (ui = 0; ui < unit_count; ui++) {
        int di;
        n += 1;
        for (di = 0; di < units[ui].decl_count; di++) {
            n += 2 + strlen(units[ui].decls[di].module) + strlen(units[ui].decls[di].alias);
        }
    }
    n += 2 + (size_t)block_count;
    return n;
}

int flat_fb_write_fnun_section(FILE *f, int unit_count, const FlatFbNativeUnit *units,
                               uint16_t block_count, const uint8_t *block_unit) {
    uint32_t magic = FLAT_FB_FNUN_MAGIC;
    uint16_t uc = (uint16_t)unit_count;
    int ui;

    if (!f || unit_count <= 0 || !units || block_count == 0 || !block_unit) {
        return -1;
    }
    if (write_bytes(f, &magic, 4) != 0 || write_bytes(f, &uc, 2) != 0) {
        return -1;
    }
    for (ui = 0; ui < unit_count; ui++) {
        uint8_t dc = (uint8_t)units[ui].decl_count;
        int di;
        if (write_bytes(f, &dc, 1) != 0) {
            return -1;
        }
        for (di = 0; di < units[ui].decl_count; di++) {
            uint8_t ml = (uint8_t)strlen(units[ui].decls[di].module);
            uint8_t al = (uint8_t)strlen(units[ui].decls[di].alias);
            if (ml == 0 || write_bytes(f, &ml, 1) != 0 ||
                write_bytes(f, units[ui].decls[di].module, ml) != 0 ||
                write_bytes(f, &al, 1) != 0 ||
                (al > 0 && write_bytes(f, units[ui].decls[di].alias, al) != 0)) {
                return -1;
            }
        }
    }
    if (write_bytes(f, &block_count, 2) != 0) {
        return -1;
    }
    {
        size_t i;
        for (i = 0; i < (size_t)block_count; i++) {
            if (write_bytes(f, &block_unit[i], 1) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

static int parse_ffun_section(const uint8_t *sec, size_t sec_len, FlatFbFunctionEntry **out_funs,
                               uint16_t *out_count) {
    size_t pos = 0;
    uint32_t magic;
    uint16_t count;
    FlatFbFunctionEntry *funs = NULL;
    int i;

    if (!sec || !out_funs || !out_count) {
        return -1;
    }
    *out_funs = NULL;
    *out_count = 0;
    if (sec_len < 6) {
        return -1;
    }
    magic = read_u32_le(sec);
    if (magic != FLAT_FB_FFUN_MAGIC) {
        return -1;
    }
    count = read_u16_le(sec + 4);
    pos = 6;
    if (count == 0) {
        return pos == sec_len ? 0 : -1;
    }
    funs = calloc(count, sizeof(FlatFbFunctionEntry));
    if (!funs) {
        return -1;
    }
    for (i = 0; i < (int)count; i++) {
        uint16_t nl;
        uint16_t rl;
        if (pos + 2 > sec_len) {
            goto fail;
        }
        nl = read_u16_le(sec + pos);
        pos += 2;
        if (nl == 0 || nl >= sizeof funs[i].name || pos + nl + 4 + 4 + 1 + 2 > sec_len) {
            goto fail;
        }
        memcpy(funs[i].name, sec + pos, nl);
        funs[i].name[nl] = '\0';
        pos += nl;
        funs[i].impl_block_id = read_u32_le(sec + pos);
        pos += 4;
        funs[i].executor_block_id = read_u32_le(sec + pos);
        pos += 4;
        funs[i].param_count = sec[pos];
        pos += 1;
        if (pos + 2 > sec_len) {
            goto fail;
        }
        rl = read_u16_le(sec + pos);
        pos += 2;
        if (rl >= sizeof funs[i].return_name || pos + rl + 4 > sec_len) {
            goto fail;
        }
        memcpy(funs[i].return_name, sec + pos, rl);
        funs[i].return_name[rl] = '\0';
        pos += rl;
        funs[i].func_vcx_slots = read_u32_le(sec + pos);
        pos += 4;
    }
    if (pos != sec_len) {
        goto fail;
    }
    *out_funs = funs;
    *out_count = count;
    return 0;
fail:
    free(funs);
    return -1;
}

static int parse_fnun_section(const uint8_t *sec, size_t sec_len, uint16_t expect_blocks,
                              FlatFbNativeUnit **out_units, int *out_unit_count,
                              uint8_t **out_block_unit) {
    size_t pos = 0;
    uint32_t magic;
    uint16_t unit_count;
    int ui;

    if (!sec || !out_units || !out_unit_count || !out_block_unit) {
        return -1;
    }
    *out_units = NULL;
    *out_unit_count = 0;
    *out_block_unit = NULL;
    if (sec_len < 6) {
        return -1;
    }
    magic = read_u32_le(sec);
    if (magic != FLAT_FB_FNUN_MAGIC) {
        return -1;
    }
    unit_count = read_u16_le(sec + 4);
    pos = 6;
    if (unit_count == 0 || unit_count > FGIR_MAX_NATIVE_UNITS) {
        return -1;
    }
    *out_units = calloc(unit_count, sizeof(FlatFbNativeUnit));
    if (!*out_units) {
        return -1;
    }
    *out_unit_count = (int)unit_count;
    for (ui = 0; ui < (int)unit_count; ui++) {
        uint8_t dc;
        int di;
        if (pos + 1 > sec_len) {
            return -1;
        }
        dc = sec[pos++];
        if (dc > FGIR_MAX_OPTIONAL_NATIVES) {
            return -1;
        }
        (*out_units)[ui].decl_count = dc;
        for (di = 0; di < dc; di++) {
            uint8_t ml;
            uint8_t al;
            if (pos + 2 > sec_len) {
                return -1;
            }
            ml = sec[pos++];
            if (pos + ml + 1 > sec_len) {
                return -1;
            }
            if (ml >= FGIR_NATIVE_MODULE_LEN) {
                return -1;
            }
            memcpy((*out_units)[ui].decls[di].module, sec + pos, ml);
            (*out_units)[ui].decls[di].module[ml] = '\0';
            pos += ml;
            al = sec[pos++];
            if (pos + al > sec_len) {
                return -1;
            }
            if (al >= FGIR_NATIVE_ALIAS_LEN) {
                return -1;
            }
            if (al > 0) {
                memcpy((*out_units)[ui].decls[di].alias, sec + pos, al);
                (*out_units)[ui].decls[di].alias[al] = '\0';
                pos += al;
            } else {
                (*out_units)[ui].decls[di].alias[0] = '\0';
            }
        }
    }
    if (pos + 2 > sec_len) {
        return -1;
    }
    {
        uint16_t bc = read_u16_le(sec + pos);
        size_t i;
        pos += 2;
        if (bc != expect_blocks) {
            return -1;
        }
        if (pos + bc > sec_len) {
            return -1;
        }
        *out_block_unit = malloc(bc);
        if (!*out_block_unit) {
            return -1;
        }
        for (i = 0; i < (size_t)bc; i++) {
            (*out_block_unit)[i] = sec[pos++];
        }
    }
    return pos == sec_len ? 0 : -1;
}

static int flat_fb_parse_loaded(uint8_t *buf, size_t fsize, FlatBytecode *out) {
    size_t dir_bytes;
    size_t header_size;
    size_t dir_offset;
    uint16_t count;
    uint32_t native_off = 0;
    FlatBlockEntry *blocks = NULL;
    size_t i;

    if (check_magic(buf, fsize) != 0) {
        free(buf);
        fprintf(stderr, "flvm: bad magic (expected FLAT)\n");
        return -1;
    }

    if (buf[4] != FLAT_FB_SENTINEL) {
        free(buf);
        fprintf(stderr, "flvm: unsupported .fb (legacy header without version); rebuild with "
                        "flc (format v%u required)\n", (unsigned)FLAT_FB_FORMAT_VERSION);
        return -1;
    }
    if (buf[5] != 3 && buf[5] != 4 && buf[5] != FLAT_FB_FORMAT_VERSION) {
        unsigned got = (unsigned)buf[5];
        free(buf);
        fprintf(stderr, "flvm: unsupported .fb format v%u (format v3–v%u required); rebuild with "
                        "flc\n", got, (unsigned)FLAT_FB_FORMAT_VERSION);
        return -1;
    }
    out->format_version = (int)buf[5];
    if (out->format_version >= 4 && fsize < FLAT_FB_HEADER_SIZE_V4) {
        free(buf);
        fprintf(stderr, "flvm: truncated .fb header\n");
        return -1;
    }
    if (out->format_version >= 5 && fsize < FLAT_FB_HEADER_SIZE) {
        free(buf);
        fprintf(stderr, "flvm: truncated .fb header\n");
        return -1;
    }
    if (out->format_version == 3 && fsize < FLAT_FB_HEADER_SIZE_V3) {
        free(buf);
        fprintf(stderr, "flvm: truncated .fb header\n");
        return -1;
    }
    count = read_u16_le(buf + 6);
    native_off = read_u32_le(buf + 8);
    out->var_slots = read_u32_le(buf + 12);
    {
        uint32_t labels_off = read_u32_le(buf + 16);
        uint32_t fnun_off = 0;
        uint32_t ffun_off = 0;

        if (out->format_version >= 4) {
            fnun_off = read_u32_le(buf + 20);
        }
        if (out->format_version >= 5) {
            ffun_off = read_u32_le(buf + 24);
        }
        (void)ffun_off;

        if (labels_off != 0 && labels_off <= fsize) {
            out->code_size = labels_off;
        } else if (fnun_off != 0 && fnun_off <= fsize) {
            out->code_size = fnun_off;
        } else if (native_off != 0 && native_off <= fsize) {
            out->code_size = native_off;
        } else {
            out->code_size = fsize;
        }
        if (labels_off != 0) {
            size_t label_len;
            if (labels_off >= fsize) {
                free(buf);
                fprintf(stderr, "flvm: block label offset out of range\n");
                return -1;
            }
            if (fnun_off != 0 && fnun_off > labels_off) {
                label_len = (size_t)(fnun_off - labels_off);
            } else if (ffun_off != 0 && ffun_off > labels_off) {
                label_len = (size_t)(ffun_off - labels_off);
            } else if (native_off != 0 && native_off > labels_off) {
                label_len = (size_t)(native_off - labels_off);
            } else {
                label_len = (size_t)(fsize - labels_off);
            }
            out->block_labels = calloc(count, FGIR_MAX_LABEL);
            if (!out->block_labels) {
                free(buf);
                return -1;
            }
            if (parse_label_section(buf + labels_off, label_len, count, out->block_labels) != 0) {
                free(out->block_labels);
                out->block_labels = NULL;
                free(buf);
                fprintf(stderr, "flvm: invalid block label section\n");
                return -1;
            }
        }
        if (fnun_off != 0 && fnun_off < fsize) {
            size_t fnun_len;
            if (ffun_off != 0 && ffun_off > fnun_off) {
                fnun_len = (size_t)(ffun_off - fnun_off);
            } else if (native_off != 0 && native_off > fnun_off) {
                fnun_len = (size_t)(native_off - fnun_off);
            } else {
                fnun_len = (size_t)(fsize - fnun_off);
            }
            if (parse_fnun_section(buf + fnun_off, fnun_len, count, &out->native_units,
                                   &out->native_unit_count, &out->block_native_unit) != 0) {
                free(buf);
                fprintf(stderr, "flvm: invalid native unit section\n");
                return -1;
            }
        }
        if (ffun_off != 0 && ffun_off < fsize) {
            size_t ffun_len;
            if (native_off != 0 && native_off > ffun_off) {
                ffun_len = (size_t)(native_off - ffun_off);
            } else {
                ffun_len = (size_t)(fsize - ffun_off);
            }
            if (parse_ffun_section(buf + ffun_off, ffun_len, &out->functions, &out->function_count) !=
                0) {
                free(buf);
                fprintf(stderr, "flvm: invalid FFUN section\n");
                return -1;
            }
        }
    }
    if (out->var_slots > (uint32_t)FLAT_MAX_VARS) {
        free(buf);
        fprintf(stderr, "flvm: var_slots %u exceeds FLAT_MAX_VARS (%d)\n",
                (unsigned)out->var_slots, FLAT_MAX_VARS);
        return -1;
    }
    if (out->format_version >= 5) {
        header_size = FLAT_FB_HEADER_SIZE;
    } else if (out->format_version >= 4) {
        header_size = FLAT_FB_HEADER_SIZE_V4;
    } else {
        header_size = FLAT_FB_HEADER_SIZE_V3;
    }
    dir_offset = header_size;

    if (count == 0) {
        free(buf);
        fprintf(stderr, "flvm: block_count is zero\n");
        return -1;
    }

    dir_bytes = (size_t)count * sizeof(FlatBlockEntry);
    if (header_size + dir_bytes > fsize) {
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
        if (blocks[i].offset >= fsize) {
            free(blocks);
            free(buf);
            fprintf(stderr, "flvm: block offset out of range\n");
            return -1;
        }
    }

    if (native_off != 0) {
        if (native_off >= fsize) {
            free(blocks);
            free(buf);
            fprintf(stderr, "flvm: native section offset out of range\n");
            return -1;
        }
        if (flat_fb_parse_native_section(buf + native_off, fsize - native_off,
                                         out->optional_natives, FGIR_MAX_OPTIONAL_NATIVES,
                                         &out->optional_native_count) != 0) {
            free(blocks);
            free(buf);
            fprintf(stderr, "flvm: invalid optional native manifest\n");
            return -1;
        }
    }

    out->data = buf;
    out->size = fsize;
    out->blocks = blocks;
    out->block_count = count;
    return 0;
}

int flat_fb_load_buffer(const uint8_t *data, size_t len, FlatBytecode *out) {
    uint8_t *buf;

    if (!data || !out || len < FLAT_FB_MAGIC_SIZE + 2) {
        return -1;
    }
    memset(out, 0, sizeof *out);
    buf = malloc(len);
    if (!buf) {
        return -1;
    }
    memcpy(buf, data, len);
    return flat_fb_parse_loaded(buf, len, out);
}

int flat_fb_load_file(const char *path, FlatBytecode *out) {
    FILE *f;
    long fsize;
    uint8_t *buf = NULL;

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

    if (flat_fb_parse_loaded(buf, (size_t)fsize, out) != 0) {
        fprintf(stderr, "flvm: invalid bytecode (%s)\n", path);
        return -1;
    }
    return 0;
}

void flat_fb_free(FlatBytecode *bc) {
    if (!bc) {
        return;
    }
    free(bc->data);
    free(bc->blocks);
    free(bc->block_labels);
    free(bc->native_units);
    free(bc->block_native_unit);
    free(bc->functions);
    bc->functions = NULL;
    bc->function_count = 0;
    memset(bc, 0, sizeof *bc);
}

size_t flat_fb_ffun_section_size(uint16_t count, const FlatFbFunctionEntry *entries) {
    size_t n = 4 + 2;
    int i;

    if (count == 0 || !entries) {
        return n;
    }
    for (i = 0; i < (int)count; i++) {
        size_t nl = strlen(entries[i].name);
        size_t rl = strlen(entries[i].return_name);
        n += 2 + nl + 4 + 4 + 1 + 2 + rl + 4;
    }
    return n;
}

int flat_fb_write_ffun_section(FILE *f, uint16_t count, const FlatFbFunctionEntry *entries) {
    uint32_t magic = FLAT_FB_FFUN_MAGIC;
    int i;

    if (!f) {
        return -1;
    }
    if (write_bytes(f, &magic, 4) != 0 || write_bytes(f, &count, 2) != 0) {
        return -1;
    }
    for (i = 0; i < (int)count; i++) {
        uint16_t nl = (uint16_t)strlen(entries[i].name);
        uint16_t rl = (uint16_t)strlen(entries[i].return_name);
        if (write_bytes(f, &nl, 2) != 0 || write_bytes(f, entries[i].name, nl) != 0) {
            return -1;
        }
        if (write_bytes(f, &entries[i].impl_block_id, 4) != 0 ||
            write_bytes(f, &entries[i].executor_block_id, 4) != 0) {
            return -1;
        }
        {
            uint8_t pc = entries[i].param_count;
            if (write_bytes(f, &pc, 1) != 0) {
                return -1;
            }
        }
        if (write_bytes(f, &rl, 2) != 0 || write_bytes(f, entries[i].return_name, rl) != 0 ||
            write_bytes(f, &entries[i].func_vcx_slots, 4) != 0) {
            return -1;
        }
    }
    return 0;
}

int flat_fb_write_min_ok(const char *path) {
    FILE *f;
    const uint8_t magic[4] = { 'F', 'L', 'A', 'T' };
    const uint8_t sentinel[1] = { FLAT_FB_SENTINEL };
    const uint8_t version[1] = { FLAT_FB_FORMAT_VERSION };
    const uint8_t count[2] = { 1, 0 };
    const uint8_t native_off[4] = { 0, 0, 0, 0 };
    const uint8_t var_slots[4] = { 0, 0, 0, 0 };
    const uint8_t ffun_off_bytes[4] = { 0, 0, 0, 0 };
    const uint8_t labels_off_bytes[4] = { 38, 0, 0, 0 }; /* header28 + dir8 + code2 */
    const uint8_t fnun_off_bytes[4] = { 51, 0, 0, 0 };
    const uint8_t dir[8] = {
        0, 0, 0, 0, /* block_id main = 0 */
        36, 0, 0, 0  /* code offset after 28-byte header */
    };
    const uint8_t code[2] = { OP_OK, 0 };
    const uint32_t blbl_magic = FLAT_FB_LABEL_MAGIC;
    const uint16_t blbl_count = 1;
    const uint16_t main_len = 5;
    const char main_label[] = "#main";

    f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "mkfb: cannot write %s\n", path);
        return -1;
    }
    {
        const uint32_t fnun_magic = FLAT_FB_FNUN_MAGIC;
        const uint16_t unit_count = 1;
        const uint8_t zero_decl = 0;
        const uint16_t block_count = 1;
        const uint8_t block_unit = 0;

        if (write_bytes(f, magic, 4) != 0 || write_bytes(f, sentinel, 1) != 0 ||
            write_bytes(f, version, 1) != 0 || write_bytes(f, count, 2) != 0 ||
            write_bytes(f, native_off, 4) != 0 || write_bytes(f, var_slots, 4) != 0 ||
            write_bytes(f, labels_off_bytes, 4) != 0 || write_bytes(f, fnun_off_bytes, 4) != 0 ||
            write_bytes(f, ffun_off_bytes, 4) != 0 || write_bytes(f, dir, 8) != 0 ||
            write_bytes(f, code, 2) != 0 ||
            write_bytes(f, &blbl_magic, 4) != 0 || write_bytes(f, &blbl_count, 2) != 0 ||
            write_bytes(f, &main_len, 2) != 0 || write_bytes(f, main_label, main_len) != 0 ||
            write_bytes(f, &fnun_magic, 4) != 0 || write_bytes(f, &unit_count, 2) != 0 ||
            write_bytes(f, &zero_decl, 1) != 0 || write_bytes(f, &block_count, 2) != 0 ||
            write_bytes(f, &block_unit, 1) != 0) {
            fclose(f);
            return -1;
        }
    }
    fclose(f);
    return 0;
}

