#ifndef FLAT_ENV_H
#define FLAT_ENV_H

#include <stddef.h>

#define FLAT_ENV_MAX_OVERRIDES 128u
#define FLAT_ENV_MAX_ARGS 64u
#define FLAT_ENV_KEY_MAX 256u
#define FLAT_ENV_VAL_MAX 8192u

/* Process-wide env/argv view for native `env.*` (set by flvm/flatl/WASM before run). */
void flat_env_reset(void);
void flat_env_set_use_process(int on); /* 1 = also getenv (native); 0 = overrides only (WASM) */
int flat_env_use_process(void);

/* Override map (--env / fleditor / tests). Last set wins for same key. */
int flat_env_override_set(const char *key, const char *val);
int flat_env_override_parse(const char *key_eq_val); /* "KEY=VAL" */
size_t flat_env_override_count(void);

/* Lookup: overrides first, then getenv if use_process. *found=1 if key exists. */
int flat_env_lookup(const char *key, char *out, size_t out_sz, int *found);

/* Program argv after `--` on the CLI (or fleditor program args). */
int flat_env_args_clear(void);
int flat_env_args_push(const char *arg);
size_t flat_env_argc(void);
const char *flat_env_arg(size_t index);

#endif
