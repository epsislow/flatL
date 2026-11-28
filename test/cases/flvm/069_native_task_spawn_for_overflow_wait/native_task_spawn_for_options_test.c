#include "fb_format.h"
#include "flat_runtime.h"
#include "flat_runtime_event.h"
#include "flat_task.h"
#include "flat_value.h"

#include <stdio.h>
#include <stdlib.h>
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
    FlatTaskId root_id = 0;
    FlatValue first, second, third;
    FILE *capture = NULL;
    int saved = -1;
    uint64_t turns = 0;
    char output[256];
    int rc = 0;
    memset(&bc, 0, sizeof bc);
    memset(&first, 0, sizeof first);
    memset(&second, 0, sizeof second);
    memset(&third, 0, sizeof third);
    if (argc != 2 || flat_fb_load_file(argv[1], &bc) != 0) return 2;
    runtime = flat_runtime_create(NULL);
    coll = flat_coll_create();
    pool = flat_pool_create();
    if (!runtime || !coll || !pool ||
        flat_runtime_task_create(runtime, &bc, NULL, &root_id) != 0 ||
        flat_runtime_task_start(runtime, root_id) != 0) { rc = 3; goto done; }
    capture = capture_begin(&saved);
    if (!capture) { rc = 4; goto done; }
    if (flat_runtime_schedule(runtime, 64, 0, &turns) != 0 || turns == 0 ||
        flat_runtime_task_state(runtime, root_id) != FLAT_TASK_COMPLETED) { rc = 5; goto done; }
    if (flat_runtime_task_destroy(runtime, root_id) != 0) { rc = 6; goto done; }
    root_id = 0;
    {
        int p1, p2, pending;
        if (flat_value_set_str_inline(&first, "first", 5) != 0 ||
            flat_value_set_str_inline(&second, "second", 6) != 0 ||
            flat_value_set_str_inline(&third, "third", 5) != 0) { rc = 7; goto done; }
        p1 = flat_runtime_event_publish(runtime, 17, "socket", "receive", &first, coll, pool, NULL);
        p2 = flat_runtime_event_publish(runtime, 17, "socket", "receive", &second, coll, pool, NULL);
        pending = flat_runtime_event_pending(runtime, 1);
        if (p1 != 1 || p2 != -2 || pending != 1) {
            fprintf(stderr, "overflow policy mismatch p1=%d p2=%d pending=%d\n", p1, p2, pending);
            rc = 7; goto done;
        }
    }
    if (flat_runtime_schedule(runtime, 64, 0, &turns) != 0 || turns == 0 ||
        flat_runtime_event_pending(runtime, 1) != 0 ||
        flat_runtime_event_publish(runtime, 17, "socket", "receive", &second, coll, pool, NULL) != 1 ||
        flat_runtime_schedule(runtime, 64, 0, &turns) != 0 || turns == 0) { rc = 8; goto done; }
    if (capture_end(capture, saved, output, sizeof output) != 0) { rc = 9; goto done; }
    capture = NULL; saved = -1;
    if (strncmp(output, "1\n", 2) != 0 ||
        !strstr(output, "first\nbound\n") || !strstr(output, "second\nbound\n") || strstr(output, "third\n")) {
        fprintf(stderr, "unexpected native spawnFor wait output: %s", output);
        rc = 10; goto done;
    }
    if (flat_runtime_event_unsubscribe(runtime, 1) != 0) { rc = 11; goto done; }
done:
    if (capture) (void)capture_end(capture, saved, output, sizeof output);
    if (runtime) {
        (void)flat_runtime_event_unsubscribe(runtime, 1);
        if (root_id) (void)flat_runtime_task_destroy(runtime, root_id);
        (void)flat_runtime_task_destroy(runtime, 2);
        (void)flat_runtime_task_destroy(runtime, 3);
        (void)flat_runtime_task_destroy(runtime, 4);
        if (flat_runtime_destroy(runtime) != 0 && !rc) rc = 12;
    }
    if (pool) flat_pool_destroy(pool);
    if (coll) flat_coll_destroy(coll);
    flat_fb_free(&bc);
    if (rc) { fprintf(stderr, "native task spawnFor wait failed: %d\n", rc); return rc; }
    puts("native-task-spawn-for-wait-ok");
    return 0;
}
