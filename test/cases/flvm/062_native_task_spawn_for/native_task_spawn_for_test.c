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
    FlatTaskId root_id = 0;
    FlatValue event_value;
    FILE *capture = NULL;
    int saved = -1;
    uint64_t turns = 0;
    char output[256];
    int rc = 0;
    memset(&bc, 0, sizeof bc);
    memset(&event_value, 0, sizeof event_value);
    if (argc != 2 || flat_fb_load_file(argv[1], &bc) != 0) return 2;
    runtime = flat_runtime_create(NULL);
    if (!runtime || flat_runtime_task_create(runtime, &bc, NULL, &root_id) != 0 ||
        flat_runtime_task_start(runtime, root_id) != 0) { rc = 3; goto done; }
    capture = capture_begin(&saved);
    if (!capture) { rc = 4; goto done; }
    if (flat_runtime_schedule(runtime, 64, 0, &turns) != 0 || turns == 0 ||
        flat_runtime_task_state(runtime, root_id) != FLAT_TASK_COMPLETED) rc = 5;
    /* The handler must own its bytecode snapshot: destroy the registering task
       before the first event is published. */
    if (!rc && flat_runtime_task_destroy(runtime, root_id) != 0) rc = 6;
    if (!rc && flat_value_set_str_inline(&event_value, "payload-1", 9) != 0) rc = 7;
    if (!rc && flat_runtime_event_publish(runtime, 17, "socket", "receive", &event_value,
                                          NULL, NULL, NULL) != -1) {
        /* Publish requires source pools, supplied below. */
        rc = 8;
    }
    if (!rc) {
        FlatCollectionPool *coll = flat_coll_create();
        FlatStringPool *pool = flat_pool_create();
        if (!coll || !pool || flat_runtime_event_publish(runtime, 17, "socket", "receive",
                                                           &event_value, coll, pool, NULL) != 1 ||
            flat_runtime_event_publish(runtime, 18, "socket", "receive", &event_value,
                                       coll, pool, NULL) != 0 ||
            flat_runtime_schedule(runtime, 64, 0, &turns) != 0 || turns == 0) rc = 9;
        if (pool) flat_pool_destroy(pool);
        if (coll) flat_coll_destroy(coll);
    }
    if (capture_end(capture, saved, output, sizeof output) != 0) rc = 10;
    capture = NULL; saved = -1;
    if (!rc && strcmp(output, "1\n2\npayload-1\nbound-arg\n") != 0) {
        fprintf(stderr, "unexpected native spawnFor output: %s", output);
        rc = 11;
    }
    /* Handler task is the second runtime task (the root was task 1). */
    if (!rc && flat_runtime_task_destroy(runtime, 2) != 0) rc = 12;
    /* Subscription ID 1 was created for source 17; ID 2 was stopped by FlatL. */
    if (!rc && flat_runtime_event_unsubscribe(runtime, 1) != 0) rc = 13;
done:
    if (capture) (void)capture_end(capture, saved, output, sizeof output);
    if (runtime) {
        /* Best-effort cleanup for failure paths. */
        (void)flat_runtime_event_unsubscribe(runtime, 1);
        (void)flat_runtime_event_unsubscribe(runtime, 2);
        if (root_id) (void)flat_runtime_task_destroy(runtime, root_id);
        (void)flat_runtime_task_destroy(runtime, 2);
        if (flat_runtime_destroy(runtime) != 0 && !rc) rc = 14;
    }
    flat_fb_free(&bc);
    if (rc) { fprintf(stderr, "native task spawnFor failed: %d\n", rc); return rc; }
    puts("native-task-spawn-for-ok");
    return 0;
}
