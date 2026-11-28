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
    FlatEventSubscriptionId sub = 0;
    FlatTaskId handler_task_id = 1;
    FlatValue event_value, bound_value;
    FILE *capture = NULL;
    int saved = -1;
    uint64_t turns = 0;
    char output[256];
    int rc = 0;
    memset(&bc, 0, sizeof bc);
    memset(&event_value, 0, sizeof event_value);
    memset(&bound_value, 0, sizeof bound_value);
    if (argc != 2 || flat_fb_load_file(argv[1], &bc) != 0) return 2;
    runtime = flat_runtime_create(NULL);
    coll = flat_coll_create();
    pool = flat_pool_create();
    if (!runtime || !coll || !pool) { rc = 3; goto done; }
    if (flat_value_set_str_inline(&event_value, "message-1", 9) != 0 ||
        flat_value_set_str_inline(&bound_value, "user-42", 7) != 0 ||
        flat_runtime_event_subscribe_handler(runtime, 17, "socket", "receive", &bc,
                                             "#onEvent", &bound_value, 1, coll, pool,
                                             NULL, &sub) != 0 ||
        flat_runtime_event_publish(runtime, 17, "socket", "receive", &event_value,
                                   coll, pool, NULL) != 1 ||
        flat_runtime_event_pending(runtime, sub) != 1) { rc = 4; goto done; }
    capture = capture_begin(&saved);
    if (!capture) { rc = 5; goto done; }
    if (flat_runtime_schedule(runtime, 32, 0, &turns) != 0 || turns == 0 ||
        flat_runtime_event_pending(runtime, sub) != 0) rc = 6;
    if (capture_end(capture, saved, output, sizeof output) != 0) rc = 7;
    capture = NULL; saved = -1;
    if (!rc && strcmp(output, "message-1\nuser-42\n") != 0) {
        fprintf(stderr, "unexpected event handler output: %s", output);
        rc = 8;
    }
    if (!rc && flat_runtime_task_destroy(runtime, handler_task_id) != 0) rc = 9;
    if (!rc && flat_runtime_event_unsubscribe(runtime, sub) != 0) rc = 10;
    sub = 0;
done:
    if (capture) (void)capture_end(capture, saved, output, sizeof output);
    if (runtime) {
        if (sub) (void)flat_runtime_event_unsubscribe(runtime, sub);
        if (flat_runtime_destroy(runtime) != 0 && !rc) rc = 11;
    }
    if (pool) flat_pool_destroy(pool);
    if (coll) flat_coll_destroy(coll);
    flat_fb_free(&bc);
    if (rc) { fprintf(stderr, "runtime event auto dispatch failed: %d\n", rc); return rc; }
    puts("runtime-event-auto-dispatch-ok");
    return 0;
}
