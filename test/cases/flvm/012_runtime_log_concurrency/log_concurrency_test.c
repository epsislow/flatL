#define _POSIX_C_SOURCE 200809L
#include "flat_runtime_log.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define THREAD_COUNT 4
#define EVENTS_PER_THREAD 100

typedef struct {
    int task_id;
} WorkerArgs;

static void *emit_events(void *arg) {
    WorkerArgs *worker = (WorkerArgs *)arg;
    FlatRuntimeLogEvent event;
    int i;

    memset(&event, 0, sizeof event);
    event.category = FLAT_RUNTIME_LOG_TASKS;
    event.task_id = worker->task_id;
    event.channel_id = -1;
    event.event = "concurrent-event";
    event.block = "#worker";

    for (i = 0; i < EVENTS_PER_THREAD; ++i) {
        flat_runtime_log_emit(&event);
    }
    return NULL;
}

int main(void) {
    FlatRuntimeLogConfig config;
    pthread_t threads[THREAD_COUNT];
    WorkerArgs args[THREAD_COUNT];
    FILE *capture;
    int saved_stdout;
    char line[256];
    int line_count = 0;
    int saw_last_sequence = 0;
    int i;

    config.categories = FLAT_RUNTIME_LOG_TASKS;
    config.buffer_size = 512u * 1024u;
    if (flat_runtime_log_configure(&config) != 0) return 1;
    capture = tmpfile();
    if (!capture) return 2;
    saved_stdout = dup(STDOUT_FILENO);
    if (saved_stdout < 0 || dup2(fileno(capture), STDOUT_FILENO) < 0) return 3;

    for (i = 0; i < THREAD_COUNT; ++i) {
        args[i].task_id = i;
        if (pthread_create(&threads[i], NULL, emit_events, &args[i]) != 0) return 4;
    }
    for (i = 0; i < THREAD_COUNT; ++i) {
        if (pthread_join(threads[i], NULL) != 0) return 5;
    }

    if (flat_runtime_log_dropped() != 0u) return 6;
    flat_runtime_log_shutdown();
    fflush(stdout);
    if (dup2(saved_stdout, STDOUT_FILENO) < 0) return 7;
    close(saved_stdout);

    rewind(capture);
    while (fgets(line, sizeof line, capture)) {
        line_count++;
        if (strstr(line, "seq: 400")) saw_last_sequence = 1;
    }
    fclose(capture);
    if (line_count != THREAD_COUNT * EVENTS_PER_THREAD || !saw_last_sequence) return 8;
    puts("runtime-log-concurrency-ok");
    return 0;
}
