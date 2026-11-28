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


static int task_spawn_for_impl(FlatNativeCtx *ctx, int with_mode, int with_limit) {
    char buf[FGIR_MAX_LINE];
    char *parts[FGIR_MAX_PARAMS + 6];
    char label[FGIR_MAX_LABEL];
    char source_kind[32];
    char event_name[48];
    char mode_name[16] = "parallel";
    FlatValue values[FGIR_MAX_PARAMS];
    FlatValue kind_value, event_value;
    uint64_t source_id = 0, subscription_id = 0;
    int part_count = 0, arg_count, arg_start, parsed = 0, i, rc;
    int dispatch_mode = 0;
    size_t max_active = SIZE_MAX;
    memset(values, 0, sizeof values);
    memset(&kind_value, 0, sizeof kind_value);
    memset(&event_value, 0, sizeof event_value);
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
    if (with_mode) {
        FlatValue mode_value;
        memset(&mode_value, 0, sizeof mode_value);
        if (part_count < 5 || flat_native_parse_rhs(ctx, parts[4], &mode_value) != 0 ||
            flat_value_get_cstr(&mode_value, ctx->pool, mode_name, sizeof mode_name) != 0) {
            flat_native_parse_rhs_release(ctx, &mode_value);
            return task_fail(ctx, "task.spawnForMode.mode");
        }
        flat_native_parse_rhs_release(ctx, &mode_value);
        if (strcmp(mode_name, "parallel") == 0) dispatch_mode = 0;
        else if (strcmp(mode_name, "serial") == 0) dispatch_mode = 1;
        else return task_fail(ctx, "task.spawnForMode.mode");
        arg_start = 5;
        if (with_limit) {
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
                limit_number != (double)(uint64_t)limit_number) {
                return task_fail(ctx, "task.spawnForLimit.maxActive");
            }
            max_active = (size_t)limit_number;
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
                                     ctx->file_tbl, dispatch_mode, max_active, &subscription_id);
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
