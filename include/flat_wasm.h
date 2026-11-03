#ifndef FLAT_WASM_H
#define FLAT_WASM_H

#include <stddef.h>
#include <stdint.h>

/* compile single buffer → writes /fleditor_src.fl then links (legacy). */
int flat_wasm_compile(const char *source, uint8_t **out_fb, size_t *out_len, char *err,
                      size_t err_sz);

/* compile+link from entry path on MEMFS (multi-file project). */
int flat_wasm_compile_entry(const char *entry_path, uint8_t **out_fb, size_t *out_len, char *err,
                            size_t err_sz);

/* run bytecode; exit code like flvm (0 ok). err optional diagnostic. */
int flat_wasm_run(const uint8_t *fb, size_t len, char *err, size_t err_sz);

/* compile + run in-process (preferred for browser glue). */
int flat_wasm_run_source(const char *source, char *err, size_t err_sz);

int flat_wasm_run_entry(const char *entry_path, char *err, size_t err_sz);

void flat_wasm_free(void *p);

/* Last flat_vm_run termination (FlatTermination); 0 if no run yet. */
int flat_wasm_get_last_termination(void);

/* flatL toolchain version string (same as flc/flvm). */
const char *flat_wasm_toolchain_version(void);

#endif
