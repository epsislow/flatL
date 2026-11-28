#include "flat_sockets_native.h"
#include "flat_task_native.h"
#include "flat_native_util.h"
#include "flat_number.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int sockets_fail(FlatNativeCtx *ctx, const char *why) {
    if (ctx && ctx->io_status) *ctx->io_status = FLAT_IO_ERR;
    if (ctx && why) snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", why);
    return -1;
}
static int sockets_ok(FlatNativeCtx *ctx) {
    if (ctx && ctx->io_status) *ctx->io_status = FLAT_IO_OK;
    return 0;
}
static int split_args(char *buf, char **parts, int max_parts, int *count) {
    char *start, *p, quote = 0;
    int depth = 0, n = 0, escaped = 0;
    if (!buf || !parts || !count || max_parts < 1) return -1;
    start = flat_native_trim(buf);
    if (!*start) return -1;
    parts[n++] = start;
    for (p = start; *p; ++p) {
        if (quote) {
            if (escaped) escaped = 0;
            else if (*p == '\\') escaped = 1;
            else if (*p == quote) quote = 0;
            continue;
        }
        if (*p == '\'' || *p == '"') quote = *p;
        else if (*p == '(' || *p == '[' || *p == '{') depth++;
        else if (*p == ')' || *p == ']' || *p == '}') { if (--depth < 0) return -1; }
        else if (*p == ',' && depth == 0) {
            *p = '\0';
            if (n >= max_parts) return -1;
            start = flat_native_trim(p + 1);
            if (!*start) return -1;
            parts[n++] = start;
            p = start - 1;
        }
    }
    if (quote || depth) return -1;
    *count = n;
    return 0;
}
static int parse_uint(FlatNativeCtx *ctx, const char *expr, uint64_t min_value,
                      uint64_t max_value, uint64_t *out) {
    FlatValue v;
    double n;
    memset(&v, 0, sizeof v);
    if (flat_native_parse_rhs(ctx, expr, &v) != 0 || v.tag != VAL_NUMBER) {
        flat_native_parse_rhs_release(ctx, &v);
        return -1;
    }
    n = flat_number_as_f64(&v);
    flat_native_parse_rhs_release(ctx, &v);
    if (!(n >= (double)min_value) || n > (double)max_value ||
        n != (double)(uint64_t)n) return -1;
    *out = (uint64_t)n;
    return 0;
}
static int parse_id(FlatNativeCtx *ctx, const char *expr, uint64_t *out) {
    return parse_uint(ctx, expr, 1, 9007199254740991ULL, out);
}
static int parse_payload(FlatNativeCtx *ctx, const char *expr,
                         const unsigned char **data, size_t *length,
                         FlatValue *value) {
    const uint8_t *p;
    memset(value, 0, sizeof *value);
    if (flat_native_parse_rhs(ctx, expr, value) != 0) return -1;
    if (value->tag == VAL_STR_INLINE) {
        p = (const uint8_t *)value->u.ins.bytes;
        *length = value->u.ins.len;
    } else if (value->tag == VAL_STR_REF) {
        p = flat_pool_read(ctx->pool, value->u.ref_id, length);
        if (!p) {
            flat_native_parse_rhs_release(ctx, value);
            return -1;
        }
    } else {
        flat_native_parse_rhs_release(ctx, value);
        return -1;
    }
    *data = p;
    return 0;
}
static int parse_string(FlatNativeCtx *ctx, const char *expr, char *out, size_t cap) {
    FlatValue v;
    int rc;
    memset(&v, 0, sizeof v);
    if (flat_native_parse_rhs(ctx, expr, &v) != 0) return -1;
    rc = flat_value_get_cstr(&v, ctx->pool, out, cap);
    flat_native_parse_rhs_release(ctx, &v);
    return rc;
}
int flat_native_sockets_ws_supported(FlatNativeCtx *ctx) {
    int supported;
    if (!ctx || !ctx->out || !ctx->io_status || !ctx->task_bridge ||
        !ctx->task_bridge->socket_supported) return sockets_fail(ctx, "sockets.runtime");
    supported = ctx->task_bridge->socket_supported(ctx->task_bridge->user);
    sockets_ok(ctx);
    return flat_value_set_bool(ctx->out, supported != 0);
}
int flat_native_sockets_ws_connect(FlatNativeCtx *ctx) {
    char buf[FGIR_MAX_LINE], *parts[3], url[2048];
    uint64_t id = 0, timeout_ms = 10000, max_frame_bytes = 1024u * 1024u;
    int count = 0;
    if (!ctx || !ctx->inner || !ctx->out || !ctx->task_bridge ||
        !ctx->task_bridge->socket_connect) return sockets_fail(ctx, "sockets.runtime");
    snprintf(buf, sizeof buf, "%s", ctx->inner);
    if (split_args(buf, parts, 3, &count) != 0 || count < 1 || count > 3 ||
        parse_string(ctx, parts[0], url, sizeof url) != 0 || !url[0] ||
        (count >= 2 && parse_uint(ctx, parts[1], 1, 86400000, &timeout_ms) != 0) ||
        (count >= 3 && parse_uint(ctx, parts[2], 1, 1073741824ULL, &max_frame_bytes) != 0))
        return sockets_fail(ctx, "sockets.wsConnect.args");
    if (ctx->task_bridge->socket_connect(ctx->task_bridge->user, url,
            (long)timeout_ms, (size_t)max_frame_bytes, &id) != 0 || !id)
        return sockets_fail(ctx, "sockets.wsConnect.failed");
    sockets_ok(ctx);
    return flat_value_set_number(ctx->out, (double)id);
}
int flat_native_sockets_ws_send(FlatNativeCtx *ctx) {
    char buf[FGIR_MAX_LINE], *parts[2], message[FGIR_MAX_LINE];
    uint64_t id;
    int count = 0;
    if (!ctx || !ctx->inner || !ctx->out || !ctx->task_bridge ||
        !ctx->task_bridge->socket_send) return sockets_fail(ctx, "sockets.runtime");
    snprintf(buf, sizeof buf, "%s", ctx->inner);
    if (split_args(buf, parts, 2, &count) != 0 || count != 2 ||
        parse_id(ctx, parts[0], &id) != 0 ||
        parse_string(ctx, parts[1], message, sizeof message) != 0)
        return sockets_fail(ctx, "sockets.wsSend.args");
    if (ctx->task_bridge->socket_send(ctx->task_bridge->user, id, 1,
            message, strlen(message)) != 0)
        return sockets_fail(ctx, "sockets.wsSend.failed");
    sockets_ok(ctx);
    return flat_value_set_bool(ctx->out, 1);
}
int flat_native_sockets_ws_send_binary(FlatNativeCtx *ctx) {
    char buf[FGIR_MAX_LINE], *parts[2];
    uint64_t id;
    const unsigned char *data = NULL;
    size_t length = 0;
    FlatValue payload;
    int count = 0, rc;
    if (!ctx || !ctx->inner || !ctx->out || !ctx->task_bridge ||
        !ctx->task_bridge->socket_send) return sockets_fail(ctx, "sockets.runtime");
    snprintf(buf, sizeof buf, "%s", ctx->inner);
    if (split_args(buf, parts, 2, &count) != 0 || count != 2 ||
        parse_id(ctx, parts[0], &id) != 0 ||
        parse_payload(ctx, parts[1], &data, &length, &payload) != 0)
        return sockets_fail(ctx, "sockets.wsSendBinary.args");
    rc = ctx->task_bridge->socket_send(ctx->task_bridge->user, id, 2, data, length);
    flat_native_parse_rhs_release(ctx, &payload);
    if (rc != 0) return sockets_fail(ctx, "sockets.wsSendBinary.failed");
    sockets_ok(ctx);
    return flat_value_set_bool(ctx->out, 1);
}
int flat_native_sockets_ws_close(FlatNativeCtx *ctx) {
    uint64_t id;
    if (!ctx || !ctx->inner || !ctx->task_bridge || !ctx->task_bridge->socket_close ||
        parse_id(ctx, ctx->inner, &id) != 0)
        return sockets_fail(ctx, "sockets.wsClose.args");
    if (ctx->task_bridge->socket_close(ctx->task_bridge->user, id) != 0)
        return sockets_fail(ctx, "sockets.wsClose.failed");
    sockets_ok(ctx);
    return flat_value_set_bool(ctx->out, 1);
}
