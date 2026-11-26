#include "flat_env.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>

/* WASM must never fall back to host getenv (Node/browser process env). */
#ifdef FLAT_WASM_BUILD
static FlatEnvContext g_env = {0, {0}, {0}, 0, {0}, 0};
#else
static FlatEnvContext g_env = {1, {0}, {0}, 0, {0}, 0};
#endif

/* Serializes host staging updates with per-VM snapshot creation. */
static atomic_flag g_env_lock = ATOMIC_FLAG_INIT;
static void env_lock(void) {
    while (atomic_flag_test_and_set_explicit(&g_env_lock, memory_order_acquire)) { }
}
static void env_unlock(void) {
    atomic_flag_clear_explicit(&g_env_lock, memory_order_release);
}

static char *dup_str(const char *s) {
    size_t n;
    char *p;
    if (!s) s = "";
    n = strlen(s);
    p = (char *)malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n + 1);
    return p;
}

void flat_env_reset(void) {
    size_t i;
    env_lock();
    for (i = 0; i < g_env.n; ++i) {
        free(g_env.keys[i]);
        free(g_env.vals[i]);
        g_env.keys[i] = NULL;
        g_env.vals[i] = NULL;
    }
    g_env.n = 0;
    for (i = 0; i < g_env.argc; ++i) {
        free(g_env.args[i]);
        g_env.args[i] = NULL;
    }
    g_env.argc = 0;
    env_unlock();
}

void flat_env_set_use_process(int on) {
    env_lock();
    g_env.use_process = on ? 1 : 0;
    env_unlock();
}

int flat_env_use_process(void) {
    int on;
    env_lock();
    on = g_env.use_process;
    env_unlock();
    return on;
}

int flat_env_override_set(const char *key, const char *val) {
    size_t i;
    char *k, *v;
    if (!key || !key[0] || strlen(key) > FLAT_ENV_KEY_MAX) return -1;
    if (!val) val = "";
    if (strlen(val) > FLAT_ENV_VAL_MAX) return -1;
    env_lock();
    for (i = 0; i < g_env.n; ++i) {
        if (strcmp(g_env.keys[i], key) == 0) {
            v = dup_str(val);
            if (!v) { env_unlock(); return -1; }
            free(g_env.vals[i]);
            g_env.vals[i] = v;
            env_unlock();
            return 0;
        }
    }
    if (g_env.n >= FLAT_ENV_MAX_OVERRIDES) { env_unlock(); return -1; }
    k = dup_str(key);
    v = dup_str(val);
    if (!k || !v) {
        free(k);
        free(v);
        env_unlock();
        return -1;
    }
    g_env.keys[g_env.n] = k;
    g_env.vals[g_env.n] = v;
    g_env.n++;
    env_unlock();
    return 0;
}

int flat_env_override_parse(const char *key_eq_val) {
    const char *eq;
    char key[FLAT_ENV_KEY_MAX + 1];
    size_t kn;
    if (!key_eq_val || !(eq = strchr(key_eq_val, '='))) return -1;
    kn = (size_t)(eq - key_eq_val);
    if (kn == 0 || kn > FLAT_ENV_KEY_MAX) return -1;
    memcpy(key, key_eq_val, kn);
    key[kn] = '\0';
    return flat_env_override_set(key, eq + 1);
}

size_t flat_env_override_count(void) {
    size_t n;
    env_lock();
    n = g_env.n;
    env_unlock();
    return n;
}

int flat_env_lookup(const char *key, char *out, size_t out_sz, int *found) {
    size_t i;
    const char *v = NULL;
    int rc = 0;
    if (found) *found = 0;
    if (!key || !key[0] || !out || out_sz == 0) return -1;
    out[0] = '\0';
    env_lock();
    for (i = 0; i < g_env.n; ++i) {
        if (strcmp(g_env.keys[i], key) == 0) {
            v = g_env.vals[i] ? g_env.vals[i] : "";
            if (found) *found = 1;
            snprintf(out, out_sz, "%s", v);
            env_unlock();
            return 0;
        }
    }
    if (g_env.use_process) {
        v = getenv(key);
        if (v) {
            if (found) *found = 1;
            snprintf(out, out_sz, "%s", v);
        }
    }
    env_unlock();
    return rc;
}

int flat_env_args_clear(void) {
    size_t i;
    env_lock();
    for (i = 0; i < g_env.argc; ++i) {
        free(g_env.args[i]);
        g_env.args[i] = NULL;
    }
    g_env.argc = 0;
    env_unlock();
    return 0;
}

int flat_env_args_push(const char *arg) {
    char *a;
    env_lock();
    if (g_env.argc >= FLAT_ENV_MAX_ARGS) { env_unlock(); return -1; }
    a = dup_str(arg ? arg : "");
    if (!a) { env_unlock(); return -1; }
    g_env.args[g_env.argc++] = a;
    env_unlock();
    return 0;
}

size_t flat_env_argc(void) {
    size_t argc;
    env_lock();
    argc = g_env.argc;
    env_unlock();
    return argc;
}

const char *flat_env_arg(size_t index) {
    const char *arg;
    env_lock();
    arg = (index >= g_env.argc) ? NULL : g_env.args[index];
    env_unlock();
    return arg; /* borrowed; invalidated by the next staging mutation */
}


int flat_env_context_init(FlatEnvContext *ctx) {
    size_t i;
    if (!ctx) return -1;
    memset(ctx, 0, sizeof *ctx);
    env_lock();
    ctx->use_process = g_env.use_process;
    for (i = 0; i < g_env.n; ++i) {
        ctx->keys[i] = dup_str(g_env.keys[i]);
        ctx->vals[i] = dup_str(g_env.vals[i]);
        ctx->n++;
        if (!ctx->keys[i] || !ctx->vals[i]) goto fail;
    }
    for (i = 0; i < g_env.argc; ++i) {
        ctx->args[i] = dup_str(g_env.args[i]);
        if (!ctx->args[i]) goto fail;
        ctx->argc++;
    }
    env_unlock();
    return 0;
fail:
    env_unlock();
    flat_env_context_destroy(ctx);
    return -1;
}

int flat_env_context_init_empty(FlatEnvContext *ctx, int use_process) {
    if (!ctx) return -1;
    memset(ctx, 0, sizeof *ctx);
    ctx->use_process = use_process ? 1 : 0;
    return 0;
}

int flat_env_context_clone(FlatEnvContext *dst, const FlatEnvContext *src) {
    size_t i;
    if (!dst || !src || dst == src || src->n > FLAT_ENV_MAX_OVERRIDES ||
        src->argc > FLAT_ENV_MAX_ARGS) return -1;
    memset(dst, 0, sizeof *dst);
    dst->use_process = src->use_process;
    for (i = 0; i < src->n; ++i) {
        if (!src->keys[i] || !src->vals[i]) {
            flat_env_context_destroy(dst);
            return -1;
        }
        dst->keys[i] = dup_str(src->keys[i]);
        dst->vals[i] = dup_str(src->vals[i]);
        if (!dst->keys[i] || !dst->vals[i]) {
            dst->n = i + 1;
            flat_env_context_destroy(dst);
            return -1;
        }
        dst->n++;
    }
    for (i = 0; i < src->argc; ++i) {
        if (!src->args[i]) {
            flat_env_context_destroy(dst);
            return -1;
        }
        dst->args[i] = dup_str(src->args[i]);
        if (!dst->args[i]) {
            dst->argc = i;
            flat_env_context_destroy(dst);
            return -1;
        }
        dst->argc++;
    }
    return 0;
}

void flat_env_context_destroy(FlatEnvContext *ctx) {
    size_t i;
    if (!ctx) return;
    for (i = 0; i < ctx->n; ++i) { free(ctx->keys[i]); free(ctx->vals[i]); }
    for (i = 0; i < ctx->argc; ++i) free(ctx->args[i]);
    memset(ctx, 0, sizeof *ctx);
}

int flat_env_context_set_use_process(FlatEnvContext *ctx, int on) {
    if (!ctx) return -1;
    ctx->use_process = on ? 1 : 0;
    return 0;
}

int flat_env_context_override_set(FlatEnvContext *ctx, const char *key, const char *val) {
    size_t i;
    char *copy;
    if (!ctx || !key || !key[0] || strlen(key) > FLAT_ENV_KEY_MAX) return -1;
    if (!val) val = "";
    if (strlen(val) > FLAT_ENV_VAL_MAX) return -1;
    for (i = 0; i < ctx->n; ++i) {
        if (strcmp(ctx->keys[i], key) == 0) {
            copy = dup_str(val);
            if (!copy) return -1;
            free(ctx->vals[i]);
            ctx->vals[i] = copy;
            return 0;
        }
    }
    if (ctx->n >= FLAT_ENV_MAX_OVERRIDES) return -1;
    ctx->keys[ctx->n] = dup_str(key);
    ctx->vals[ctx->n] = dup_str(val);
    if (!ctx->keys[ctx->n] || !ctx->vals[ctx->n]) {
        free(ctx->keys[ctx->n]);
        free(ctx->vals[ctx->n]);
        ctx->keys[ctx->n] = NULL;
        ctx->vals[ctx->n] = NULL;
        return -1;
    }
    ctx->n++;
    return 0;
}

int flat_env_context_args_clear(FlatEnvContext *ctx) {
    size_t i;
    if (!ctx) return -1;
    for (i = 0; i < ctx->argc; ++i) {
        free(ctx->args[i]);
        ctx->args[i] = NULL;
    }
    ctx->argc = 0;
    return 0;
}

int flat_env_context_args_push(FlatEnvContext *ctx, const char *arg) {
    char *copy;
    if (!ctx || ctx->argc >= FLAT_ENV_MAX_ARGS) return -1;
    copy = dup_str(arg ? arg : "");
    if (!copy) return -1;
    ctx->args[ctx->argc++] = copy;
    return 0;
}

int flat_env_context_lookup(const FlatEnvContext *ctx, const char *key, char *out,
                            size_t out_sz, int *found) {
    size_t i;
    const char *v;
    if (!ctx) return flat_env_lookup(key, out, out_sz, found);
    if (!key || !key[0] || !out || out_sz == 0) return -1;
    if (found) *found = 0;
    out[0] = '\0';
    for (i = 0; i < ctx->n; ++i) {
        if (strcmp(ctx->keys[i], key) == 0) {
            snprintf(out, out_sz, "%s", ctx->vals[i] ? ctx->vals[i] : "");
            if (found) *found = 1;
            return 0;
        }
    }
    /* The host process environment is treated as immutable during a VM run. */
    if (ctx->use_process && (v = getenv(key)) != NULL) {
        snprintf(out, out_sz, "%s", v);
        if (found) *found = 1;
    }
    return 0;
}

size_t flat_env_context_argc(const FlatEnvContext *ctx) { return ctx ? ctx->argc : flat_env_argc(); }
const char *flat_env_context_arg(const FlatEnvContext *ctx, size_t index) {
    return ctx ? ((index >= ctx->argc) ? NULL : ctx->args[index]) : flat_env_arg(index);
}
