#include "flat_native.h"
#include "flat_native_mod.h"
#include "flat_native_plugin.h"

int flat_native_invoke(uint16_t id, FlatNativeCtx *ctx) {
    if (!ctx || id >= FLAT_NATIVE_COUNT) {
        return -1;
    }
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
    case FLAT_NATIVE_STR_TO_NUM:
        return flat_native_str_to_num(ctx);
    case FLAT_NATIVE_STR_TO_BOOL:
        return flat_native_str_to_bool(ctx);
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
    default:
        return -1;
    }
}
