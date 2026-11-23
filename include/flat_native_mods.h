#ifndef FLAT_NATIVE_MODS_H
#define FLAT_NATIVE_MODS_H

#include <stddef.h>
#include <stdio.h>

/* Build-time feature switches (Makefile: http=0 / env=0). */
#ifndef FLAT_WITH_HTTP
#define FLAT_WITH_HTTP 1
#endif
#ifndef FLAT_WITH_ENV
#define FLAT_WITH_ENV 1
#endif

/* 1 if this toolchain binary was built with the named module. */
int flat_native_mod_is_built(const char *mod);

/* Write built module ids, one per line, to out. Returns count written. */
int flat_native_mods_print(FILE *out);

/* Space-separated module list into buf. Returns bytes written (excl. NUL), or -1. */
int flat_native_mods_format(char *buf, size_t buf_sz);

#endif
