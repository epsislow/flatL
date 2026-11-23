#include "flat_env.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    int use_process;
    char *keys[FLAT_ENV_MAX_OVERRIDES];
    char *vals[FLAT_ENV_MAX_OVERRIDES];
    size_t n;
    char *args[FLAT_ENV_MAX_ARGS];
    size_t argc;
} FlatEnvState;

static FlatEnvState g_env = {1, {0}, {0}, 0, {0}, 0};

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
}

void flat_env_set_use_process(int on) {
    g_env.use_process = on ? 1 : 0;
}

int flat_env_use_process(void) {
    return g_env.use_process;
}

int flat_env_override_set(const char *key, const char *val) {
    size_t i;
    char *k, *v;
    if (!key || !key[0] || strlen(key) > FLAT_ENV_KEY_MAX) return -1;
    if (!val) val = "";
    if (strlen(val) > FLAT_ENV_VAL_MAX) return -1;
    for (i = 0; i < g_env.n; ++i) {
        if (strcmp(g_env.keys[i], key) == 0) {
            v = dup_str(val);
            if (!v) return -1;
            free(g_env.vals[i]);
            g_env.vals[i] = v;
            return 0;
        }
    }
    if (g_env.n >= FLAT_ENV_MAX_OVERRIDES) return -1;
    k = dup_str(key);
    v = dup_str(val);
    if (!k || !v) {
        free(k);
        free(v);
        return -1;
    }
    g_env.keys[g_env.n] = k;
    g_env.vals[g_env.n] = v;
    g_env.n++;
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
    return g_env.n;
}

int flat_env_lookup(const char *key, char *out, size_t out_sz, int *found) {
    size_t i;
    const char *v = NULL;
    if (found) *found = 0;
    if (!key || !key[0] || !out || out_sz == 0) return -1;
    out[0] = '\0';
    for (i = 0; i < g_env.n; ++i) {
        if (strcmp(g_env.keys[i], key) == 0) {
            v = g_env.vals[i] ? g_env.vals[i] : "";
            if (found) *found = 1;
            snprintf(out, out_sz, "%s", v);
            return 0;
        }
    }
    if (g_env.use_process) {
        v = getenv(key);
        if (v) {
            if (found) *found = 1;
            snprintf(out, out_sz, "%s", v);
            return 0;
        }
    }
    return 0;
}

int flat_env_args_clear(void) {
    size_t i;
    for (i = 0; i < g_env.argc; ++i) {
        free(g_env.args[i]);
        g_env.args[i] = NULL;
    }
    g_env.argc = 0;
    return 0;
}

int flat_env_args_push(const char *arg) {
    char *a;
    if (g_env.argc >= FLAT_ENV_MAX_ARGS) return -1;
    a = dup_str(arg ? arg : "");
    if (!a) return -1;
    g_env.args[g_env.argc++] = a;
    return 0;
}

size_t flat_env_argc(void) {
    return g_env.argc;
}

const char *flat_env_arg(size_t index) {
    if (index >= g_env.argc) return NULL;
    return g_env.args[index];
}
