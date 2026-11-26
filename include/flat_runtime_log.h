#ifndef FLAT_RUNTIME_LOG_H
#define FLAT_RUNTIME_LOG_H

#include <stddef.h>
#include <stdint.h>

enum {
    FLAT_RUNTIME_LOG_TASKS = 1u << 0,
    FLAT_RUNTIME_LOG_CHANNELS = 1u << 1
};

typedef struct {
    uint32_t categories;
    size_t buffer_size; /* 0 = live output; otherwise bounded output buffer */
} FlatRuntimeLogConfig;

typedef struct {
    uint32_t category;
    int64_t task_id;    /* negative = omit */
    int64_t channel_id; /* negative = omit */
    const char *event;
    const char *block;
    const char *target;
    const char *name;
    const char *value_type;
    const char *reason;
    const char *action;
} FlatRuntimeLogEvent;

/*
 * Process-owned logger. Its internal state is synchronized; configure/shutdown may
 * serialize with emit/flush, but callers must keep event strings alive for the call.
 * Disabled categories do no formatting or allocation.
 */
int flat_runtime_log_configure(const FlatRuntimeLogConfig *config);
int flat_runtime_log_enabled(uint32_t category);
void flat_runtime_log_emit(const FlatRuntimeLogEvent *event);
void flat_runtime_log_flush(void);
void flat_runtime_log_shutdown(void);
uint64_t flat_runtime_log_dropped(void);

#endif
