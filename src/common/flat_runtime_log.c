#include "flat_runtime_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FLAT_RUNTIME_LOG_LINE_MAX 1024u
#define FLAT_RUNTIME_LOG_MAX_BUFFER (16u * 1024u * 1024u)

typedef struct {
    uint32_t categories;
    char *buffer;
    size_t buffer_size;
    size_t buffer_used;
    uint64_t sequence;
    uint64_t dropped;
} FlatRuntimeLogger;

static FlatRuntimeLogger g_logger;

static void append_text(char *dst, size_t cap, size_t *used, const char *src) {
    size_t i;
    if (!src || !dst || !used || cap == 0) return;
    for (i = 0; src[i] && *used + 1 < cap; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '\\' || c == '"') {
            if (*used + 2 >= cap) break;
            dst[(*used)++] = '\\';
            dst[(*used)++] = (char)c;
        } else if (c == '\n' || c == '\r' || c == '\t') {
            if (*used + 2 >= cap) break;
            dst[(*used)++] = '\\';
            dst[(*used)++] = c == '\n' ? 'n' : (c == '\r' ? 'r' : 't');
        } else if (c < 0x20) {
            continue;
        } else {
            dst[(*used)++] = (char)c;
        }
    }
}

static void append_string_field(char *line, size_t cap, size_t *used, const char *key,
                                const char *value) {
    int n;
    if (!value || !value[0] || *used + 8 >= cap) return;
    n = snprintf(line + *used, cap - *used, ", %s: \"", key);
    if (n < 0 || (size_t)n >= cap - *used) {
        *used = cap - 1;
        line[*used] = '\0';
        return;
    }
    *used += (size_t)n;
    append_text(line, cap, used, value);
    if (*used + 2 < cap) {
        line[(*used)++] = '\"';
        line[*used] = '\0';
    } else {
        *used = cap - 1;
        line[*used] = '\0';
    }
}

int flat_runtime_log_configure(const FlatRuntimeLogConfig *config) {
    char *buffer = NULL;
    size_t buffer_size = 0;
    if (config) {
        if (config->buffer_size > FLAT_RUNTIME_LOG_MAX_BUFFER) return -1;
        if (config->categories && config->buffer_size) {
            buffer = (char *)malloc(config->buffer_size);
            if (!buffer) return -1;
            buffer_size = config->buffer_size;
        }
    }
    free(g_logger.buffer);
    memset(&g_logger, 0, sizeof g_logger);
    if (config) {
        g_logger.categories = config->categories &
                              (FLAT_RUNTIME_LOG_TASKS | FLAT_RUNTIME_LOG_CHANNELS);
        g_logger.buffer = buffer;
        g_logger.buffer_size = buffer_size;
    }
    return 0;
}

int flat_runtime_log_enabled(uint32_t category) {
    return (g_logger.categories & category) != 0;
}

void flat_runtime_log_emit(const FlatRuntimeLogEvent *event) {
    char line[FLAT_RUNTIME_LOG_LINE_MAX];
    size_t used;
    int n;
    if (!event || !event->event || !flat_runtime_log_enabled(event->category)) return;

    n = snprintf(line, sizeof line, "{seq: %llu", (unsigned long long)(++g_logger.sequence));
    if (n < 0 || (size_t)n >= sizeof line) return;
    used = (size_t)n;
    if (event->task_id >= 0)
        used += (size_t)snprintf(line + used, sizeof line - used, ", taskId: %d", event->task_id);
    if (event->channel_id >= 0)
        used += (size_t)snprintf(line + used, sizeof line - used, ", chanId: %d", event->channel_id);
    append_string_field(line, sizeof line, &used, "event", event->event);
    append_string_field(line, sizeof line, &used, "block", event->block);
    append_string_field(line, sizeof line, &used, "target", event->target);
    append_string_field(line, sizeof line, &used, "var", event->name);
    append_string_field(line, sizeof line, &used, "type", event->value_type);
    append_string_field(line, sizeof line, &used, "reason", event->reason);
    append_string_field(line, sizeof line, &used, "action", event->action);
    if (used + 3 >= sizeof line) return;
    line[used++] = ' ';
    line[used++] = '}';
    line[used++] = '\n';
    line[used] = '\0';

    if (!g_logger.buffer) {
        (void)fwrite(line, 1, used, stdout);
        return;
    }
    if (used > g_logger.buffer_size - g_logger.buffer_used) {
        g_logger.dropped++;
        return;
    }
    memcpy(g_logger.buffer + g_logger.buffer_used, line, used);
    g_logger.buffer_used += used;
}

void flat_runtime_log_flush(void) {
    if (g_logger.buffer && g_logger.buffer_used) {
        (void)fwrite(g_logger.buffer, 1, g_logger.buffer_used, stdout);
        g_logger.buffer_used = 0;
    }
    if (g_logger.dropped) {
        printf("{log: dropped, count: %llu}\n", (unsigned long long)g_logger.dropped);
        g_logger.dropped = 0;
    }
    (void)fflush(stdout);
}

void flat_runtime_log_shutdown(void) {
    flat_runtime_log_flush();
    free(g_logger.buffer);
    memset(&g_logger, 0, sizeof g_logger);
}

uint64_t flat_runtime_log_dropped(void) { return g_logger.dropped; }
