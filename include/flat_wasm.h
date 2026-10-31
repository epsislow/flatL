#ifndef FLAT_WASM_H
#define FLAT_WASM_H

#include <stddef.h>
#include <stdint.h>

/* compile: returns 0 ok; *out_fb malloc'd (free with flat_wasm_free). */
int flat_wasm_compile(const char *source, uint8_t **out_fb, size_t *out_len, char *err,
                      size_t err_sz);

/* run bytecode; exit code like flvm (0 ok). err optional diagnostic. */
int flat_wasm_run(const uint8_t *fb, size_t len, char *err, size_t err_sz);

/* compile + run in-process (preferred for browser glue). */
int flat_wasm_run_source(const char *source, char *err, size_t err_sz);

void flat_wasm_free(void *p);

#endif
