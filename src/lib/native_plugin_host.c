#include "flat_native_plugin.h"
#include "flat_native.h"
#include "flat_native_mod.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || \
    defined(__OpenBSD__)
#define FLAT_HAVE_DLOPEN 1
#include <dlfcn.h>
#endif

static int (*g_handlers[FLAT_NATIVE_COUNT])(FlatNativeCtx *);
static FlatOptionalNativeDecl g_manifest[FGIR_MAX_OPTIONAL_NATIVES];
static int g_manifest_count;
static char g_search_dir[512];
static int g_loaded_math_trig;
static int g_loaded_math_stats;
static int g_loaded_math;
static int g_loaded_list;

static void register_embedded_mem(void) {
    flat_native_plugin_register(FLAT_NATIVE_MEM_VARS_MAX, flat_native_mem_vars_max);
    flat_native_plugin_register(FLAT_NATIVE_MEM_VARS_USED, flat_native_mem_vars_used);
    flat_native_plugin_register(FLAT_NATIVE_MEM_VARS_AVAIL, flat_native_mem_vars_avail);
    flat_native_plugin_register(FLAT_NATIVE_MEM_LIST_MAX, flat_native_mem_list_max);
    flat_native_plugin_register(FLAT_NATIVE_MEM_LIST_USED, flat_native_mem_list_used);
    flat_native_plugin_register(FLAT_NATIVE_MEM_LIST_AVAIL, flat_native_mem_list_avail);
    flat_native_plugin_register(FLAT_NATIVE_MEM_OBJECT_MAX, flat_native_mem_object_max);
    flat_native_plugin_register(FLAT_NATIVE_MEM_OBJECT_USED, flat_native_mem_object_used);
    flat_native_plugin_register(FLAT_NATIVE_MEM_OBJECT_AVAIL, flat_native_mem_object_avail);
    flat_native_plugin_register(FLAT_NATIVE_MEM_STRREF_MAX, flat_native_mem_strref_max);
    flat_native_plugin_register(FLAT_NATIVE_MEM_STRREF_USED, flat_native_mem_strref_used);
    flat_native_plugin_register(FLAT_NATIVE_MEM_STRREF_AVAIL, flat_native_mem_strref_avail);
    flat_native_plugin_register(FLAT_NATIVE_MEM_STR_USED, flat_native_mem_str_used);
    flat_native_plugin_register(FLAT_NATIVE_MEM_BOOL_USED, flat_native_mem_bool_used);
    flat_native_plugin_register(FLAT_NATIVE_MEM_NUM_USED, flat_native_mem_num_used);
    flat_native_plugin_register(FLAT_NATIVE_MEM_NULL_USED, flat_native_mem_null_used);
}

static int manifest_has_module(const char *mod) {
    int i;
    for (i = 0; i < g_manifest_count; i++) {
        if (strcmp(g_manifest[i].module, mod) == 0) {
            return 1;
        }
    }
    return 0;
}

static void module_to_so_basename(const char *mod, char *out, size_t out_sz) {
    size_t i;
    snprintf(out, out_sz, "libflat_");
    if (strlen(out) + strlen(mod) + 8 >= out_sz) {
        out[0] = '\0';
        return;
    }
    {
        char *p = out + strlen(out);
        for (i = 0; mod[i]; i++) {
            p[i] = mod[i] == '.' ? '_' : mod[i];
        }
        p[i] = '\0';
    }
    snprintf(out + strlen(out), out_sz - strlen(out), ".so");
}

static int try_dlopen_path(const char *path, char *err, size_t err_size) {
#ifdef FLAT_HAVE_DLOPEN
    void *h;
    FlatNativePluginInitFn init;
    if (!path || !path[0]) {
        return -1;
    }
    h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        snprintf(err, err_size, "dlopen %s: %s", path, dlerror());
        return -1;
    }
    init = (FlatNativePluginInitFn)dlsym(h, FLAT_NATIVE_PLUGIN_INIT_SYMBOL);
    if (!init) {
        snprintf(err, err_size, "dlsym %s: %s", FLAT_NATIVE_PLUGIN_INIT_SYMBOL, dlerror());
        dlclose(h);
        return -1;
    }
    if (init(flat_native_plugin_register) != 0) {
        snprintf(err, err_size, "plugin init failed: %s", path);
        dlclose(h);
        return -1;
    }
    (void)h;
    return 0;
#else
    (void)path;
    snprintf(err, err_size, "optional native plugins require dlopen (not built on this OS)");
    return -1;
#endif
}

static int load_module_plugin(const char *mod, char *err, size_t err_size) {
    char base[128];
    char path[640];
    const char *env;

    if (strcmp(mod, FLAT_NATIVE_MODULE_MATH_TRIG) == 0 && g_loaded_math_trig) {
        return 0;
    }
    if (strcmp(mod, FLAT_NATIVE_MODULE_MATH_STATS) == 0 && g_loaded_math_stats) {
        return 0;
    }
    if (strcmp(mod, FLAT_NATIVE_MODULE_MATH) == 0 && g_loaded_math) {
        return 0;
    }
    if (strcmp(mod, FLAT_NATIVE_MODULE_LIST) == 0 && g_loaded_list) {
        return 0;
    }

    module_to_so_basename(mod, base, sizeof base);
    if (!base[0]) {
        snprintf(err, err_size, "invalid module name for plugin");
        return -1;
    }

    env = getenv("FLAT_NATIVE_PATH");
    if (env && env[0]) {
        char buf[512];
        size_t k;
        snprintf(buf, sizeof buf, "%s", env);
        for (k = 0; buf[k]; k++) {
            size_t start = k;
            while (buf[k] && buf[k] != ':') {
                k++;
            }
            if (k > start) {
                char dir[512];
                snprintf(dir, sizeof dir, "%.*s", (int)(k - start), buf + start);
                snprintf(path, sizeof path, "%s/%s", dir, base);
                if (try_dlopen_path(path, err, err_size) == 0) {
                    goto ok;
                }
            }
            if (buf[k] == ':') {
                continue;
            }
            break;
        }
    }

    if (g_search_dir[0]) {
        snprintf(path, sizeof path, "%s/%s", g_search_dir, base);
        if (try_dlopen_path(path, err, err_size) == 0) {
            goto ok;
        }
    }

    snprintf(path, sizeof path, "bin/%s", base);
    if (try_dlopen_path(path, err, err_size) == 0) {
        goto ok;
    }

#ifndef FLAT_HAVE_DLOPEN
    if (strcmp(mod, FLAT_NATIVE_MODULE_MATH_TRIG) == 0) {
        flat_native_plugin_register(FLAT_NATIVE_MATH_TRIG_SIN, flat_native_math_trig_sin);
        flat_native_plugin_register(FLAT_NATIVE_MATH_TRIG_COS, flat_native_math_trig_cos);
        flat_native_plugin_register(FLAT_NATIVE_MATH_TRIG_TAN, flat_native_math_trig_tan);
        flat_native_plugin_register(FLAT_NATIVE_MATH_TRIG_NUM_PI, flat_native_math_trig_num_pi);
        flat_native_plugin_register(FLAT_NATIVE_MATH_TRIG_NUM_TAU, flat_native_math_trig_num_tau);
        flat_native_plugin_register(FLAT_NATIVE_MATH_TRIG_DEG_TO_RAD, flat_native_math_trig_deg_to_rad);
        flat_native_plugin_register(FLAT_NATIVE_MATH_TRIG_RAD_TO_DEG, flat_native_math_trig_rad_to_deg);
        flat_native_plugin_register(FLAT_NATIVE_MATH_TRIG_SIN_DEG, flat_native_math_trig_sin_deg);
        flat_native_plugin_register(FLAT_NATIVE_MATH_TRIG_COS_DEG, flat_native_math_trig_cos_deg);
        flat_native_plugin_register(FLAT_NATIVE_MATH_TRIG_TAN_DEG, flat_native_math_trig_tan_deg);
        g_loaded_math_trig = 1;
        return 0;
    }
    if (strcmp(mod, FLAT_NATIVE_MODULE_MATH_STATS) == 0) {
        flat_native_plugin_register(FLAT_NATIVE_MATH_STATS_MEAN, flat_native_math_stats_mean);
        flat_native_plugin_register(FLAT_NATIVE_MATH_STATS_MEDIAN, flat_native_math_stats_median);
        flat_native_plugin_register(FLAT_NATIVE_MATH_STATS_VAR, flat_native_math_stats_var);
        flat_native_plugin_register(FLAT_NATIVE_MATH_STATS_STDEV, flat_native_math_stats_stdev);
        g_loaded_math_stats = 1;
        return 0;
    }
    if (strcmp(mod, FLAT_NATIVE_MODULE_MATH) == 0) {
        flat_native_plugin_register(FLAT_NATIVE_MATH_POW, flat_native_math_pow);
        flat_native_plugin_register(FLAT_NATIVE_MATH_SQRT, flat_native_math_sqrt);
        flat_native_plugin_register(FLAT_NATIVE_MATH_EXP, flat_native_math_exp);
        flat_native_plugin_register(FLAT_NATIVE_MATH_LOG, flat_native_math_log);
        flat_native_plugin_register(FLAT_NATIVE_MATH_LOG10, flat_native_math_log10);
        flat_native_plugin_register(FLAT_NATIVE_MATH_FLOOR, flat_native_math_floor);
        flat_native_plugin_register(FLAT_NATIVE_MATH_CEIL, flat_native_math_ceil);
        flat_native_plugin_register(FLAT_NATIVE_MATH_ROUND, flat_native_math_round);
        flat_native_plugin_register(FLAT_NATIVE_MATH_MOD, flat_native_math_mod);
        flat_native_plugin_register(FLAT_NATIVE_MATH_FLOOR_MOD, flat_native_math_floor_mod);
        flat_native_plugin_register(FLAT_NATIVE_MATH_SUM, flat_native_math_sum);
        flat_native_plugin_register(FLAT_NATIVE_MATH_MIN, flat_native_math_min);
        flat_native_plugin_register(FLAT_NATIVE_MATH_MAX, flat_native_math_max);
        flat_native_plugin_register(FLAT_NATIVE_MATH_SUM2, flat_native_math_sum2);
        flat_native_plugin_register(FLAT_NATIVE_MATH_MIN2, flat_native_math_min2);
        flat_native_plugin_register(FLAT_NATIVE_MATH_MAX2, flat_native_math_max2);
        flat_native_plugin_register(FLAT_NATIVE_MATH_NUM_PI, flat_native_math_num_pi);
        flat_native_plugin_register(FLAT_NATIVE_MATH_NUM_TAU, flat_native_math_num_tau);
        flat_native_plugin_register(FLAT_NATIVE_MATH_NUM_E, flat_native_math_num_e);
        g_loaded_math = 1;
        return 0;
    }
    if (strcmp(mod, FLAT_NATIVE_MODULE_LIST) == 0) {
        flat_native_plugin_register(FLAT_NATIVE_LIST_EMPTY, flat_native_list_empty);
        flat_native_plugin_register(FLAT_NATIVE_LIST_PACK, flat_native_list_pack);
        flat_native_plugin_register(FLAT_NATIVE_LIST_PUSH, flat_native_list_push);
        flat_native_plugin_register(FLAT_NATIVE_LIST_GET, flat_native_list_get);
        flat_native_plugin_register(FLAT_NATIVE_LIST_LEN, flat_native_list_len);
        flat_native_plugin_register(FLAT_NATIVE_LIST_PREPEND, flat_native_list_prepend);
        flat_native_plugin_register(FLAT_NATIVE_LIST_INSERT, flat_native_list_insert);
        flat_native_plugin_register(FLAT_NATIVE_LIST_SET, flat_native_list_set);
        flat_native_plugin_register(FLAT_NATIVE_LIST_INDEX_OF, flat_native_list_index_of);
        flat_native_plugin_register(FLAT_NATIVE_LIST_CONTAINS, flat_native_list_contains);
        flat_native_plugin_register(FLAT_NATIVE_LIST_REMOVE_AT, flat_native_list_remove_at);
        flat_native_plugin_register(FLAT_NATIVE_LIST_SLICE, flat_native_list_slice);
        flat_native_plugin_register(FLAT_NATIVE_LIST_SPLICE, flat_native_list_splice);
        flat_native_plugin_register(FLAT_NATIVE_LIST_RANGE, flat_native_list_range);
        flat_native_plugin_register(FLAT_NATIVE_LIST_REPEAT, flat_native_list_repeat);
        g_loaded_list = 1;
        return 0;
    }
#endif

    snprintf(err, err_size, "optional native plugin not found for \"%s\" (tried FLAT_NATIVE_PATH, "
                           "VM dir, bin/)",
             mod);
    return -1;

ok:
    if (strcmp(mod, FLAT_NATIVE_MODULE_MATH_TRIG) == 0) {
        g_loaded_math_trig = 1;
    }
    if (strcmp(mod, FLAT_NATIVE_MODULE_MATH_STATS) == 0) {
        g_loaded_math_stats = 1;
    }
    if (strcmp(mod, FLAT_NATIVE_MODULE_MATH) == 0) {
        g_loaded_math = 1;
    }
    if (strcmp(mod, FLAT_NATIVE_MODULE_LIST) == 0) {
        g_loaded_list = 1;
    }
    return 0;
}

void flat_native_runtime_reset(void) {
    memset(g_handlers, 0, sizeof g_handlers);
    g_manifest_count = 0;
    g_loaded_math_trig = 0;
    g_loaded_math_stats = 0;
    g_loaded_math = 0;
    g_loaded_list = 0;
    g_search_dir[0] = '\0';
    register_embedded_mem();
}

void flat_native_runtime_set_manifest(const FlatOptionalNativeDecl *decls, int count) {
    int i;
    g_manifest_count = 0;
    if (!decls || count <= 0) {
        return;
    }
    if (count > FGIR_MAX_OPTIONAL_NATIVES) {
        count = FGIR_MAX_OPTIONAL_NATIVES;
    }
    for (i = 0; i < count; i++) {
        g_manifest[i] = decls[i];
    }
    g_manifest_count = count;
}

void flat_native_runtime_set_search_dir(const char *dir) {
    if (!dir) {
        g_search_dir[0] = '\0';
        return;
    }
    snprintf(g_search_dir, sizeof g_search_dir, "%s", dir);
}

void flat_native_runtime_set_search_dir_from_argv0(const char *argv0) {
    char path[512];
    char *slash;
    if (!argv0) {
        return;
    }
    snprintf(path, sizeof path, "%s", argv0);
    slash = strrchr(path, '/');
    if (!slash) {
        slash = strrchr(path, '\\');
    }
    if (slash) {
        *slash = '\0';
        flat_native_runtime_set_search_dir(path);
    }
}

void flat_native_plugin_register(uint16_t id, int (*handler)(FlatNativeCtx *)) {
    if (id >= FLAT_NATIVE_COUNT || !handler) {
        return;
    }
    g_handlers[id] = handler;
}

int flat_native_runtime_prepare(uint16_t native_id, char *err, size_t err_size) {
    char mod[FGIR_NATIVE_MODULE_LEN];

    if (!flat_native_is_optional_id(native_id)) {
        return 0;
    }
    if (g_handlers[native_id]) {
        return 0;
    }
    if (flat_native_module_for_id(native_id, mod, sizeof mod) != 0) {
        snprintf(err, err_size, "unknown native id %u", (unsigned)native_id);
        return -1;
    }
    if (!manifest_has_module(mod)) {
        snprintf(err, err_size, "native %s not declared in program manifest", mod);
        return -1;
    }
    return load_module_plugin(mod, err, err_size);
}

int flat_native_invoke_optional(uint16_t id, FlatNativeCtx *ctx) {
    char err[256];
    if (flat_native_runtime_prepare(id, err, sizeof err) != 0) {
        fprintf(stderr, "flvm: %s\n", err);
        return -1;
    }
    if (!g_handlers[id]) {
        fprintf(stderr, "flvm: optional native handler missing for id %u\n", (unsigned)id);
        return -1;
    }
    return g_handlers[id](ctx);
}
