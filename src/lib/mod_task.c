#include "flat_native.h"
#include "flat_native_util.h"
#include "flat_task_native.h"
#include "flat_number.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

static int task_fail(FlatNativeCtx *ctx, const char *reason) {
    if (ctx && ctx->io_status) *ctx->io_status = FLAT_IO_ERR;
    if (ctx && reason) snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", reason);
    return -1;
}

static int task_parse_id(FlatNativeCtx *ctx, const char *expr, uint64_t *out_id) {
    FlatValue value;
    double number;
    if (!ctx || !expr || !out_id || flat_native_parse_expr(ctx, expr, &value) != 0 ||
        value.tag != VAL_NUMBER) return -1;
    number = flat_number_as_f64(&value);
    if (!(number >= 1.0) || number > 9007199254740991.0 || number != (double)(uint64_t)number)
        return -1;
    *out_id = (uint64_t)number;
    return 0;
}

static int task_split_spawn_args(char *buf, char *parts[], int max_parts, int *out_count) {
    char *start;
    char *p;
    int depth = 0;
    int count = 0;
    char quote = '\0';
    int escaped = 0;
    if (!buf || !parts || !out_count || max_parts <= 0) return -1;
    start = flat_native_trim(buf);
    if (!*start) return -1;
    parts[count++] = start;
    for (p = start; *p; p++) {
        if (quote) {
            if (escaped) escaped = 0;
            else if (*p == '\\') escaped = 1;
            else if (*p == quote) quote = '\0';
            continue;
        }
        if (*p == '\'' || *p == '"') {
            quote = *p;
        } else if (*p == '(' || *p == '[' || *p == '{') {
            depth++;
        } else if (*p == ')' || *p == ']' || *p == '}') {
            if (--depth < 0) return -1;
        } else if (*p == ',' && depth == 0) {
            *p = '\0';
            if (count >= max_parts) return -1;
            start = flat_native_trim(p + 1);
            if (!*start) return -1;
            parts[count++] = start;
            p = start - 1;
        }
    }
    if (quote || depth != 0) return -1;
    *out_count = count;
    return 0;
}

int flat_native_task_spawn(FlatNativeCtx *ctx) {
    char buf[FGIR_MAX_LINE];
    char *parts[FGIR_MAX_PARAMS + 1];
    char label[FGIR_MAX_LABEL];
    FlatValue values[FGIR_MAX_PARAMS];
    uint64_t id = 0;
    int part_count = 0;
    int arg_count;
    int parsed = 0;
    int i;
    int rc;
    if (!ctx || !ctx->task_bridge || !ctx->task_bridge->spawn || !ctx->bc || !ctx->inner ||
        !ctx->out || !ctx->io_status || !ctx->pool || !ctx->coll) {
        return task_fail(ctx, "task.spawn.context");
    }
    snprintf(buf, sizeof buf, "%s", ctx->inner);
    if (task_split_spawn_args(buf, parts, FGIR_MAX_PARAMS + 1, &part_count) != 0 ||
        part_count < 1) return task_fail(ctx, "task.spawn.args");
    if (strlen(parts[0]) >= sizeof label) return task_fail(ctx, "task.spawn.args");
    snprintf(label, sizeof label, "%s", parts[0]);
    if (label[0] != '#' || label[1] == '\0' || strpbrk(label, " \t\r\n"))
        return task_fail(ctx, "task.spawn.args");
    arg_count = part_count - 1;
    memset(values, 0, sizeof values);
    for (i = 0; i < arg_count; i++) {
        if (flat_native_parse_rhs(ctx, parts[i + 1], &values[i]) != 0) {
            int j;
            for (j = 0; j < parsed; j++) flat_native_parse_rhs_release(ctx, &values[j]);
            return task_fail(ctx, "task.spawn.value");
        }
        parsed++;
    }
    rc = ctx->task_bridge->spawn(ctx->task_bridge->user, ctx->task_bridge->task_id,
                                 ctx->bc, label, values, (size_t)arg_count, ctx->coll,
                                 ctx->pool, ctx->file_tbl, &id);
    for (i = 0; i < parsed; i++) flat_native_parse_rhs_release(ctx, &values[i]);
    if (rc == -2) return task_fail(ctx, "task.spawn.args");
    if (rc != 0 || id == 0) return task_fail(ctx, "task.spawn.failed");
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_number(ctx->out, (double)id);
}

int flat_native_task_wait(FlatNativeCtx *ctx) {
    uint64_t target_id;
    int rc;
    if (!ctx || !ctx->task_bridge || !ctx->task_bridge->wait || !ctx->inner || !ctx->out ||
        !ctx->io_status || task_parse_id(ctx, ctx->inner, &target_id) != 0) {
        return task_fail(ctx, "task.wait.id");
    }
    rc = ctx->task_bridge->wait(ctx->task_bridge->user, ctx->task_bridge->task_id, target_id);
    if (rc < 0) return task_fail(ctx, "task.wait.invalid");
    if (rc > 0) ctx->task_bridge->yield_requested = 1;
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_bool(ctx->out, 1);
}

int flat_native_task_state(FlatNativeCtx *ctx) {
    uint64_t task_id;
    int state;
    if (!ctx || !ctx->task_bridge || !ctx->task_bridge->state || !ctx->inner || !ctx->out ||
        !ctx->io_status || task_parse_id(ctx, ctx->inner, &task_id) != 0 ||
        ctx->task_bridge->state(ctx->task_bridge->user, task_id, &state) != 0) {
        return task_fail(ctx, "task.state.invalid");
    }
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_number(ctx->out, (double)state);
}

int flat_native_task_result(FlatNativeCtx *ctx) {
    uint64_t task_id;
    FlatVmResult result;
    FlatValue values[4];
    char keys[4][FLAT_OBJ_KEY_MAX + 1] = {
        "state", "exitCode", "termination", "diagnostic"
    };
    uint32_t object_id = 0;
    int state = 0;
    size_t diagnostic_len;
    if (!ctx || !ctx->task_bridge || !ctx->task_bridge->result || !ctx->inner ||
        !ctx->out || !ctx->io_status || !ctx->pool || !ctx->coll ||
        task_parse_id(ctx, ctx->inner, &task_id) != 0 ||
        ctx->task_bridge->result(ctx->task_bridge->user, task_id, &result, &state) != 0) {
        return task_fail(ctx, "task.result.unavailable");
    }
    memset(values, 0, sizeof values);
    flat_value_set_number(&values[0], (double)state);
    flat_value_set_number(&values[1], (double)result.exit_code);
    flat_value_set_number(&values[2], (double)result.termination);
    diagnostic_len = strlen(result.diagnostic);
    if (diagnostic_len > FLAT_INLINE_STR_MAX) diagnostic_len = FLAT_INLINE_STR_MAX;
    if (flat_value_set_str_inline(&values[3], result.diagnostic, diagnostic_len) != 0 ||
        flat_obj_build(ctx->coll, ctx->pool,
                       (const char (*)[FLAT_OBJ_KEY_MAX + 1])keys, values, 4,
                       &object_id) != 0 || flat_value_set_object(ctx->out, object_id) != 0) {
        return task_fail(ctx, "task.result.memory");
    }
    *ctx->io_status = FLAT_IO_OK;
    return 0;
}


static int task_event_option_number(FlatNativeCtx *ctx, uint32_t object_id,
                                    const char *key, size_t *out, int *present) {
    FlatValue value;
    double number;
    memset(&value, 0, sizeof value);
    *present = 0;
    if (flat_obj_get(ctx->coll, object_id, key, &value) != 0) return 0;
    *present = 1;
    if (value.tag != VAL_NUMBER) return -1;
    number = flat_number_as_f64(&value);
    if (!(number >= 1.0) || number > 9007199254740991.0 ||
        number != (double)(uint64_t)number || (uint64_t)number > (uint64_t)SIZE_MAX)
        return -1;
    *out = (size_t)number;
    return 0;
}

static int task_event_option_string(FlatNativeCtx *ctx, uint32_t object_id,
                                    const char *key, char *out, size_t out_size,
                                    int *present) {
    FlatValue value;
    memset(&value, 0, sizeof value);
    *present = 0;
    if (flat_obj_get(ctx->coll, object_id, key, &value) != 0) return 0;
    *present = 1;
    if (flat_value_get_cstr(&value, ctx->pool, out, out_size) != 0) return -1;
    return 0;
}

static int task_event_options_parse(FlatNativeCtx *ctx, const char *expr,
                                    FlatEventSubscriptionOptions *options,
                                    int *is_options) {
    FlatValue value;
    char mode[16] = "parallel";
    char overflow[24] = "error";
    int has_mode = 0, has_max = 0, has_capacity = 0, has_overflow = 0;
    int i, count;
    size_t max_active = SIZE_MAX, queue_capacity = FLAT_EVENT_DEFAULT_QUEUE_CAPACITY;
    static const char *const allowed[] = { "mode", "maxActive", "queueCapacity", "overflow" };
    memset(&value, 0, sizeof value);
    *is_options = 0;
    if (flat_native_parse_rhs(ctx, expr, &value) != 0) return -1;
    if (value.tag != VAL_OBJECT) {
        flat_native_parse_rhs_release(ctx, &value);
        return 0;
    }
    if (flat_obj_len(ctx->coll, value.u.coll_id, &count) != 0) {
        flat_native_parse_rhs_release(ctx, &value);
        return -1;
    }
    /* Treat the first post-handler OBJECT as options only when it contains
     * at least one recognized option key. This preserves ordinary object
     * values as bound arguments for existing spawnFor calls. */
    if (flat_obj_get(ctx->coll, value.u.coll_id, "mode", &(FlatValue){0}) == 0 ||
        flat_obj_get(ctx->coll, value.u.coll_id, "maxActive", &(FlatValue){0}) == 0 ||
        flat_obj_get(ctx->coll, value.u.coll_id, "queueCapacity", &(FlatValue){0}) == 0 ||
        flat_obj_get(ctx->coll, value.u.coll_id, "overflow", &(FlatValue){0}) == 0)
        *is_options = 1;
    if (!*is_options) {
        flat_native_parse_rhs_release(ctx, &value);
        return 0;
    }
    for (i = 0; i < count; ++i) {
        const char *key = NULL;
        FlatValue ignored;
        int j, known = 0;
        memset(&ignored, 0, sizeof ignored);
        if (flat_obj_entry_at(ctx->coll, value.u.coll_id, i, &key, &ignored) != 0 || !key) {
            flat_native_parse_rhs_release(ctx, &value);
            return -1;
        }
        for (j = 0; j < (int)(sizeof allowed / sizeof allowed[0]); ++j)
            if (strcmp(key, allowed[j]) == 0) known = 1;
        if (!known) {
            flat_native_parse_rhs_release(ctx, &value);
            return -1;
        }
    }
    if (task_event_option_string(ctx, value.u.coll_id, "mode", mode, sizeof mode, &has_mode) != 0 ||
        task_event_option_number(ctx, value.u.coll_id, "maxActive", &max_active, &has_max) != 0 ||
        task_event_option_number(ctx, value.u.coll_id, "queueCapacity", &queue_capacity, &has_capacity) != 0 ||
        task_event_option_string(ctx, value.u.coll_id, "overflow", overflow, sizeof overflow, &has_overflow) != 0) {
        flat_native_parse_rhs_release(ctx, &value);
        return -1;
    }
    flat_native_parse_rhs_release(ctx, &value);
    options->mode = FLAT_EVENT_DISPATCH_PARALLEL;
    if (strcmp(mode, "parallel") == 0) options->mode = FLAT_EVENT_DISPATCH_PARALLEL;
    else if (strcmp(mode, "serial") == 0) options->mode = FLAT_EVENT_DISPATCH_SERIAL;
    else return -1;
    options->max_active = max_active;
    options->queue_capacity = queue_capacity;
    if (strcmp(overflow, "wait") == 0) options->overflow = FLAT_EVENT_OVERFLOW_WAIT;
    else if (strcmp(overflow, "dropOldest") == 0) options->overflow = FLAT_EVENT_OVERFLOW_DROP_OLDEST;
    else if (strcmp(overflow, "dropNewest") == 0) options->overflow = FLAT_EVENT_OVERFLOW_DROP_NEWEST;
    else if (strcmp(overflow, "error") == 0) options->overflow = FLAT_EVENT_OVERFLOW_ERROR;
    else return -1;
    (void)has_mode; (void)has_max; (void)has_capacity; (void)has_overflow;
    return 0;
}

static int task_spawn_for_impl(FlatNativeCtx *ctx, int legacy_mode, int legacy_limit) {
    char buf[FGIR_MAX_LINE];
    char *parts[FGIR_MAX_PARAMS + 6];
    char label[FGIR_MAX_LABEL];
    char source_kind[32];
    char event_name[48];
    FlatValue values[FGIR_MAX_PARAMS];
    FlatValue kind_value, event_value;
    FlatEventSubscriptionOptions options;
    uint64_t source_id = 0, subscription_id = 0;
    int part_count = 0, arg_count, arg_start, parsed = 0, i, rc;
    memset(values, 0, sizeof values);
    memset(&kind_value, 0, sizeof kind_value);
    memset(&event_value, 0, sizeof event_value);
    options.mode = FLAT_EVENT_DISPATCH_PARALLEL;
    options.max_active = SIZE_MAX;
    options.queue_capacity = FLAT_EVENT_DEFAULT_QUEUE_CAPACITY;
    options.overflow = FLAT_EVENT_OVERFLOW_ERROR;
    if (!ctx || !ctx->task_bridge || !ctx->task_bridge->spawn_for || !ctx->bc ||
        !ctx->inner || !ctx->out || !ctx->io_status || !ctx->pool || !ctx->coll ||
        !ctx->vars || task_split_spawn_args((snprintf(buf, sizeof buf, "%s", ctx->inner), buf),
                                             parts, FGIR_MAX_PARAMS + 6, &part_count) != 0 ||
        part_count < 4 || task_parse_id(ctx, parts[0], &source_id) != 0)
        return task_fail(ctx, "task.spawnFor.args");
    if (flat_native_parse_rhs(ctx, parts[1], &kind_value) != 0 ||
        flat_value_get_cstr(&kind_value, ctx->pool, source_kind, sizeof source_kind) != 0 ||
        flat_native_parse_rhs(ctx, parts[2], &event_value) != 0 ||
        flat_value_get_cstr(&event_value, ctx->pool, event_name, sizeof event_name) != 0) {
        flat_native_parse_rhs_release(ctx, &kind_value);
        flat_native_parse_rhs_release(ctx, &event_value);
        return task_fail(ctx, "task.spawnFor.source");
    }
    flat_native_parse_rhs_release(ctx, &kind_value);
    flat_native_parse_rhs_release(ctx, &event_value);
    if (strlen(parts[3]) >= sizeof label) return task_fail(ctx, "task.spawnFor.handler");
    snprintf(label, sizeof label, "%s", parts[3]);
    if (label[0] != '#' || label[1] == '\0' || strpbrk(label, " \t\r\n"))
        return task_fail(ctx, "task.spawnFor.handler");
    arg_start = 4;
    if (!legacy_mode && part_count > 4) {
        int is_options = 0;
        if (task_event_options_parse(ctx, parts[4], &options, &is_options) != 0)
            return task_fail(ctx, "task.spawnFor.options");
        if (is_options) arg_start = 5;
    }
    if (legacy_mode) {
        FlatValue mode_value;
        char mode_name[16];
        memset(&mode_value, 0, sizeof mode_value);
        if (part_count < 5 || flat_native_parse_rhs(ctx, parts[4], &mode_value) != 0 ||
            flat_value_get_cstr(&mode_value, ctx->pool, mode_name, sizeof mode_name) != 0) {
            flat_native_parse_rhs_release(ctx, &mode_value);
            return task_fail(ctx, "task.spawnForMode.mode");
        }
        flat_native_parse_rhs_release(ctx, &mode_value);
        if (strcmp(mode_name, "parallel") == 0) options.mode = FLAT_EVENT_DISPATCH_PARALLEL;
        else if (strcmp(mode_name, "serial") == 0) options.mode = FLAT_EVENT_DISPATCH_SERIAL;
        else return task_fail(ctx, "task.spawnForMode.mode");
        arg_start = 5;
        if (legacy_limit) {
            FlatValue limit_value;
            double limit_number;
            memset(&limit_value, 0, sizeof limit_value);
            if (part_count < 6 || flat_native_parse_rhs(ctx, parts[5], &limit_value) != 0 ||
                limit_value.tag != VAL_NUMBER) {
                flat_native_parse_rhs_release(ctx, &limit_value);
                return task_fail(ctx, "task.spawnForLimit.maxActive");
            }
            limit_number = flat_number_as_f64(&limit_value);
            flat_native_parse_rhs_release(ctx, &limit_value);
            if (!(limit_number >= 1.0) || limit_number > 9007199254740991.0 ||
                limit_number != (double)(uint64_t)limit_number)
                return task_fail(ctx, "task.spawnForLimit.maxActive");
            options.max_active = (size_t)limit_number;
            arg_start = 6;
        }
    }
    arg_count = part_count - arg_start;
    if (arg_count >= FGIR_MAX_PARAMS) return task_fail(ctx, "task.spawnFor.args");
    for (i = 0; i < arg_count; ++i) {
        if (flat_native_parse_rhs(ctx, parts[i + arg_start], &values[i]) != 0) {
            int j;
            for (j = 0; j < parsed; ++j) flat_native_parse_rhs_release(ctx, &values[j]);
            return task_fail(ctx, "task.spawnFor.value");
        }
        parsed++;
    }
    rc = ctx->task_bridge->spawn_for(ctx->task_bridge->user, ctx->task_bridge->task_id,
                                     ctx->bc, source_id, source_kind, event_name, label,
                                     values, (size_t)arg_count, ctx->coll, ctx->pool,
                                     ctx->file_tbl, &options, &subscription_id);
    for (i = 0; i < parsed; ++i) flat_native_parse_rhs_release(ctx, &values[i]);
    if (rc == -2) return task_fail(ctx, "task.spawnFor.args");
    if (rc != 0 || subscription_id == 0) return task_fail(ctx, "task.spawnFor.failed");
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_number(ctx->out, (double)subscription_id);
}

int flat_native_task_spawn_for(FlatNativeCtx *ctx) {
    return task_spawn_for_impl(ctx, 0, 0);
}

int flat_native_task_spawn_for_mode(FlatNativeCtx *ctx) {
    return task_spawn_for_impl(ctx, 1, 0);
}

int flat_native_task_spawn_for_limit(FlatNativeCtx *ctx) {
    return task_spawn_for_impl(ctx, 1, 1);
}

int flat_native_task_stop_for(FlatNativeCtx *ctx) {
    uint64_t subscription_id;
    int rc;
    if (!ctx || !ctx->task_bridge || !ctx->task_bridge->stop_for || !ctx->inner ||
        !ctx->out || !ctx->io_status || task_parse_id(ctx, ctx->inner, &subscription_id) != 0)
        return task_fail(ctx, "task.stopFor.id");
    rc = ctx->task_bridge->stop_for(ctx->task_bridge->user, ctx->task_bridge->task_id,
                                    subscription_id);
    if (rc != 0) return task_fail(ctx, "task.stopFor.invalid");
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_bool(ctx->out, 1);
}
