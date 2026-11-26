#ifndef FLAT_ENV_H
#define FLAT_ENV_H

#include <stddef.h>

#define FLAT_ENV_MAX_OVERRIDES 128u
#define FLAT_ENV_MAX_ARGS 64u
#define FLAT_ENV_KEY_MAX 256u
#define FLAT_ENV_VAL_MAX 8192u

typedef struct FlatEnvContext {
    int use_process;
    char *keys[FLAT_ENV_MAX_OVERRIDES];
    char *vals[FLAT_ENV_MAX_OVERRIDES];
    size_t n;
    char *args[FLAT_ENV_MAX_ARGS];
    size_t argc;
} FlatEnvContext;

/* Process staging configuration, copied into each VM at run start. */
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

/* Per-VM snapshot API. Context must be zero-initialized before init. */
int flat_env_context_init(FlatEnvContext *ctx);
/* Build an independently owned context without consulting process staging. */
int flat_env_context_init_empty(FlatEnvContext *ctx, int use_process);
/* Deep-copy an existing context. dst must be zero-initialized and not alias src. */
int flat_env_context_clone(FlatEnvContext *dst, const FlatEnvContext *src);
void flat_env_context_destroy(FlatEnvContext *ctx);
int flat_env_context_set_use_process(FlatEnvContext *ctx, int on);
int flat_env_context_override_set(FlatEnvContext *ctx, const char *key, const char *val);
int flat_env_context_args_clear(FlatEnvContext *ctx);
int flat_env_context_args_push(FlatEnvContext *ctx, const char *arg);
int flat_env_context_lookup(const FlatEnvContext *ctx, const char *key, char *out,
                            size_t out_sz, int *found);
size_t flat_env_context_argc(const FlatEnvContext *ctx);
const char *flat_env_context_arg(const FlatEnvContext *ctx, size_t index);

#endif
