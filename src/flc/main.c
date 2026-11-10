#include "fgir.h"
#include "flat.h"
#include "flat_disasm.h"
#include "flat_link.h"
#include "flat_port.h"
#include "flat_time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void print_version(void) {
    printf("flc %s (flatL compiler)\n", FLAT_TOOLCHAIN_VERSION);
}

static int usage(const char *prog) {
    fprintf(stderr, "Usage: %s --version\n", prog);
    fprintf(stderr, "       %s --dump-ir program.fl\n", prog);
    fprintf(stderr, "       %s --dump-bc [options] program.fl\n", prog);
    fprintf(stderr, "       %s [--verbose] [--time] [-o out.fb] [--dump-bc] program.fl\n", prog);
    fprintf(stderr, "\n  --dump-bc     bytecode disassembly to stdout (fbdump options allowed)\n");
    fprintf(stderr, "  --time        print phase timings on stderr (flc: time_ms …)\n");
    fprintf(stderr, "  FLAT_TIME=1   same as --time when flag omitted\n");
    return 2;
}

static void flc_print_time(double link_ms, double compile_ms, int units, int fmod, double total_ms) {
    char body[512];

    flat_time_line_begin(body, sizeof body);
    flat_time_append_field(body, sizeof body, "link", link_ms);
    flat_time_append_field(body, sizeof body, "compile", compile_ms);
    flat_time_append_field(body, sizeof body, "total", total_ms);
    flat_time_append_int_field(body, sizeof body, "units", units);
    flat_time_append_int_field(body, sizeof body, "fmod", fmod);
    flat_time_fprintf_time_ms("flc", body);
}

int main(int argc, char **argv) {
    FgirProgram *prog;
    char err[512];
    const char *in_path = NULL;
    const char *out_path = NULL;
    int dump_ir = 0;
    int dump_bc = 0;
    int report_time = 0;
    int disasm_arg_start = 0;
    FlatDisasmOpts disasm_opts;
    FlatLinkOptions link_opts = {0};
    FlatLinkReport link_report = {0};
    int i;
    int rc = 0;
    double t0 = 0.0;
    double t_link = 0.0;
    double t_compile = 0.0;
    double t_end = 0.0;

    report_time = flat_time_env_enabled();

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-V") == 0) {
            print_version();
            return 0;
        }
    }

    prog = calloc(1, sizeof(FgirProgram));
    if (!prog) {
        fprintf(stderr, "flc: out of memory\n");
        return 1;
    }
    flat_disasm_opts_init(&disasm_opts);

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dump-ir") == 0) {
            dump_ir = 1;
            continue;
        }
        if (strcmp(argv[i], "--dump-bc") == 0) {
            dump_bc = 1;
            disasm_arg_start = i + 1;
            continue;
        }
        if (strcmp(argv[i], "--verbose") == 0) {
            link_opts.verbose = 1;
            continue;
        }
        if (strcmp(argv[i], "--time") == 0) {
            report_time = 1;
            continue;
        }
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_path = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--summary") == 0 || strcmp(argv[i], "--sections-only") == 0 ||
            strcmp(argv[i], "--no-ffun") == 0 || strcmp(argv[i], "--no-natives") == 0) {
            continue;
        }
        if (strcmp(argv[i], "--block") == 0) {
            if (i + 1 < argc) {
                i++;
            }
            continue;
        }
        if (argv[i][0] == '-') {
            if (dump_bc) {
                continue;
            }
            rc = usage(argv[0]);
            goto done;
        }
        if (!in_path) {
            in_path = argv[i];
        }
    }

    if (dump_bc && disasm_arg_start > 0) {
        const char *parsed_in = NULL;
        int dpr = flat_disasm_parse_args(argc, argv, disasm_arg_start, &disasm_opts, &parsed_in);
        if (dpr == 1) {
            rc = flat_disasm_usage_fbdump(argv[0]);
            goto done;
        }
        if (parsed_in) {
            if (in_path && strcmp(in_path, parsed_in) != 0) {
                fprintf(stderr, "flc: ambiguous input path\n");
                rc = 2;
                goto done;
            }
            in_path = parsed_in;
        }
    }

    if (!in_path) {
        rc = usage(argv[0]);
        goto done;
    }

    if (dump_ir && dump_bc) {
        fprintf(stderr, "flc: --dump-ir and --dump-bc are mutually exclusive\n");
        rc = 2;
        goto done;
    }

    if (report_time) {
        t0 = flat_time_monotonic_ms();
    }

    if (flat_link_program(in_path, &link_opts, prog, &link_report, err, sizeof err) != 0) {
        fprintf(stderr, "flc: %s\n", err);
        rc = 1;
        if (report_time) {
            t_end = flat_time_monotonic_ms();
            flc_print_time(t_end - t0, 0.0, 0, 0, t_end - t0);
        }
        goto done;
    }
    if (report_time) {
        t_link = flat_time_monotonic_ms();
    }

    if (dump_ir) {
        flat_fgir_dump(prog, stdout);
        rc = 0;
        if (report_time) {
            t_end = flat_time_monotonic_ms();
            flc_print_time(t_link - t0, 0.0, link_report.units, link_report.fmod, t_end - t0);
        }
        goto done;
    }

    if (!dump_bc && !out_path) {
        fprintf(stderr, "flc: missing -o out.fb\n");
        rc = 1;
        if (report_time) {
            t_end = flat_time_monotonic_ms();
            flc_print_time(t_link - t0, 0.0, link_report.units, link_report.fmod, t_end - t0);
        }
        goto done;
    }

    {
        char tmp_fb[FLAT_PORT_PATH_MAX];
        const char *emit_path = out_path;
        int need_unlink = 0;

        if (dump_bc && !emit_path) {
            if (flat_port_make_temp_fb_path(tmp_fb, sizeof tmp_fb, NULL) != 0) {
                fprintf(stderr, "flc: cannot create temp .fb path\n");
                rc = 1;
                goto done;
            }
            emit_path = tmp_fb;
            need_unlink = 1;
        }

        if (flat_compile_fgir(prog, emit_path, err, sizeof err) != 0) {
            fprintf(stderr, "flc: compile error: %s\n", err);
            rc = 1;
            if (report_time) {
                t_end = flat_time_monotonic_ms();
                flc_print_time(t_link - t0, t_end - t_link, link_report.units, link_report.fmod,
                               t_end - t0);
            }
            goto done;
        }

        if (dump_bc) {
            if (flat_disasm_file(emit_path, stdout, &disasm_opts) != 0) {
                rc = 1;
            } else {
                rc = 0;
            }
        }

        if (need_unlink) {
            unlink(emit_path);
        }
    }

    if (report_time) {
        t_compile = flat_time_monotonic_ms();
        t_end = t_compile;
        flc_print_time(t_link - t0, t_compile - t_link, link_report.units, link_report.fmod,
                       t_end - t0);
    }
done:
    free(prog);
    return rc;
}
