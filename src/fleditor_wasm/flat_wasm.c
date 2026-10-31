#include "flat_wasm.h"
#include "fb_format.h"
#include "flat_link.h"
#include "fgir.h"
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FLAT_WASM_SRC_PATH "/fleditor_src.fl"
#define FLAT_WASM_FB_PATH "/fleditor_out.fb"

static int read_file_bytes(const char *path, uint8_t **out, size_t *out_len, char *err,
                           size_t err_sz) {
    FILE *f;
    long fsize;
    uint8_t *buf;

    f = fopen(path, "rb");
    if (!f) {
        snprintf(err, err_sz, "cannot read %s", path);
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        snprintf(err, err_sz, "seek failed");
        return -1;
    }
    fsize = ftell(f);
    if (fsize < 0) {
        fclose(f);
        snprintf(err, err_sz, "tell failed");
        return -1;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        snprintf(err, err_sz, "seek failed");
        return -1;
    }
    buf = malloc((size_t)fsize);
    if (!buf) {
        fclose(f);
        snprintf(err, err_sz, "out of memory");
        return -1;
    }
    if (fread(buf, 1, (size_t)fsize, f) != (size_t)fsize) {
        free(buf);
        fclose(f);
        snprintf(err, err_sz, "read failed");
        return -1;
    }
    fclose(f);
    *out = buf;
    *out_len = (size_t)fsize;
    return 0;
}

static int write_source_file(const char *source, char *err, size_t err_sz) {
    FILE *f;

    f = fopen(FLAT_WASM_SRC_PATH, "w");
    if (!f) {
        snprintf(err, err_sz, "cannot write temp source");
        return -1;
    }
    if (fputs(source, f) == EOF) {
        fclose(f);
        snprintf(err, err_sz, "write temp source failed");
        return -1;
    }
    fclose(f);
    return 0;
}

int flat_wasm_compile(const char *source, uint8_t **out_fb, size_t *out_len, char *err,
                      size_t err_sz) {
    FgirProgram *prog;
    FlatLinkOptions link_opts = {0};
    int rc = -1;

    if (!source || !out_fb || !out_len) {
        if (err && err_sz) {
            snprintf(err, err_sz, "invalid arguments");
        }
        return -1;
    }
    *out_fb = NULL;
    *out_len = 0;

    prog = calloc(1, sizeof(FgirProgram));
    if (!prog) {
        snprintf(err, err_sz, "out of memory");
        return -1;
    }

    if (write_source_file(source, err, err_sz) != 0) {
        goto done;
    }
    if (flat_link_program(FLAT_WASM_SRC_PATH, &link_opts, prog, err, err_sz) != 0) {
        goto done;
    }
    if (flat_compile_fgir(prog, FLAT_WASM_FB_PATH, err, err_sz) != 0) {
        goto done;
    }
    if (read_file_bytes(FLAT_WASM_FB_PATH, out_fb, out_len, err, err_sz) != 0) {
        goto done;
    }
    rc = 0;

done:
    free(prog);
    return rc;
}

int flat_wasm_run_source(const char *source, char *err, size_t err_sz) {
    uint8_t *fb = NULL;
    size_t len = 0;
    int rc;

    if (flat_wasm_compile(source, &fb, &len, err, err_sz) != 0) {
        return 1;
    }
    rc = flat_wasm_run(fb, len, err, err_sz);
    flat_wasm_free(fb);
    return rc;
}

int flat_wasm_run(const uint8_t *fb, size_t len, char *err, size_t err_sz) {
    FlatBytecode bc;
    int rc;

    if (!fb || len == 0) {
        if (err && err_sz) {
            snprintf(err, err_sz, "empty bytecode");
        }
        return 1;
    }
    memset(&bc, 0, sizeof bc);
    if (flat_fb_load_buffer(fb, len, &bc) != 0) {
        if (err && err_sz) {
            snprintf(err, err_sz, "invalid .fb bytecode");
        }
        return 1;
    }
    rc = flat_vm_run(&bc);
    flat_fb_free(&bc);
    if (rc != 0 && err && err_sz) {
        snprintf(err, err_sz, "program exited with code %d", rc);
    }
    return rc;
}

void flat_wasm_free(void *p) {
    free(p);
}
