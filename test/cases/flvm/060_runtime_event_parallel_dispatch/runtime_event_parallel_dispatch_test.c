#include "fb_format.h"
#include "flat_runtime.h"
#include "flat_runtime_event.h"
#include "flat_task.h"
#include "flat_value.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static FILE *capture_begin(int *saved) {
    FILE *f = tmpfile();
    if (!f) return NULL;
    fflush(stdout);
    *saved = dup(STDOUT_FILENO);
    if (*saved < 0 || dup2(fileno(f), STDOUT_FILENO) < 0) {
        if (*saved >= 0) close(*saved);
        fclose(f);
        return NULL;
    }
    return f;
}

static int capture_end(FILE *f, int saved, char *out, size_t cap) {
    long n;
    int rc = 0;
    fflush(stdout);
    if (dup2(saved, STDOUT_FILENO) < 0) rc = -1;
    close(saved);
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || (size_t)n >= cap ||
        fseek(f, 0, SEEK_SET) != 0 || fread(out, 1, (size_t)n, f) != (size_t)n) rc = -1;
    else out[n] = '\0';
    fclose(f);
    return rc;
}

int main(int argc, char **argv) {
    FlatBytecode bc;
    FlatRuntime *runtime = NULL;
    FlatCollectionPool *coll = NULL;
    FlatStringPool *pool = NULL;
    FlatEventSubscriptionId sub = 0;
    FlatValue event_a, event_b, bound;
    FlatTaskId task_a = 0, task_b = 0;
    FlatVmResult result;
    FILE *capture = NULL;
    int saved = -1;
    uint64_t turns = 0;
    char output[256];
    int rc = 0;
    memset(&bc, 0, sizeof bc);
    memset(&event_a, 0, sizeof event_a);
    memset(&event_b, 0, sizeof event_b);
    memset(&bound, 0, sizeof bound);
    if (argc != 2 || flat_fb_load_file(argv[1], &bc) != 0) return 2;
    runtime = flat_runtime_create(NULL);
    coll = flat_coll_create();
    pool = flat_pool_create();
    if (!runtime || !coll || !pool) { rc = 3; goto done; }
    if (flat_value_set_str_inline(&event_a, "first", 5) != 0 ||
        flat_value_set_str_inline(&event_b, "second", 6) != 0 ||
        flat_value_set_str_inline(&bound, "shared", 6) != 0 ||
        flat_runtime_event_subscribe(runtime, 23, "socket", "receive", &sub) != 0 ||
        flat_runtime_event_publish(runtime, 23, "socket", "receive", &event_a, coll, pool, NULL) != 1 ||
        flat_runtime_event_publish(runtime, 23, "socket", "receive", &event_b, coll, pool, NULL) != 1 ||
        flat_runtime_event_pending(runtime, sub) != 2) { rc = 4; goto done; }

    capture = capture_begin(&saved);
    if (!capture) { rc = 5; goto done; }
    if (flat_runtime_event_spawn_next(runtime, sub, &bc, "#onEvent", &bound, 1,
                                      coll, pool, NULL, &task_a) != 1 ||
        flat_runtime_event_spawn_next(runtime, sub, &bc, "#onEvent", &bound, 1,
                                      coll, pool, NULL, &task_b) != 1 ||
        task_a == 0 || task_b == 0 || task_a == task_b ||
        flat_runtime_event_pending(runtime, sub) != 0) { rc = 6; goto done; }

    if (flat_runtime_schedule(runtime, 32, 0, &turns) != 0 || turns == 0 ||
        flat_runtime_task_state(runtime, task_a) != FLAT_TASK_COMPLETED ||
        flat_runtime_task_state(runtime, task_b) != FLAT_TASK_COMPLETED ||
        flat_runtime_task_result(runtime, task_a, &result) != 0 || result.termination != FLAT_TERM_OK ||
        flat_runtime_task_result(runtime, task_b, &result) != 0 || result.termination != FLAT_TERM_OK) {
        rc = 7;
    }
    if (capture_end(capture, saved, output, sizeof output) != 0) rc = 8;
    capture = NULL;
    saved = -1;
    if (!rc && strcmp(output, "first\nshared\nsecond\nshared\n") != 0) rc = 9;
    if (!rc && (flat_runtime_task_destroy(runtime, task_a) != 0 ||
                flat_runtime_task_destroy(runtime, task_b) != 0)) rc = 10;
    if (!rc && flat_runtime_event_unsubscribe(runtime, sub) != 0) rc = 11;
done:
    if (capture) (void)capture_end(capture, saved, output, sizeof output);
    if (runtime) {
        if (sub) (void)flat_runtime_event_unsubscribe(runtime, sub);
        if (flat_runtime_destroy(runtime) != 0 && !rc) rc = 12;
    }
    if (pool) flat_pool_destroy(pool);
    if (coll) flat_coll_destroy(coll);
    flat_fb_free(&bc);
    if (rc) { fprintf(stderr, "runtime event parallel dispatch failed: %d\n", rc); return rc; }
    puts("runtime-event-parallel-dispatch-ok");
    return 0;
}
