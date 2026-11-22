#include "flat_number.h"
#include "flat_time.h"
#include "flat_date.h"
#include "flat_number.h"
#include "flat_native_mod.h"
#include "flat_clock_runtime.h"
#include "flat_native_util.h"
#include "flat_value.h"

#include <stdio.h>
#include <math.h>
#include <string.h>

static int io_ok(FlatNativeCtx *ctx) {
    if (ctx->io_status) {
        *ctx->io_status = FLAT_IO_OK;
    }
    return 0;
}

static int io_err(FlatNativeCtx *ctx) {
    if (ctx->io_status) {
        *ctx->io_status = FLAT_IO_ERR;
    }
    return -1;
}

static int split_two_args(char *inner, char **left, char **right) {
    int depth = 0;
    char quote = 0;
    char *p;
    for (p = inner; *p; p++) {
        if (!quote && (*p == '\'' || *p == '"')) {
            quote = *p;
        } else if (quote && *p == quote) {
            quote = 0;
        } else if (!quote && (*p == '[' || *p == '(')) {
            depth++;
        } else if (!quote && (*p == ']' || *p == ')')) {
            if (depth > 0) {
                depth--;
            }
        } else if (!quote && depth == 0 && *p == ',') {
            *p = '\0';
            *left = flat_native_trim(inner);
            *right = flat_native_trim(p + 1);
            return 0;
        }
    }
    return -1;
}

static int string_from_value(const FlatValue *v, FlatStringPool *pool, char *buf, size_t buf_sz) {
    if (v->tag == VAL_STR_INLINE) {
        snprintf(buf, buf_sz, "%s", v->u.ins.bytes);
        return 0;
    }
    if (v->tag == VAL_STR_REF) {
        size_t len = 0;
        const uint8_t *bp = flat_pool_read(pool, v->u.ref_id, &len);
        if (!bp || len >= buf_sz) {
            return -1;
        }
        memcpy(buf, bp, len);
        buf[len] = '\0';
        return 0;
    }
    return -1;
}

static int emit_string(FlatNativeCtx *ctx, const char *s) {
    size_t len = strlen(s);
    uint32_t id;
    if (!ctx->out) {
        return -1;
    }
    if (len == 0) {
        return flat_value_set_str_inline(ctx->out, "", 0);
    }
    if (len <= FLAT_INLINE_STR_MAX) {
        return flat_value_set_str_inline(ctx->out, s, len);
    }
    if (flat_pool_alloc_copy(ctx->pool, s, len, &id) != 0) {
        return -1;
    }
    return flat_value_set_str_ref(ctx->out, id);
}

static int string_from_inner(FlatNativeCtx *ctx, char *token, size_t token_size) {
    FlatValue src;
    char inner_buf[4096];
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner ? ctx->inner : "");
    if (flat_native_parse_expr(ctx, inner_buf, &src) != 0) {
        return -1;
    }
    return string_from_value(&src, ctx->pool, token, token_size);
}

int flat_native_date_format(FlatNativeCtx *ctx) {
    char inner_buf[4096];
    char *a;
    char *b = NULL;
    FlatValue src;
    int64_t sec;
    char out[512];
    FlatDateRunState *st = flat_date_global_state();

    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    io_ok(ctx);
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner ? ctx->inner : "");
    if (split_two_args(inner_buf, &a, &b) != 0) {
        a = flat_native_trim(inner_buf);
        b = NULL;
    }
    memset(&src, 0, sizeof src);
    if (flat_native_parse_rhs(ctx, a, &src) != 0) {
        return io_err(ctx);
    }
    if (flat_date_value_to_utc_sec(&src, ctx->pool, &sec, st) != 0) {
        flat_native_parse_rhs_release(ctx, &src);
        return io_err(ctx);
    }
    {
        const char *pattern = "YYYY-MM-DD";
        char fmtbuf[256];

        if (b) {
            FlatValue vf;
            if (flat_native_parse_expr(ctx, b, &vf) != 0 ||
                string_from_value(&vf, ctx->pool, fmtbuf, sizeof fmtbuf) != 0) {
                flat_native_parse_rhs_release(ctx, &src);
                return io_err(ctx);
            }
            pattern = fmtbuf;
        }
        if (flat_date_format(sec, pattern, out, sizeof out, st) != 0) {
            flat_native_parse_rhs_release(ctx, &src);
            return io_err(ctx);
        }
    }
    flat_native_parse_rhs_release(ctx, &src);
    return emit_string(ctx, out);
}

int flat_native_date_parse_auto(FlatNativeCtx *ctx) {
    char inner_buf[4096];
    FlatValue v;
    int64_t sec;
    FlatDateRunState *st = flat_date_global_state();

    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    io_ok(ctx);
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner ? ctx->inner : "");
    if (flat_native_parse_expr(ctx, flat_native_trim(inner_buf), &v) != 0) {
        return io_err(ctx);
    }
    if (v.tag == VAL_NUMBER) {
        if (flat_date_value_to_utc_sec(&v, ctx->pool, &sec, st) != 0) {
            return io_err(ctx);
        }
        return flat_value_set_number(ctx->out, (double)sec);
    }
    if (v.tag == VAL_STR_INLINE || v.tag == VAL_STR_REF) {
        char token[512];
        if (string_from_value(&v, ctx->pool, token, sizeof token) != 0) {
            return io_err(ctx);
        }
        if (flat_date_parse_auto(flat_native_trim(token), &sec, st) != 0) {
            return io_err(ctx);
        }
        return flat_value_set_number(ctx->out, (double)sec);
    }
    return io_err(ctx);
}

int flat_native_date_parse(FlatNativeCtx *ctx) {
    char inner_buf[4096];
    char *s;
    char *fmt;
    char stok[512];
    char ftok[256];
    int64_t sec;
    FlatDateRunState *st = flat_date_global_state();

    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    io_ok(ctx);
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner ? ctx->inner : "");
    if (split_two_args(inner_buf, &s, &fmt) != 0) {
        return io_err(ctx);
    }
    {
        FlatValue vs;
        memset(&vs, 0, sizeof vs);
        if (flat_native_parse_rhs(ctx, s, &vs) != 0 ||
            string_from_value(&vs, ctx->pool, stok, sizeof stok) != 0) {
            flat_native_parse_rhs_release(ctx, &vs);
            return io_err(ctx);
        }
        {
            FlatValue vf;
            if (flat_native_parse_expr(ctx, fmt, &vf) != 0 ||
                string_from_value(&vf, ctx->pool, ftok, sizeof ftok) != 0) {
                flat_native_parse_rhs_release(ctx, &vs);
                return io_err(ctx);
            }
        }
        flat_native_parse_rhs_release(ctx, &vs);
    }
    if (ftok[0] == '\0') {
        return io_err(ctx);
    }
    if (flat_date_parse_strict(stok, ftok, &sec, st) != 0) {
        return io_err(ctx);
    }
    return flat_value_set_number(ctx->out, (double)sec);
}

int flat_native_date_now(FlatNativeCtx *ctx) {
    FlatDateRunState *st = flat_date_global_state();
    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    io_ok(ctx);
    return flat_value_set_number(ctx->out, (double)flat_date_now_sec(st));
}

int flat_native_date_today(FlatNativeCtx *ctx) {
    char out[32];
    FlatDateRunState *st = flat_date_global_state();
    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    io_ok(ctx);
    if (flat_date_today_string(out, sizeof out, st) != 0) {
        return io_err(ctx);
    }
    return emit_string(ctx, out);
}

int flat_native_date_add(FlatNativeCtx *ctx) {
    char inner_buf[4096];
    char *base;
    char *mod;
    FlatValue vb;
    int64_t sec;
    char out[64];
    FlatDateRunState *st = flat_date_global_state();

    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    io_ok(ctx);
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner ? ctx->inner : "");
    if (split_two_args(inner_buf, &base, &mod) != 0) {
        return io_err(ctx);
    }
    memset(&vb, 0, sizeof vb);
    if (flat_native_parse_rhs(ctx, base, &vb) != 0) {
        return io_err(ctx);
    }
    if (flat_date_value_to_utc_sec(&vb, ctx->pool, &sec, st) != 0) {
        flat_native_parse_rhs_release(ctx, &vb);
        return io_err(ctx);
    }
    {
        FlatValue vm;
        char modtok[128];
        if (flat_native_parse_expr(ctx, mod, &vm) != 0 ||
            string_from_value(&vm, ctx->pool, modtok, sizeof modtok) != 0) {
            flat_native_parse_rhs_release(ctx, &vb);
            return io_err(ctx);
        }
        if (flat_date_add_modifier(sec, modtok, &sec) != 0) {
            flat_native_parse_rhs_release(ctx, &vb);
            return io_err(ctx);
        }
    }
    flat_native_parse_rhs_release(ctx, &vb);
    if (vb.tag == VAL_NUMBER) {
        return flat_value_set_number(ctx->out, (double)sec);
    }
    if (flat_date_iso_z_from_sec(sec, out, sizeof out) != 0) {
        return io_err(ctx);
    }
    return emit_string(ctx, out);
}

int flat_native_date_cmp(FlatNativeCtx *ctx) {
    char inner_buf[4096];
    char *a;
    char *b;
    FlatValue va;
    FlatValue vb;
    int64_t sa;
    int64_t sb;
    FlatDateRunState *st = flat_date_global_state();

    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    io_ok(ctx);
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner ? ctx->inner : "");
    if (split_two_args(inner_buf, &a, &b) != 0) {
        return io_err(ctx);
    }
    memset(&va, 0, sizeof va);
    memset(&vb, 0, sizeof vb);
    if (flat_native_parse_rhs(ctx, a, &va) != 0) {
        return io_err(ctx);
    }
    if (flat_native_parse_rhs(ctx, b, &vb) != 0) {
        flat_native_parse_rhs_release(ctx, &va);
        return io_err(ctx);
    }
    if (flat_date_value_to_utc_sec(&va, ctx->pool, &sa, st) != 0 ||
        flat_date_value_to_utc_sec(&vb, ctx->pool, &sb, st) != 0) {
        flat_native_parse_rhs_release(ctx, &va);
        flat_native_parse_rhs_release(ctx, &vb);
        return io_err(ctx);
    }
    flat_native_parse_rhs_release(ctx, &va);
    flat_native_parse_rhs_release(ctx, &vb);
    if (sa < sb) {
        return flat_value_set_number(ctx->out, -1.0);
    }
    if (sa > sb) {
        return flat_value_set_number(ctx->out, 1.0);
    }
    return flat_value_set_number(ctx->out, 0.0);
}

int flat_native_date_to_seconds(FlatNativeCtx *ctx) {
    FlatValue v;
    char inner_buf[4096];
    int64_t sec;
    FlatDateRunState *st = flat_date_global_state();

    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    io_ok(ctx);
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner ? ctx->inner : "");
    if (flat_native_parse_expr(ctx, flat_native_trim(inner_buf), &v) != 0) {
        return io_err(ctx);
    }
    if (flat_date_value_to_utc_sec(&v, ctx->pool, &sec, st) != 0) {
        return io_err(ctx);
    }
    return flat_value_set_number(ctx->out, (double)sec);
}

int flat_native_date_from_seconds(FlatNativeCtx *ctx) {
    FlatValue v;
    char inner_buf[4096];
    char out[64];
    int64_t sec;

    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    io_ok(ctx);
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner ? ctx->inner : "");
    if (flat_native_parse_expr(ctx, flat_native_trim(inner_buf), &v) != 0 || v.tag != VAL_NUMBER) {
        return io_err(ctx);
    }
    sec = (int64_t)flat_number_as_f64(&v);
    if (flat_date_iso_z_from_sec(sec, out, sizeof out) != 0) {
        return io_err(ctx);
    }
    return emit_string(ctx, out);
}

int flat_native_date_to_ms(FlatNativeCtx *ctx) {
    if (flat_native_date_to_seconds(ctx) != 0) {
        return -1;
    }
    if (ctx->out->tag == VAL_NUMBER) {
        double d = flat_number_as_f64(ctx->out) * 1000.0;
        flat_value_set_number(ctx->out, d);
    }
    return 0;
}

int flat_native_date_from_ms(FlatNativeCtx *ctx) {
    FlatValue v;
    char inner_buf[4096];
    char out[64];
    int64_t sec;
    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    io_ok(ctx);
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner ? ctx->inner : "");
    if (flat_native_parse_expr(ctx, flat_native_trim(inner_buf), &v) != 0 || v.tag != VAL_NUMBER) {
        return io_err(ctx);
    }
    sec = (int64_t)(flat_number_as_f64(&v) / 1000.0);
    if (flat_date_iso_z_from_sec(sec, out, sizeof out) != 0) {
        return io_err(ctx);
    }
    return emit_string(ctx, out);
}

int flat_native_date_time_to_sec(FlatNativeCtx *ctx) {
    char token[64];
    int sec;
    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    io_ok(ctx);
    if (string_from_inner(ctx, token, sizeof token) != 0) {
        return io_err(ctx);
    }
    if (flat_date_time_to_sec(flat_native_trim(token), &sec) != 0) {
        return io_err(ctx);
    }
    return flat_value_set_number(ctx->out, (double)sec);
}

int flat_native_date_sec_to_time(FlatNativeCtx *ctx) {
    FlatValue v;
    char inner_buf[4096];
    char out[16];
    int sec;

    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    io_ok(ctx);
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner ? ctx->inner : "");
    if (flat_native_parse_expr(ctx, flat_native_trim(inner_buf), &v) != 0 || v.tag != VAL_NUMBER) {
        return io_err(ctx);
    }
    sec = (int)flat_number_as_f64(&v);
    if (flat_date_sec_to_time(sec, out, sizeof out) != 0) {
        return io_err(ctx);
    }
    return emit_string(ctx, out);
}

int flat_native_date_run_offset(FlatNativeCtx *ctx) {
    char token[64];
    FlatDateRunState *st = flat_date_global_state();
    if (!ctx->io_status) {
        return -1;
    }
    io_ok(ctx);
    if (string_from_inner(ctx, token, sizeof token) != 0) {
        return io_err(ctx);
    }
    flat_native_trim(token);
    if (token[0] == '\0' || strcmp(token, "Z") == 0 || strcmp(token, "+00:00") == 0) {
        st->run_offset_min = 0;
        st->has_run_offset = 0;
        return 0;
    }
    if (flat_date_parse_offset_hhmm(token, &st->run_offset_min) != 0) {
        return io_err(ctx);
    }
    st->has_run_offset = 1;
    return 0;
}

int flat_native_str_to_date(FlatNativeCtx *ctx) {
    char token[512];
    char out[32];
    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    io_ok(ctx);
    if (string_from_inner(ctx, token, sizeof token) != 0) {
        return io_err(ctx);
    }
    if (flat_date_str_to_date(flat_native_trim(token), out, sizeof out) != 0) {
        return io_err(ctx);
    }
    return emit_string(ctx, out);
}

int flat_native_str_to_time(FlatNativeCtx *ctx) {
    char token[512];
    char out[32];
    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    io_ok(ctx);
    if (string_from_inner(ctx, token, sizeof token) != 0) {
        return io_err(ctx);
    }
    if (flat_date_str_to_time(flat_native_trim(token), out, sizeof out) != 0) {
        return io_err(ctx);
    }
    return emit_string(ctx, out);
}

int flat_native_str_to_datetime(FlatNativeCtx *ctx) {
    char token[512];
    char out[64];
    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    io_ok(ctx);
    if (string_from_inner(ctx, token, sizeof token) != 0) {
        return io_err(ctx);
    }
    if (flat_date_str_to_datetime(flat_native_trim(token), out, sizeof out) != 0) {
        return io_err(ctx);
    }
    return emit_string(ctx, out);
}


/* Monotonic performance clock. Values are elapsed units from an unspecified
 * origin; callers should compare differences, never interpret them as dates. */
int flat_native_clock_now(FlatNativeCtx *ctx) {
    if (!ctx || !ctx->out || !ctx->io_status) {
        return -1;
    }
    io_ok(ctx);
    return flat_value_set_number(ctx->out, flat_time_monotonic_ms());
}

int flat_native_clock_now_us(FlatNativeCtx *ctx) {
    if (!ctx || !ctx->out || !ctx->io_status) {
        return -1;
    }
    io_ok(ctx);
    return flat_value_set_number(ctx->out, flat_time_monotonic_ms() * 1000.0);
}

/* Stopwatch resources share the VM's monotonic clock and are owned by one VM run. */
int flat_clock_stopwatch_live(const FlatClockRuntime *rt, uint32_t id) {
    return rt && id >= 1u && id <= FLAT_CLOCK_MAX_STOPWATCHES && rt->sw[id].live;
}

static int clock_sw_fail(FlatNativeCtx *ctx, const char *why) {
    if (ctx && ctx->io_status) *ctx->io_status = FLAT_IO_ERR;
    if (ctx && why) snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", why);
    return -1;
}

static int clock_sw_out_num(FlatNativeCtx *ctx, double n) {
    if (!ctx || !ctx->out || !ctx->io_status) return -1;
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_number(ctx->out, n);
}

static int clock_sw_out_bool(FlatNativeCtx *ctx, int b) {
    if (!ctx || !ctx->out || !ctx->io_status) return -1;
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_bool(ctx->out, b);
}

static int clock_sw_parse_num(FlatNativeCtx *ctx, const char *expr, double *out) {
    FlatValue v;
    if (!ctx || !expr || !out || flat_native_parse_expr(ctx, expr, &v) != 0 || v.tag != VAL_NUMBER) return -1;
    *out = flat_number_as_f64(&v);
    return 0;
}

static int clock_sw_args(FlatNativeCtx *ctx, double *a, double *b, int need_two) {
    char buf[512], *comma;
    double x, y = 0;
    if (!ctx || !ctx->inner) return -1;
    snprintf(buf, sizeof buf, "%s", ctx->inner);
    comma = strchr(buf, ',');
    if (need_two) {
        if (!comma) return -1;
        *comma++ = '\0';
        if (strchr(comma, ',')) return -1;
    } else if (comma) return -1;
    if (clock_sw_parse_num(ctx, flat_native_trim(buf), &x) != 0) return -1;
    if (need_two && clock_sw_parse_num(ctx, flat_native_trim(comma), &y) != 0) return -1;
    *a = x; if (b) *b = y;
    return 0;
}

static int clock_sw_resolve(FlatNativeCtx *ctx, uint32_t *id) {
    double n;
    if (clock_sw_args(ctx, &n, NULL, 0) != 0 || n < 1.0 || n > (double)FLAT_CLOCK_MAX_STOPWATCHES || floor(n) != n) return -1;
    *id = (uint32_t)n;
    return flat_clock_stopwatch_live(ctx->clock, *id) ? 0 : -1;
}

static double clock_sw_elapsed_at(const FlatClockStopwatch *sw, double now) {
    return sw->accumulated_ms + (sw->running ? now - sw->started_ms : 0.0);
}

int flat_native_clock_create(FlatNativeCtx *ctx) {
    uint32_t i;
    if (!ctx || !ctx->clock) return clock_sw_fail(ctx, "clock.create");
    for (i = 1; i <= FLAT_CLOCK_MAX_STOPWATCHES; ++i) {
        if (!ctx->clock->sw[i].live) {
            memset(&ctx->clock->sw[i], 0, sizeof ctx->clock->sw[i]);
            ctx->clock->sw[i].live = 1;
            return clock_sw_out_num(ctx, (double)i);
        }
    }
    return clock_sw_fail(ctx, "clock.create.limit");
}

int flat_native_clock_start(FlatNativeCtx *ctx) {
    uint32_t id; FlatClockStopwatch *sw;
    if (clock_sw_resolve(ctx, &id) != 0) return clock_sw_fail(ctx, "clock.start.id");
    sw = &ctx->clock->sw[id];
    if (!sw->running) { sw->started_ms = flat_time_monotonic_ms(); sw->running = 1; }
    *ctx->io_status = FLAT_IO_OK; return 0;
}

int flat_native_clock_stop(FlatNativeCtx *ctx) {
    uint32_t id; FlatClockStopwatch *sw; double now;
    if (clock_sw_resolve(ctx, &id) != 0) return clock_sw_fail(ctx, "clock.stop.id");
    sw = &ctx->clock->sw[id];
    if (sw->running) { now = flat_time_monotonic_ms(); sw->accumulated_ms += now - sw->started_ms; sw->running = 0; }
    *ctx->io_status = FLAT_IO_OK; return 0;
}

int flat_native_clock_reset(FlatNativeCtx *ctx) {
    uint32_t id;
    if (clock_sw_resolve(ctx, &id) != 0) return clock_sw_fail(ctx, "clock.reset.id");
    memset(&ctx->clock->sw[id], 0, sizeof ctx->clock->sw[id]); ctx->clock->sw[id].live = 1;
    *ctx->io_status = FLAT_IO_OK; return 0;
}

int flat_native_clock_elapsed(FlatNativeCtx *ctx) {
    uint32_t id;
    if (clock_sw_resolve(ctx, &id) != 0) return clock_sw_fail(ctx, "clock.elapsed.id");
    return clock_sw_out_num(ctx, clock_sw_elapsed_at(&ctx->clock->sw[id], flat_time_monotonic_ms()));
}

int flat_native_clock_running(FlatNativeCtx *ctx) {
    uint32_t id;
    if (clock_sw_resolve(ctx, &id) != 0) return clock_sw_fail(ctx, "clock.running.id");
    return clock_sw_out_bool(ctx, ctx->clock->sw[id].running);
}

int flat_native_clock_lap(FlatNativeCtx *ctx) {
    uint32_t id; FlatClockStopwatch *sw; double total, lap;
    if (clock_sw_resolve(ctx, &id) != 0) return clock_sw_fail(ctx, "clock.lap.id");
    sw = &ctx->clock->sw[id];
    if (!sw->running) return clock_sw_fail(ctx, "clock.lap.stopped");
    if (sw->lap_count >= FLAT_CLOCK_MAX_LAPS) return clock_sw_fail(ctx, "clock.lap.limit");
    total = clock_sw_elapsed_at(sw, flat_time_monotonic_ms());
    lap = total - sw->last_lap_ms;
    sw->last_lap_ms = total;
    sw->laps[sw->lap_count++] = total;
    return clock_sw_out_num(ctx, lap);
}

int flat_native_clock_lap_count(FlatNativeCtx *ctx) {
    uint32_t id;
    if (clock_sw_resolve(ctx, &id) != 0) return clock_sw_fail(ctx, "clock.lapCount.id");
    return clock_sw_out_num(ctx, (double)ctx->clock->sw[id].lap_count);
}

int flat_native_clock_lap_last(FlatNativeCtx *ctx) {
    uint32_t id; FlatClockStopwatch *sw; double total;
    if (clock_sw_resolve(ctx, &id) != 0) return clock_sw_fail(ctx, "clock.lapLast.id");
    sw = &ctx->clock->sw[id];
    if (!sw->lap_count) return clock_sw_fail(ctx, "clock.lapLast.empty");
    total = sw->laps[sw->lap_count - 1u];
    return clock_sw_out_num(ctx, total - (sw->lap_count > 1u ? sw->laps[sw->lap_count - 2u] : 0.0));
}

int flat_native_clock_lap_get(FlatNativeCtx *ctx) {
    double did, dindex; uint32_t id, index;
    if (clock_sw_args(ctx, &did, &dindex, 1) != 0 || did < 1.0 || did > FLAT_CLOCK_MAX_STOPWATCHES || floor(did) != did || dindex < 0.0 || dindex >= FLAT_CLOCK_MAX_LAPS || floor(dindex) != dindex) return clock_sw_fail(ctx, "clock.lapGet.arg");
    id = (uint32_t)did; index = (uint32_t)dindex;
    if (!flat_clock_stopwatch_live(ctx->clock, id)) return clock_sw_fail(ctx, "clock.lapGet.id");
    if (index >= ctx->clock->sw[id].lap_count) return clock_sw_fail(ctx, "clock.lapGet.index");
    return clock_sw_out_num(ctx, ctx->clock->sw[id].laps[index]);
}

int flat_native_clock_lap_clear(FlatNativeCtx *ctx) {
    uint32_t id; FlatClockStopwatch *sw;
    if (clock_sw_resolve(ctx, &id) != 0) return clock_sw_fail(ctx, "clock.lapClear.id");
    sw = &ctx->clock->sw[id]; sw->lap_count = 0; sw->last_lap_ms = clock_sw_elapsed_at(sw, flat_time_monotonic_ms());
    *ctx->io_status = FLAT_IO_OK; return 0;
}

int flat_native_clock_close(FlatNativeCtx *ctx) {
    uint32_t id;
    if (clock_sw_resolve(ctx, &id) != 0) return clock_sw_fail(ctx, "clock.close.id");
    memset(&ctx->clock->sw[id], 0, sizeof ctx->clock->sw[id]);
    *ctx->io_status = FLAT_IO_OK; return 0;
}
