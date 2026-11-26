#include "flat_env.h"
#include "flat_path.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#define WRITES 4000
#define READS 1200

static atomic_int g_failed = ATOMIC_VAR_INIT(0);

static void *writer_main(void *unused) {
    int i;
    (void)unused;
    for (i = 0; i < WRITES; ++i) {
        const char *value = (i & 1) ? "first" : "second";
        if (flat_env_override_set("FLAT_STAGING_RACE", value) != 0) {
            atomic_store(&g_failed, 1);
            break;
        }
        if (flat_path_sep_set((i & 1) ? '/' : '\\') != 0) {
            atomic_store(&g_failed, 1);
            break;
        }
    }
    return NULL;
}

static void *reader_main(void *unused) {
    int i;
    (void)unused;
    for (i = 0; i < READS; ++i) {
        FlatEnvContext env = {0};
        FlatPathContext path;
        char value[32];
        int found = 0;
        char sep;
        if (flat_env_context_init(&env) != 0) {
            atomic_store(&g_failed, 1);
            break;
        }
        if (flat_env_context_lookup(&env, "FLAT_STAGING_RACE", value, sizeof value, &found) != 0 ||
            !found || (strcmp(value, "first") != 0 && strcmp(value, "second") != 0)) {
            atomic_store(&g_failed, 1);
        }
        flat_env_context_destroy(&env);

        flat_path_context_init(&path);
        sep = flat_path_context_sep(&path);
        if (sep != '/' && sep != '\\') atomic_store(&g_failed, 1);
    }
    return NULL;
}

int main(void) {
    pthread_t writer;
    pthread_t readers[4];
    int i;

    flat_env_reset();
    flat_env_set_use_process(0);
    if (flat_env_override_set("FLAT_STAGING_RACE", "first") != 0) return 1;
    flat_path_sep_reset();

    if (pthread_create(&writer, NULL, writer_main, NULL) != 0) return 2;
    for (i = 0; i < 4; ++i) {
        if (pthread_create(&readers[i], NULL, reader_main, NULL) != 0) return 3;
    }
    pthread_join(writer, NULL);
    for (i = 0; i < 4; ++i) pthread_join(readers[i], NULL);

    flat_env_reset();
    if (atomic_load(&g_failed)) return 4;
    puts("host-staging-concurrency-ok");
    return 0;
}
