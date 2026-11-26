#include "flat_native.h"
#include "flat_native_mod.h"
#include "flat_native_util.h"
#include "flat_number.h"

#include <stdio.h>
#include <string.h>

static int channel_fail(FlatNativeCtx *ctx, const char *reason) {
    if (ctx && ctx->io_status) *ctx->io_status = FLAT_IO_ERR;
    if (ctx && reason) snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", reason);
    return -1;
}

static int channel_split_args(char *buf, char *parts[], int max_parts, int *out_count) {
    char *p;
    char *start;
    char quote = '\0';
    int escaped = 0;
    int depth = 0;
    int count = 0;
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
        if (*p == '\'' || *p == '"') quote = *p;
        else if (*p == '(' || *p == '[' || *p == '{') depth++;
        else if (*p == ')' || *p == ']' || *p == '}') {
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

static int channel_parse_id(FlatNativeCtx *ctx, const char *expr, uint64_t *out_id) {
    FlatValue value;
    double number;
    if (!ctx || !expr || !out_id || flat_native_parse_expr(ctx, expr, &value) != 0 ||
        value.tag != VAL_NUMBER) return -1;
    number = flat_number_as_f64(&value);
    if (!(number >= 1.0) || number > 9007199254740991.0 ||
        number != (double)(uint64_t)number) return -1;
    *out_id = (uint64_t)number;
    return 0;
}

static int channel_parse_name(FlatNativeCtx *ctx, const char *expr, char *name, size_t name_size) {
    FlatValue value;
    if (!ctx || !expr || !name || name_size == 0 ||
        flat_native_parse_expr(ctx, expr, &value) != 0 ||
        flat_value_get_cstr(&value, ctx->pool, name, name_size) != 0 || !name[0]) return -1;
    return 0;
}

static int channel_require_bridge(FlatNativeCtx *ctx) {
    return ctx && ctx->task_bridge && ctx->task_bridge->task_id != 0;
}

int flat_native_channel_create(FlatNativeCtx *ctx) {
    uint64_t id = 0;
    if (!ctx || !ctx->out || !ctx->io_status || !ctx->inner || ctx->inner[0] ||
        !channel_require_bridge(ctx) || !ctx->task_bridge->channel_create) {
        return channel_fail(ctx, "channel.create.context");
    }
    if (ctx->task_bridge->channel_create(ctx->task_bridge->user,
                                         ctx->task_bridge->task_id, &id) != 0 || id == 0) {
        return channel_fail(ctx, "channel.create.failed");
    }
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_number(ctx->out, (double)id);
}

int flat_native_channel_set(FlatNativeCtx *ctx) {
    char buf[FGIR_MAX_LINE];
    char *parts[3];
    int count = 0;
    uint64_t id;
    char name[FLAT_VAR_NAME_MAX + 1];
    FlatValue value;
    int rc;
    if (!ctx || !ctx->inner || !ctx->out || !ctx->io_status || !ctx->pool || !ctx->coll ||
        !channel_require_bridge(ctx) || !ctx->task_bridge->channel_set) {
        return channel_fail(ctx, "channel.set.context");
    }
    snprintf(buf, sizeof buf, "%s", ctx->inner);
    if (channel_split_args(buf, parts, 3, &count) != 0 || count != 3 ||
        channel_parse_id(ctx, parts[0], &id) != 0 ||
        channel_parse_name(ctx, parts[1], name, sizeof name) != 0 ||
        flat_native_parse_rhs(ctx, parts[2], &value) != 0) {
        return channel_fail(ctx, "channel.set.args");
    }
    rc = ctx->task_bridge->channel_set(ctx->task_bridge->user, ctx->task_bridge->task_id,
                                       id, name, &value, ctx->coll, ctx->pool, ctx->file_tbl);
    flat_native_parse_rhs_release(ctx, &value);
    if (rc > 0) {
        ctx->task_bridge->retry_requested = 1;
        ctx->task_bridge->yield_requested = 1;
        *ctx->io_status = FLAT_IO_OK;
        return flat_value_set_bool(ctx->out, 1);
    }
    if (rc != 0) return channel_fail(ctx, rc == -2 ? "channel.set.busy" : "channel.set.failed");
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_bool(ctx->out, 1);
}

int flat_native_channel_var(FlatNativeCtx *ctx) {
    char buf[FGIR_MAX_LINE];
    char *parts[2];
    int count = 0;
    uint64_t id;
    char name[FLAT_VAR_NAME_MAX + 1];
    int rc;
    if (!ctx || !ctx->inner || !ctx->out || !ctx->io_status || !ctx->pool || !ctx->coll ||
        !channel_require_bridge(ctx) || !ctx->task_bridge->channel_var) {
        return channel_fail(ctx, "channel.var.context");
    }
    snprintf(buf, sizeof buf, "%s", ctx->inner);
    if (channel_split_args(buf, parts, 2, &count) != 0 || count != 2 ||
        channel_parse_id(ctx, parts[0], &id) != 0 ||
        channel_parse_name(ctx, parts[1], name, sizeof name) != 0) {
        return channel_fail(ctx, "channel.var.args");
    }
    rc = ctx->task_bridge->channel_var(ctx->task_bridge->user, ctx->task_bridge->task_id,
                                       id, name, ctx->coll, ctx->pool, ctx->file_tbl, ctx->out);
    if (rc > 0) {
        ctx->task_bridge->retry_requested = 1;
        ctx->task_bridge->yield_requested = 1;
        *ctx->io_status = FLAT_IO_OK;
        return 0;
    }
    if (rc != 0) return channel_fail(ctx, rc == -2 ? "channel.var.busy" : "channel.var.missing");
    *ctx->io_status = FLAT_IO_OK;
    return 0;
}

static int channel_tx_call(FlatNativeCtx *ctx, int (*callback)(void *, uint64_t, uint64_t),
                           const char *name) {
    uint64_t id;
    if (!ctx || !ctx->inner || !ctx->out || !ctx->io_status ||
        !channel_require_bridge(ctx) || !callback || channel_parse_id(ctx, ctx->inner, &id) != 0) {
        char reason[64];
        snprintf(reason, sizeof reason, "%s.args", name);
        return channel_fail(ctx, reason);
    }
    if (callback(ctx->task_bridge->user, ctx->task_bridge->task_id, id) != 0) {
        char reason[64];
        snprintf(reason, sizeof reason, "%s.failed", name);
        return channel_fail(ctx, reason);
    }
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_bool(ctx->out, 1);
}

int flat_native_channel_lock(FlatNativeCtx *ctx) {
    uint64_t id;
    int rc;
    if (!ctx || !ctx->inner || !ctx->out || !ctx->io_status ||
        !channel_require_bridge(ctx) || !ctx->task_bridge->channel_lock ||
        channel_parse_id(ctx, ctx->inner, &id) != 0) {
        return channel_fail(ctx, "channel.lock.args");
    }
    rc = ctx->task_bridge->channel_lock(ctx->task_bridge->user,
                                        ctx->task_bridge->task_id, id);
    if (rc < 0) return channel_fail(ctx, "channel.lock.failed");
    if (rc > 0) {
        ctx->task_bridge->retry_requested = 1;
        ctx->task_bridge->yield_requested = 1;
    }
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_bool(ctx->out, 1);
}
int flat_native_channel_unlock(FlatNativeCtx *ctx) {
    return channel_tx_call(ctx, ctx && ctx->task_bridge ? ctx->task_bridge->channel_unlock : NULL,
                           "channel.unlock");
}
int flat_native_channel_revert(FlatNativeCtx *ctx) {
    return channel_tx_call(ctx, ctx && ctx->task_bridge ? ctx->task_bridge->channel_revert : NULL,
                           "channel.revert");
}

int flat_native_channel_destroy(FlatNativeCtx *ctx) {
    return channel_tx_call(ctx, ctx && ctx->task_bridge ? ctx->task_bridge->channel_destroy : NULL,
                           "channel.destroy");
}
