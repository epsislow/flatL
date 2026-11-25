#include "flat_runtime_log.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    FlatRuntimeLogEvent event;
    FlatRuntimeLogConfig config;
    char large_reason[400];

    memset(&event, 0, sizeof event);
    event.task_id = 3;
    event.channel_id = -1;
    event.event = "started";
    event.block = "#worker1";
    event.category = FLAT_RUNTIME_LOG_TASKS;

    config.categories = 0;
    config.buffer_size = 0;
    if (flat_runtime_log_configure(&config) != 0) return 1;
    flat_runtime_log_emit(&event); /* Disabled category must produce nothing. */
    flat_runtime_log_shutdown();

    config.categories = FLAT_RUNTIME_LOG_TASKS;
    config.buffer_size = 0;
    if (flat_runtime_log_configure(&config) != 0) return 2;
    event.category = FLAT_RUNTIME_LOG_CHANNELS;
    event.event = "filtered-event";
    flat_runtime_log_emit(&event); /* Disabled category must be filtered. */
    event.category = FLAT_RUNTIME_LOG_TASKS;
    event.event = "started";
    flat_runtime_log_emit(&event); /* Live output. */
    flat_runtime_log_shutdown();

    config.categories = FLAT_RUNTIME_LOG_CHANNELS;
    config.buffer_size = 256;
    if (flat_runtime_log_configure(&config) != 0) return 3;
    memset(&event, 0, sizeof event);
    event.category = FLAT_RUNTIME_LOG_CHANNELS;
    event.task_id = 4;
    event.channel_id = 21;
    event.event = "read";
    event.name = "x";
    event.value_type = "list";
    flat_runtime_log_emit(&event); /* Buffered output. */

    memset(large_reason, 'r', sizeof large_reason - 1);
    large_reason[sizeof large_reason - 1] = '\0';
    event.reason = large_reason;
    flat_runtime_log_emit(&event); /* Must be dropped, not grow the buffer. */
    if (flat_runtime_log_dropped() != 1u) return 4;
    flat_runtime_log_shutdown();

    puts("runtime-log-test-ok");
    return 0;
}
