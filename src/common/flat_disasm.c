#include "flat_disasm.h"
#include "flat.h"
#include "flat_native.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *disasm_opcode_name(uint8_t opcode) {
    switch ((OpCode)opcode) {
    case OP_ASSIGN:
        return "ASSIGN";
    case OP_GUARD:
        return "GUARD";
    case OP_CALL:
        return "CALL";
    case OP_CALL_BLOCK:
        return "CALL_BLOCK";
    case OP_RETURN:
        return "RETURN";
    case OP_NEXT:
        return "NEXT";
    case OP_FOREACH:
        return "FOREACH";
    case OP_FOR:
        return "FOR";
    case OP_OK:
        return "OK";
    case OP_FAIL:
        return "FAIL";
    case OP_PANIC:
        return "PANIC";
    case OP_IO:
        return "IO";
    case OP_NATIVE:
        return "NATIVE";
    case OP_FREE:
        return "FREE";
    case OP_PANIC_RESET:
        return "PANIC_RESET";
    case OP_PANIC_REASON:
        return "PANIC_REASON";
    case OP_JUMP:
        return "JUMP";
    case OP_CALL_FUNCTION:
        return "CALL_FUNCTION";
    default:
        return "UNKNOWN";
    }
}

void flat_disasm_opts_init(FlatDisasmOpts *opts) {
    if (!opts) {
        return;
    }
    memset(opts, 0, sizeof *opts);
    opts->block_id = -1;
}

static uint32_t read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint16_t read_u16_le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static int block_index_for_id(const FlatBytecode *bc, uint32_t block_id) {
    int i;
    for (i = 0; i < (int)bc->block_count; i++) {
        if (bc->blocks[i].block_id == block_id) {
            return i;
        }
    }
    return -1;
}

static const char *label_for_block_index(const FlatBytecode *bc, int bi) {
    if (bi < 0 || bi >= (int)bc->block_count || !bc->block_labels) {
        return NULL;
    }
    if (bc->block_labels[bi][0] == '\0') {
        return NULL;
    }
    return bc->block_labels[bi];
}

static uint32_t block_end_ip(const FlatBytecode *bc, uint32_t start_ip) {
    uint32_t end = (uint32_t)bc->code_size;
    int i;
    for (i = 0; i < (int)bc->block_count; i++) {
        uint32_t off = bc->blocks[i].offset;
        if (off > start_ip && off < end) {
            end = off;
        }
    }
    return end;
}

static void print_payload(FILE *out, uint8_t opcode, const uint8_t *payload, uint8_t psz,
                            const FlatBytecode *bc) {
    size_t i;

    switch ((OpCode)opcode) {
    case OP_ASSIGN:
        if (psz > 0) {
            uint8_t mod = payload[0];
            const char *line = (const char *)(payload + 1);
            fprintf(out, " mod=%u", (unsigned)mod);
            if (line[0] != '\0') {
                fprintf(out, " %s", line);
            }
            if (mod == FLAT_LINE_MOD_TILDE && psz >= 1 + strlen(line) + 1 + 4) {
                fprintf(out, " or_ip=%" PRIu32, read_u32_le(payload + 1 + strlen(line) + 1));
            }
        }
        break;
    case OP_NATIVE:
        if (psz >= 2) {
            uint16_t nid = read_u16_le(payload);
            const char *inner = (const char *)(payload + 2);
            const char *bind = inner + strlen(inner) + 1;
            fprintf(out, " id=%u (%s)", (unsigned)nid, flat_native_name(nid));
            if (inner[0] != '\0') {
                fprintf(out, " inner=%s", inner);
            }
            if (bind[0] != '\0') {
                fprintf(out, " bind=%s", bind);
            }
        }
        break;
    case OP_GUARD:
        if (psz >= 4) {
            uint32_t bt = read_u32_le(payload);
            fprintf(out, " or_ip=%" PRIu32 " expr=%s", bt, (const char *)(payload + 4));
        }
        break;
    case OP_CALL:
        if (psz >= 4) {
            uint32_t t = read_u32_le(payload);
            const char *bind = (const char *)(payload + 4);
            fprintf(out, " target_ip=%" PRIu32, t);
            if (bind[0] != '\0') {
                fprintf(out, " bind=%s", bind);
            }
        }
        break;
    case OP_CALL_BLOCK:
        if (psz > 0) {
            const char *expr = (const char *)payload;
            const char *bind = expr + strlen(expr) + 1;
            fprintf(out, " expr=%s", expr);
            if (bind < (const char *)payload + psz && bind[0] != '\0') {
                fprintf(out, " bind=%s", bind);
            }
        }
        break;
    case OP_JUMP:
    case OP_PANIC:
        if (psz >= 4) {
            fprintf(out, " target_ip=%" PRIu32, read_u32_le(payload));
        }
        break;
    case OP_FOREACH:
    case OP_FOR:
        if (psz >= 4) {
            uint32_t t = read_u32_le(payload);
            fprintf(out, " loop_ip=%" PRIu32 " line=%s", t, (const char *)(payload + 4));
        }
        break;
    case OP_CALL_FUNCTION:
        if (psz >= 4) {
            uint16_t fi = read_u16_le(payload);
            fprintf(out, " func_index=%u argc=%u global=%u ret=%s", (unsigned)fi,
                    (unsigned)payload[2], (unsigned)payload[3],
                    (const char *)(payload + 4));
        }
        break;
    case OP_FREE:
    case OP_PANIC_REASON:
    case OP_IO:
        if (psz > 0) {
            fprintf(out, " %s", (const char *)payload);
        }
        break;
    case OP_OK:
    case OP_FAIL:
    case OP_RETURN:
    case OP_NEXT:
        break;
    default:
        fprintf(out, " hex=");
        for (i = 0; i < (size_t)psz; i++) {
            fprintf(out, "%02x", payload[i]);
        }
        break;
    }
    (void)bc;
}

static int block_passes_filter(const FlatBytecode *bc, int bi, const FlatDisasmOpts *opts) {
    if (opts->block_id >= 0 && (uint32_t)opts->block_id != bc->blocks[bi].block_id) {
        return 0;
    }
    if (opts->block_label[0] != '\0') {
        const char *lab = label_for_block_index(bc, bi);
        if (!lab || strcmp(lab, opts->block_label) != 0) {
            return 0;
        }
    }
    return 1;
}

static void print_header(FILE *out, const FlatBytecode *bc) {
    const uint8_t *h = bc->data;
    uint32_t native_off;
    uint32_t var_slots;
    uint32_t labels_off = 0;
    uint32_t fnun_off = 0;
    uint32_t ffun_off = 0;

    if (!h || bc->size < FLAT_FB_HEADER_SIZE_V3) {
        return;
    }
    native_off = read_u32_le(h + 8);
    var_slots = read_u32_le(h + 12);
    if (bc->format_version >= 4) {
        labels_off = read_u32_le(h + 16);
        fnun_off = read_u32_le(h + 20);
    }
    if (bc->format_version >= 5) {
        ffun_off = read_u32_le(h + 24);
    }
    fprintf(out, "=== .fb header ===\n");
    fprintf(out, "format_version: %d\n", bc->format_version);
    fprintf(out, "block_count: %u\n", (unsigned)bc->block_count);
    fprintf(out, "var_slots: %" PRIu32 "\n", var_slots);
    fprintf(out, "code_size: %zu\n", bc->code_size);
    fprintf(out, "file_size: %zu\n", bc->size);
    fprintf(out, "optional_native_off: %" PRIu32 "\n", native_off);
    if (bc->format_version >= 4) {
        fprintf(out, "labels_off: %" PRIu32 "\n", labels_off);
        fprintf(out, "fnun_off: %" PRIu32 "\n", fnun_off);
    }
    if (bc->format_version >= 5) {
        fprintf(out, "ffun_off: %" PRIu32 "\n", ffun_off);
    }
}

static void print_block_directory(FILE *out, const FlatBytecode *bc) {
    int i;
    fprintf(out, "\n=== block directory ===\n");
    for (i = 0; i < (int)bc->block_count; i++) {
        const char *lab = label_for_block_index(bc, i);
        fprintf(out, "  id=%" PRIu32 " ip=%" PRIu32, bc->blocks[i].block_id,
                bc->blocks[i].offset);
        if (lab) {
            fprintf(out, " label=%s", lab);
        }
        if (bc->block_native_unit) {
            fprintf(out, " native_unit=%u", (unsigned)bc->block_native_unit[i]);
        }
        fputc('\n', out);
    }
}

static void print_optional_natives(FILE *out, const FlatBytecode *bc) {
    int i;
    fprintf(out, "\n=== optional natives (FNAT) ===\n");
    if (bc->optional_native_count <= 0) {
        fprintf(out, "  (none)\n");
        return;
    }
    for (i = 0; i < bc->optional_native_count; i++) {
        fprintf(out, "  %s", bc->optional_natives[i].module);
        if (bc->optional_natives[i].alias[0] != '\0') {
            fprintf(out, " with %s", bc->optional_natives[i].alias);
        }
        fputc('\n', out);
    }
}

static void print_fnun(FILE *out, const FlatBytecode *bc) {
    int ui;
    fprintf(out, "\n=== FNUN ===\n");
    if (!bc->native_units || bc->native_unit_count <= 0) {
        fprintf(out, "  (none)\n");
        return;
    }
    for (ui = 0; ui < bc->native_unit_count; ui++) {
        int di;
        fprintf(out, "  unit %d:\n", ui);
        for (di = 0; di < bc->native_units[ui].decl_count; di++) {
            fprintf(out, "    %s", bc->native_units[ui].decls[di].module);
            if (bc->native_units[ui].decls[di].alias[0] != '\0') {
                fprintf(out, " with %s", bc->native_units[ui].decls[di].alias);
            }
            fputc('\n', out);
        }
    }
}

static void print_ffun(FILE *out, const FlatBytecode *bc) {
    int i;
    fprintf(out, "\n=== FFUN ===\n");
    if (!bc->functions || bc->function_count == 0) {
        fprintf(out, "  (none)\n");
        return;
    }
    for (i = 0; i < (int)bc->function_count; i++) {
        const FlatFbFunctionEntry *f = &bc->functions[i];
        const char *impl = NULL;
        const char *exec = NULL;
        int ib = block_index_for_id(bc, f->impl_block_id);
        int eb = block_index_for_id(bc, f->executor_block_id);
        if (ib >= 0) {
            impl = label_for_block_index(bc, ib);
        }
        if (eb >= 0) {
            exec = label_for_block_index(bc, eb);
        }
        fprintf(out, "  [%d] %s params=%u return=%s vcx_slots=%" PRIu32 "\n", i, f->name,
                (unsigned)f->param_count, f->return_name, f->func_vcx_slots);
        fprintf(out, "      impl_block_id=%" PRIu32, f->impl_block_id);
        if (impl) {
            fprintf(out, " (%s)", impl);
        }
        fprintf(out, " executor_block_id=%" PRIu32, f->executor_block_id);
        if (exec) {
            fprintf(out, " (%s)", exec);
        }
        fputc('\n', out);
    }
}

typedef struct {
    unsigned long counts[256];
    unsigned long total;
} OpcodeHist;

static int disasm_block(const FlatBytecode *bc, FILE *out, int bi, const FlatDisasmOpts *opts,
                          OpcodeHist *hist) {
    uint32_t ip = bc->blocks[bi].offset;
    uint32_t end = block_end_ip(bc, ip);
    const char *lab = label_for_block_index(bc, bi);

    fprintf(out, "\n--- block id=%" PRIu32, bc->blocks[bi].block_id);
    if (lab) {
        fprintf(out, " %s", lab);
    }
    fprintf(out, " (ip %" PRIu32 "..%" PRIu32 ") ---\n", ip, end);

    while (ip < end) {
        uint8_t opcode;
        uint8_t psz;
        const uint8_t *payload;

        if ((size_t)ip + 2 > bc->code_size) {
            fprintf(out, "  ip=%" PRIu32 " TRUNCATED\n", ip);
            return -1;
        }
        opcode = bc->data[ip];
        psz = bc->data[ip + 1];
        if ((size_t)ip + 2 + psz > bc->code_size) {
            fprintf(out, "  ip=%" PRIu32 " payload overflow (op 0x%02x len %u)\n", ip, opcode,
                    (unsigned)psz);
            return -1;
        }
        payload = &bc->data[ip + 2];

        if (!opts->summary) {
            fprintf(out, "  ip=%-6" PRIu32 " %s(0x%02x) len=%u", ip, disasm_opcode_name(opcode),
                    opcode, (unsigned)psz);
            print_payload(out, opcode, payload, psz, bc);
            fputc('\n', out);
        }

        if (hist) {
            hist->counts[opcode]++;
            hist->total++;
        }

        ip += 2u + psz;
    }
    return 0;
}

static void print_histogram(FILE *out, const FlatBytecode *bc, const OpcodeHist *hist) {
    int op;
    fprintf(out, "\n=== summary ===\n");
    fprintf(out, "code_bytes: %zu\n", bc->code_size);
    fprintf(out, "instructions: %lu\n", hist->total);
    fprintf(out, "opcode histogram:\n");
    for (op = 0; op < 256; op++) {
        if (hist->counts[op] == 0) {
            continue;
        }
        fprintf(out, "  %s(0x%02x): %lu\n", disasm_opcode_name((uint8_t)op), op, hist->counts[op]);
    }
}

int flat_disasm_print(const FlatBytecode *bc, FILE *out, const FlatDisasmOpts *opts) {
    OpcodeHist hist = {0};
    FlatDisasmOpts def;
    int bi;
    int any = 0;

    if (!bc || !out) {
        return -1;
    }
    if (!opts) {
        flat_disasm_opts_init(&def);
        opts = &def;
    }

    print_header(out, bc);
    print_block_directory(out, bc);

    if (!opts->no_natives) {
        print_optional_natives(out, bc);
        print_fnun(out, bc);
    }
    if (!opts->no_ffun) {
        print_ffun(out, bc);
    }

    if (opts->sections_only) {
        return 0;
    }

    for (bi = 0; bi < (int)bc->block_count; bi++) {
        if (!block_passes_filter(bc, bi, opts)) {
            continue;
        }
        any = 1;
        if (disasm_block(bc, out, bi, opts, &hist) != 0) {
            return -1;
        }
    }

    if (opts->block_id >= 0 || opts->block_label[0] != '\0') {
        if (!any) {
            fprintf(stderr, "fbdump: no block matches filter\n");
            return -1;
        }
    }

    if (opts->summary) {
        print_histogram(out, bc, &hist);
    }

    return 0;
}

int flat_disasm_file(const char *path, FILE *out, const FlatDisasmOpts *opts) {
    FlatBytecode bc;

    memset(&bc, 0, sizeof bc);
    if (flat_fb_load_file(path, &bc) != 0) {
        return -1;
    }
    if (flat_disasm_print(&bc, out, opts) != 0) {
        flat_fb_free(&bc);
        return -1;
    }
    flat_fb_free(&bc);
    return 0;
}

int flat_disasm_usage_fbdump(const char *prog) {
    fprintf(stderr, "Usage: %s [options] program.fb\n", prog);
    fprintf(stderr, "  --summary          opcode histogram + instruction count\n");
    fprintf(stderr, "  --sections-only    header and metadata sections only\n");
    fprintf(stderr, "  --block ID|#label  disassemble one block\n");
    fprintf(stderr, "  --no-ffun          omit FFUN listing\n");
    fprintf(stderr, "  --no-natives       omit FNAT/FNUN listing\n");
    fprintf(stderr, "  -h                 help\n");
    return 2;
}

int flat_disasm_parse_args(int argc, char **argv, int start, FlatDisasmOpts *opts,
                            const char **path_out) {
    int i;

    if (!opts || !path_out) {
        return 2;
    }
    flat_disasm_opts_init(opts);
    *path_out = NULL;

    for (i = start; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            return 1;
        }
        if (strcmp(argv[i], "--summary") == 0) {
            opts->summary = 1;
            continue;
        }
        if (strcmp(argv[i], "--sections-only") == 0) {
            opts->sections_only = 1;
            continue;
        }
        if (strcmp(argv[i], "--no-ffun") == 0) {
            opts->no_ffun = 1;
            continue;
        }
        if (strcmp(argv[i], "--no-natives") == 0) {
            opts->no_natives = 1;
            continue;
        }
        if (strcmp(argv[i], "--block") == 0) {
            if (i + 1 >= argc) {
                return 2;
            }
            i++;
            if (argv[i][0] == '#') {
                snprintf(opts->block_label, sizeof opts->block_label, "%s", argv[i]);
            } else {
                char *end = NULL;
                long v = strtol(argv[i], &end, 10);
                if (end == argv[i] || (end && *end)) {
                    return 2;
                }
                opts->block_id = (int)v;
            }
            continue;
        }
        if (argv[i][0] == '-') {
            return 2;
        }
        *path_out = argv[i];
        return 0;
    }
    return 2;
}
