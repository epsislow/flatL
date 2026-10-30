#ifndef FLAT_NATIVE_MOD_H
#define FLAT_NATIVE_MOD_H

#include "flat_native.h"

int flat_native_io_outln(FlatNativeCtx *ctx);
int flat_native_io_out(FlatNativeCtx *ctx);
int flat_native_io_outvar(FlatNativeCtx *ctx);
int flat_native_io_in(FlatNativeCtx *ctx);
int flat_native_io_inln(FlatNativeCtx *ctx);
int flat_native_io_in_num(FlatNativeCtx *ctx);
int flat_native_io_in_bool(FlatNativeCtx *ctx);
int flat_native_io_in_list(FlatNativeCtx *ctx);
int flat_native_io_in_object(FlatNativeCtx *ctx);
int flat_native_io_in_bools(FlatNativeCtx *ctx);
int flat_native_str_to_num(FlatNativeCtx *ctx);
int flat_native_str_to_bool(FlatNativeCtx *ctx);
int flat_native_json_parse(FlatNativeCtx *ctx);
int flat_native_json_parse_list(FlatNativeCtx *ctx);
int flat_native_json_parse_object(FlatNativeCtx *ctx);
int flat_native_json_parse_bools(FlatNativeCtx *ctx);
int flat_native_json_stringify(FlatNativeCtx *ctx);

int flat_native_math_trig_sin(FlatNativeCtx *ctx);
int flat_native_math_trig_cos(FlatNativeCtx *ctx);
int flat_native_math_trig_tan(FlatNativeCtx *ctx);
int flat_native_math_trig_num_pi(FlatNativeCtx *ctx);
int flat_native_math_trig_num_tau(FlatNativeCtx *ctx);
int flat_native_math_trig_deg_to_rad(FlatNativeCtx *ctx);
int flat_native_math_trig_rad_to_deg(FlatNativeCtx *ctx);
int flat_native_math_trig_sin_deg(FlatNativeCtx *ctx);
int flat_native_math_trig_cos_deg(FlatNativeCtx *ctx);
int flat_native_math_trig_tan_deg(FlatNativeCtx *ctx);

int flat_native_math_stats_mean(FlatNativeCtx *ctx);
int flat_native_math_stats_median(FlatNativeCtx *ctx);
int flat_native_math_stats_var(FlatNativeCtx *ctx);
int flat_native_math_stats_stdev(FlatNativeCtx *ctx);

int flat_native_math_pow(FlatNativeCtx *ctx);
int flat_native_math_sqrt(FlatNativeCtx *ctx);
int flat_native_math_exp(FlatNativeCtx *ctx);
int flat_native_math_log(FlatNativeCtx *ctx);
int flat_native_math_log10(FlatNativeCtx *ctx);
int flat_native_math_floor(FlatNativeCtx *ctx);
int flat_native_math_ceil(FlatNativeCtx *ctx);
int flat_native_math_round(FlatNativeCtx *ctx);
int flat_native_math_mod(FlatNativeCtx *ctx);
int flat_native_math_floor_mod(FlatNativeCtx *ctx);
int flat_native_math_sum(FlatNativeCtx *ctx);
int flat_native_math_min(FlatNativeCtx *ctx);
int flat_native_math_max(FlatNativeCtx *ctx);
int flat_native_math_sum2(FlatNativeCtx *ctx);
int flat_native_math_min2(FlatNativeCtx *ctx);
int flat_native_math_max2(FlatNativeCtx *ctx);
int flat_native_math_num_pi(FlatNativeCtx *ctx);
int flat_native_math_num_tau(FlatNativeCtx *ctx);
int flat_native_math_num_e(FlatNativeCtx *ctx);

int flat_native_list_empty(FlatNativeCtx *ctx);
int flat_native_list_pack(FlatNativeCtx *ctx);
int flat_native_list_push(FlatNativeCtx *ctx);
int flat_native_list_get(FlatNativeCtx *ctx);
int flat_native_list_len(FlatNativeCtx *ctx);
int flat_native_list_prepend(FlatNativeCtx *ctx);
int flat_native_list_insert(FlatNativeCtx *ctx);
int flat_native_list_set(FlatNativeCtx *ctx);
int flat_native_list_index_of(FlatNativeCtx *ctx);
int flat_native_list_contains(FlatNativeCtx *ctx);
int flat_native_list_remove_at(FlatNativeCtx *ctx);
int flat_native_list_slice(FlatNativeCtx *ctx);
int flat_native_list_splice(FlatNativeCtx *ctx);

#endif
