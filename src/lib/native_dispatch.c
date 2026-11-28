#include "flat_native.h"
#include "flat_native_mod.h"
#include "flat_native_plugin.h"

static int flat_native_invoke_inner(uint16_t id, FlatNativeCtx *ctx) {
    if (!ctx || id >= FLAT_NATIVE_COUNT) {
        return -1;
    }
    if (id == FLAT_NATIVE_TASK_SPAWN) return flat_native_task_spawn(ctx);
    if (id == FLAT_NATIVE_TASK_WAIT) return flat_native_task_wait(ctx);
    if (id == FLAT_NATIVE_TASK_STATE) return flat_native_task_state(ctx);
    if (id == FLAT_NATIVE_TASK_RESULT) return flat_native_task_result(ctx);
    if (id == FLAT_NATIVE_TASK_SPAWN_FOR) return flat_native_task_spawn_for(ctx);
    if (id == FLAT_NATIVE_TASK_SPAWN_FOR_MODE) return flat_native_task_spawn_for_mode(ctx);
    if (id == FLAT_NATIVE_TASK_STOP_FOR) return flat_native_task_stop_for(ctx);
    if (id == FLAT_NATIVE_CHANNEL_CREATE) return flat_native_channel_create(ctx);
    if (id == FLAT_NATIVE_CHANNEL_SET) return flat_native_channel_set(ctx);
    if (id == FLAT_NATIVE_CHANNEL_VAR) return flat_native_channel_var(ctx);
    if (id == FLAT_NATIVE_CHANNEL_LOCK) return flat_native_channel_lock(ctx);
    if (id == FLAT_NATIVE_CHANNEL_UNLOCK) return flat_native_channel_unlock(ctx);
    if (id == FLAT_NATIVE_CHANNEL_REVERT) return flat_native_channel_revert(ctx);
    if (id == FLAT_NATIVE_CHANNEL_DESTROY) return flat_native_channel_destroy(ctx);
    if (flat_native_is_optional_id(id)) {
        return flat_native_invoke_optional(id, ctx);
    }
    switch ((FlatNativeId)id) {
    case FLAT_NATIVE_IO_OUTLN:
        return flat_native_io_outln(ctx);
    case FLAT_NATIVE_IO_OUT:
        return flat_native_io_out(ctx);
    case FLAT_NATIVE_IO_OUTVAR:
        return flat_native_io_outvar(ctx);
    case FLAT_NATIVE_IO_IN:
        return flat_native_io_in(ctx);
    case FLAT_NATIVE_IO_INLN:
        return flat_native_io_inln(ctx);
    case FLAT_NATIVE_IO_IN_NUM:
        return flat_native_io_in_num(ctx);
    case FLAT_NATIVE_IO_IN_BOOL:
        return flat_native_io_in_bool(ctx);
    case FLAT_NATIVE_IO_IN_LIST:
        return flat_native_io_in_list(ctx);
    case FLAT_NATIVE_IO_IN_OBJECT:
        return flat_native_io_in_object(ctx);
    case FLAT_NATIVE_IO_IN_BOOLS:
        return flat_native_io_in_bools(ctx);
    case FLAT_NATIVE_IO_DOCS:
        return flat_native_io_docs(ctx);
    case FLAT_NATIVE_IO_CTXIN:
        return flat_native_io_ctxin(ctx);
    case FLAT_NATIVE_IO_CTXOUT:
        return flat_native_io_ctxout(ctx);
    case FLAT_NATIVE_IO_CTXERR:
        return flat_native_io_ctxerr(ctx);
    case FLAT_NATIVE_IO_ERR:
        return flat_native_io_err(ctx);
    case FLAT_NATIVE_IO_ERRLN:
        return flat_native_io_errln(ctx);
    case FLAT_NATIVE_IO_OUTCLS:
        return flat_native_io_outcls(ctx);
    case FLAT_NATIVE_STR_TO_NUM:
        return flat_native_str_to_num(ctx);
    case FLAT_NATIVE_STR_TO_BOOL:
        return flat_native_str_to_bool(ctx);
    case FLAT_NATIVE_STR_LEN:
        return flat_native_str_len(ctx);
    case FLAT_NATIVE_STR_REPEAT:
        return flat_native_str_repeat(ctx);
    case FLAT_NATIVE_STR_CONCAT:
        return flat_native_str_concat(ctx);
    case FLAT_NATIVE_STR_SLICE:
        return flat_native_str_slice(ctx);
    case FLAT_NATIVE_STR_INDEX_OF:
        return flat_native_str_index_of(ctx);
    case FLAT_NATIVE_STR_CONTAINS:
        return flat_native_str_contains(ctx);
    case FLAT_NATIVE_STR_STARTS_WITH:
        return flat_native_str_starts_with(ctx);
    case FLAT_NATIVE_STR_ENDS_WITH:
        return flat_native_str_ends_with(ctx);
    case FLAT_NATIVE_STR_TRIM:
        return flat_native_str_trim(ctx);
    case FLAT_NATIVE_STR_SPLIT:
        return flat_native_str_split(ctx);
    case FLAT_NATIVE_STR_JOIN:
        return flat_native_str_join(ctx);
    case FLAT_NATIVE_STR_LOWER:
        return flat_native_str_lower(ctx);
    case FLAT_NATIVE_STR_UPPER:
        return flat_native_str_upper(ctx);
    case FLAT_NATIVE_STR_REPLACE:
        return flat_native_str_replace(ctx);
    case FLAT_NATIVE_STR_REPLACE_ALL:
        return flat_native_str_replace_all(ctx);
    case FLAT_NATIVE_JSON_PARSE:
        return flat_native_json_parse(ctx);
    case FLAT_NATIVE_JSON_PARSE_LIST:
        return flat_native_json_parse_list(ctx);
    case FLAT_NATIVE_JSON_PARSE_OBJECT:
        return flat_native_json_parse_object(ctx);
    case FLAT_NATIVE_JSON_PARSE_BOOLS:
        return flat_native_json_parse_bools(ctx);
    case FLAT_NATIVE_JSON_STRINGIFY:
        return flat_native_json_stringify(ctx);
    case FLAT_NATIVE_JSON_CHECK:
        return flat_native_json_check(ctx);
    case FLAT_NATIVE_JSON_FIND:
        return flat_native_json_find(ctx);
    case FLAT_NATIVE_JSON_COUNT:
        return flat_native_json_count(ctx);
    default:
        return -1;
    }
}


int flat_native_invoke(uint16_t id, FlatNativeCtx *ctx) {
    char module[FGIR_NATIVE_MODULE_LEN];
    char err[256];
    FlatNativeThreadCapability policy;
    int gated = 0;
    int result;

    if (!ctx || id >= FLAT_NATIVE_COUNT) return -1;

    /* Load/publish optional plugin metadata before selecting its invocation gate.
     * Otherwise the first call could observe a built-in default and bypass a
     * plugin's SERIALIZED declaration while the handler is being lazy-loaded. */
    if (flat_native_is_optional_id(id) &&
        flat_native_runtime_prepare(ctx->native_runtime, id, err, sizeof err) != 0) {
        fprintf(stderr, "flvm: %s\n", err[0] ? err : "native preparation failed");
        return -1;
    }

    if (flat_native_module_for_id(id, module, sizeof module) == 0) {
        policy = flat_native_module_worker_policy(module);
        if (flat_native_worker_mode_is_active() &&
            policy != FLAT_NATIVE_THREAD_CAP_THREAD_SAFE &&
            policy != FLAT_NATIVE_THREAD_CAP_SERIALIZED) {
            fprintf(stderr, "flvm: native module %s is not permitted while worker execution is active\n",
                    module);
            return -1;
        }
        if (policy == FLAT_NATIVE_THREAD_CAP_SERIALIZED) {
            if (flat_native_module_gate_enter(module) != 0) return -1;
            gated = 1;
        }
    }

    result = flat_native_invoke_inner(id, ctx);
    if (gated && flat_native_module_gate_leave(module) != 0) return -1;
    return result;
}
