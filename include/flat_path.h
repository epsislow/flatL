#ifndef FLAT_PATH_H
#define FLAT_PATH_H

#include <stddef.h>

#define FLAT_PATH_MAX 4096u

/* Effective path separator for this process / WASM run (host default or override). */
char flat_path_sep(void);
void flat_path_sep_reset(void);          /* restore host default */
int flat_path_sep_set(char sep);         /* '/' or '\\' only; 0 ok, -1 bad */
int flat_path_sep_set_str(const char *s); /* "/", "\\", or "\" */

int flat_path_is_sep(char c);

/* Pure string ops — out must hold FLAT_PATH_MAX. Return 0 ok, -1 error (*why optional). */
int flat_path_join(const char *const *parts, int nparts, char *out, size_t out_sz, const char **why);
int flat_path_base(const char *p, char *out, size_t out_sz, const char **why);
int flat_path_dir(const char *p, char *out, size_t out_sz, const char **why);
int flat_path_ext(const char *p, char *out, size_t out_sz, const char **why);
int flat_path_normalize(const char *p, char *out, size_t out_sz, const char **why);
int flat_path_is_abs(const char *p, int *out_abs, const char **why);

#endif
