#include "flat_number.h"

#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "check failed at line %d: %s\\n", __LINE__, #cond);                  \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)


/* op mapping in flat_number_compare_rel(): 0 ==, 1 >=, 2 <=, 3 >, 4 <. */
static int check_pair(const FlatValue *a, const FlatValue *b, int expected_cmp,
                      const char *label) {
    char panic[64] = {0};
    int eq = expected_cmp == 0;
    int ge = expected_cmp >= 0;
    int le = expected_cmp <= 0;
    int gt = expected_cmp > 0;
    int lt = expected_cmp < 0;
#define PAIR_CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "comparison failed (%s) at line %d: %s\\n", label, __LINE__, #cond); \
    return 1; } } while (0)
    PAIR_CHECK(flat_number_equals(a, b) == eq);
    PAIR_CHECK(flat_number_equals(b, a) == eq);
    PAIR_CHECK(flat_number_compare_rel(a, b, 0, panic, sizeof panic) == eq);
    PAIR_CHECK(flat_number_compare_rel(a, b, 1, panic, sizeof panic) == ge);
    PAIR_CHECK(flat_number_compare_rel(a, b, 2, panic, sizeof panic) == le);
    PAIR_CHECK(flat_number_compare_rel(a, b, 3, panic, sizeof panic) == gt);
    PAIR_CHECK(flat_number_compare_rel(a, b, 4, panic, sizeof panic) == lt);
    PAIR_CHECK(flat_number_compare_rel(b, a, 0, panic, sizeof panic) == eq);
    PAIR_CHECK(flat_number_compare_rel(b, a, 1, panic, sizeof panic) == le);
    PAIR_CHECK(flat_number_compare_rel(b, a, 2, panic, sizeof panic) == ge);
    PAIR_CHECK(flat_number_compare_rel(b, a, 3, panic, sizeof panic) == lt);
    PAIR_CHECK(flat_number_compare_rel(b, a, 4, panic, sizeof panic) == gt);
#undef PAIR_CHECK
    return 0;
}

int main(void) {
    FlatValue a, b, out;
    char panic[64] = {0};
    FlatSlotTypeKind suffix = FLAT_SLOT_TYPE_NONE;
    int had_suffix = 0;

    flat_number_set_whole_u64(&a, UINT64_MAX);
    flat_number_set_whole_u64(&b, UINT64_MAX - 1u);
    CHECK(!flat_number_equals(&a, &b));
    CHECK(flat_number_compare_rel(&a, &b, 3, panic, sizeof panic) == 1);
    CHECK(flat_number_compare_rel(&a, &b, 4, panic, sizeof panic) == 0);

    flat_number_set_whole_i64(&a, INT64_MIN);
    flat_number_set_whole_u64(&b, 0);
    CHECK(flat_number_compare_rel(&a, &b, 4, panic, sizeof panic) == 1);
    flat_number_set_whole_i64(&a, 1);
    flat_number_set_whole_u64(&b, 1);
    CHECK(flat_number_equals(&a, &b));

    CHECK(flat_number_parse_literal_with_optional_suffix(
              "18446744073709551615:u64", &out, &suffix, &had_suffix, panic, sizeof panic) == 0);
    CHECK(had_suffix && suffix == FLAT_SLOT_TYPE_U64);
    CHECK(out.u.number.payload.u64 == UINT64_MAX);
    CHECK(flat_number_parse_literal_with_optional_suffix(
              "18446744073709551616:u64", &out, &suffix, &had_suffix, panic, sizeof panic) != 0);

    CHECK(flat_number_parse_literal_with_optional_suffix(
              "-9223372036854775808:s64", &out, &suffix, &had_suffix, panic, sizeof panic) == 0);
    CHECK(out.u.number.payload.i64 == INT64_MIN);

    flat_number_set_decimal_f64(&a, 1.0000000000000002);
    CHECK(flat_number_coerce_for_slot(&a, FLAT_SLOT_TYPE_U8, &out, panic, sizeof panic) != 0);
    flat_number_set_decimal_f64(&a, 1.0);
    CHECK(flat_number_coerce_for_slot(&a, FLAT_SLOT_TYPE_U8, &out, panic, sizeof panic) == 0);
    CHECK(out.u.number.payload.u64 == 1);

    CHECK(flat_number_parse_literal_with_optional_suffix("0.1:f32", &out, &suffix, &had_suffix,
                                                         panic, sizeof panic) == 0);
    CHECK(had_suffix && suffix == FLAT_SLOT_TYPE_F32);
    CHECK(flat_number_stor(&out) == FLAT_NUM_STOR_F32);


    /* Every pair checks ==, >=, <=, >, < in both operand orders. */
    flat_number_set_whole_u64(&a, UINT64_MAX);
    flat_number_set_whole_u64(&b, UINT64_MAX - 1u);
    CHECK(check_pair(&a, &b, 1, "u64 max adjacent") == 0);
    flat_number_set_whole_u64(&a, UINT64_C(9007199254740993));
    flat_number_set_decimal_f64(&b, 9007199254740992.0);
    CHECK(check_pair(&a, &b, 1, "u64 above 2^53 vs f64 2^53") == 0);
    flat_number_set_whole_u64(&a, UINT64_C(9007199254740992));
    flat_number_set_decimal_f64(&b, 9007199254740992.0);
    CHECK(check_pair(&a, &b, 0, "u64 exactly 2^53 vs f64") == 0);
    flat_number_set_whole_u64(&a, UINT64_MAX);
    flat_number_set_decimal_f64(&b, 0x1p64);
    CHECK(check_pair(&a, &b, -1, "u64 max below 2^64 f64") == 0);
    flat_number_set_whole_u64(&a, UINT64_C(9223372036854775808));
    flat_number_set_whole_i64(&b, INT64_MAX);
    CHECK(check_pair(&a, &b, 1, "u64 2^63 above s64 max") == 0);
    flat_number_set_whole_u64(&a, 0);
    flat_number_set_whole_i64(&b, INT64_MIN);
    CHECK(check_pair(&a, &b, 1, "u64 zero above s64 min") == 0);
    flat_number_set_whole_i64(&a, -1);
    flat_number_set_whole_u64(&b, UINT64_MAX);
    CHECK(check_pair(&a, &b, -1, "negative s64 below u64 max") == 0);
    flat_number_set_whole_i64(&a, INT64_MIN);
    flat_number_set_decimal_f64(&b, -0x1p63);
    CHECK(check_pair(&a, &b, 0, "s64 min equals exact f64") == 0);
    flat_number_set_whole_i64(&a, INT64_MAX);
    flat_number_set_decimal_f64(&b, 0x1p63);
    CHECK(check_pair(&a, &b, -1, "s64 max below 2^63 f64") == 0);
    flat_number_set_whole_i64(&a, INT64_MAX);
    flat_number_set_decimal_f64(&b, 9223372036854774784.0);
    CHECK(check_pair(&a, &b, 1, "s64 max above next lower f64") == 0);

    flat_number_set_whole_i64(&a, 1);
    flat_number_set_decimal_f64(&b, nextafter(1.0, 2.0));
    CHECK(check_pair(&a, &b, -1, "integer below next f64 after one") == 0);
    flat_number_set_whole_i64(&a, 1);
    flat_number_set_decimal_f64(&b, nextafter(1.0, 0.0));
    CHECK(check_pair(&a, &b, 1, "integer above previous f64 before one") == 0);
    flat_number_set_whole_i64(&a, 0);
    flat_number_set_decimal_f64(&b, -0.5);
    CHECK(check_pair(&a, &b, 1, "zero above negative fraction") == 0);
    flat_number_set_whole_u64(&a, 0);
    flat_number_set_decimal_f64(&b, 0.5);
    CHECK(check_pair(&a, &b, -1, "zero below positive fraction") == 0);
    flat_number_set_decimal_f64(&a, 1.25);
    flat_number_set_decimal_f64(&b, 1.5);
    CHECK(check_pair(&a, &b, -1, "decimal fraction ordering") == 0);
    flat_number_set_decimal_f32(&a, 0.5f);
    flat_number_set_decimal_f64(&b, 0.5);
    CHECK(check_pair(&a, &b, 0, "f32 equals exact f64") == 0);
    flat_number_set_decimal_f32(&a, 0.1f);
    flat_number_set_decimal_f64(&b, 0.1);
    CHECK(check_pair(&a, &b, 1, "f32 0.1 differs from f64 0.1") == 0);
    flat_number_set_decimal_f64(&a, -0.0);
    flat_number_set_whole_u64(&b, 0);
    CHECK(check_pair(&a, &b, 0, "negative zero equals integer zero") == 0);


    /* Storage-width matrix: equal values across widths, then adjacent limits. */
    CHECK(flat_number_set_for_slot_kind(&a, FLAT_SLOT_TYPE_U8, 255, 0, 0.0) == 0);
    CHECK(flat_number_set_for_slot_kind(&b, FLAT_SLOT_TYPE_U16, 255, 0, 0.0) == 0);
    CHECK(check_pair(&a, &b, 0, "u8/u16 same maximum") == 0);
    CHECK(flat_number_set_for_slot_kind(&b, FLAT_SLOT_TYPE_U16, 256, 0, 0.0) == 0);
    CHECK(check_pair(&a, &b, -1, "u8 max below u16 next") == 0);
    CHECK(flat_number_set_for_slot_kind(&a, FLAT_SLOT_TYPE_U16, 65535, 0, 0.0) == 0);
    CHECK(flat_number_set_for_slot_kind(&b, FLAT_SLOT_TYPE_U32, 65535, 0, 0.0) == 0);
    CHECK(check_pair(&a, &b, 0, "u16/u32 same maximum") == 0);
    CHECK(flat_number_set_for_slot_kind(&b, FLAT_SLOT_TYPE_U32, 65536, 0, 0.0) == 0);
    CHECK(check_pair(&a, &b, -1, "u16 max below u32 next") == 0);
    CHECK(flat_number_set_for_slot_kind(&a, FLAT_SLOT_TYPE_U32, UINT32_MAX, 0, 0.0) == 0);
    CHECK(flat_number_set_for_slot_kind(&b, FLAT_SLOT_TYPE_U64, UINT32_MAX, 0, 0.0) == 0);
    CHECK(check_pair(&a, &b, 0, "u32/u64 same maximum") == 0);
    CHECK(flat_number_set_for_slot_kind(&b, FLAT_SLOT_TYPE_U64, (uint64_t)UINT32_MAX + 1u, 0, 0.0) == 0);
    CHECK(check_pair(&a, &b, -1, "u32 max below u64 next") == 0);

    CHECK(flat_number_set_for_slot_kind(&a, FLAT_SLOT_TYPE_S8, 0, INT8_MIN, 0.0) == 0);
    CHECK(flat_number_set_for_slot_kind(&b, FLAT_SLOT_TYPE_S16, 0, INT8_MIN, 0.0) == 0);
    CHECK(check_pair(&a, &b, 0, "s8/s16 same minimum") == 0);
    CHECK(flat_number_set_for_slot_kind(&b, FLAT_SLOT_TYPE_S16, 0, INT8_MIN - 1, 0.0) == 0);
    CHECK(check_pair(&a, &b, 1, "s8 min above s16 lower") == 0);
    CHECK(flat_number_set_for_slot_kind(&a, FLAT_SLOT_TYPE_S16, 0, INT16_MAX, 0.0) == 0);
    CHECK(flat_number_set_for_slot_kind(&b, FLAT_SLOT_TYPE_S32, 0, INT16_MAX, 0.0) == 0);
    CHECK(check_pair(&a, &b, 0, "s16/s32 same maximum") == 0);
    CHECK(flat_number_set_for_slot_kind(&b, FLAT_SLOT_TYPE_S32, 0, (int64_t)INT16_MAX + 1, 0.0) == 0);
    CHECK(check_pair(&a, &b, -1, "s16 max below s32 next") == 0);
    CHECK(flat_number_set_for_slot_kind(&a, FLAT_SLOT_TYPE_S32, 0, INT32_MIN, 0.0) == 0);
    CHECK(flat_number_set_for_slot_kind(&b, FLAT_SLOT_TYPE_S64, 0, INT32_MIN, 0.0) == 0);
    CHECK(check_pair(&a, &b, 0, "s32/s64 same minimum") == 0);
    CHECK(flat_number_set_for_slot_kind(&b, FLAT_SLOT_TYPE_S64, 0, (int64_t)INT32_MIN - 1, 0.0) == 0);
    CHECK(check_pair(&a, &b, 1, "s32 min above s64 lower") == 0);

    CHECK(flat_number_set_for_slot_kind(&a, FLAT_SLOT_TYPE_U8, 127, 0, 0.0) == 0);
    CHECK(flat_number_set_for_slot_kind(&b, FLAT_SLOT_TYPE_S8, 0, 127, 0.0) == 0);
    CHECK(check_pair(&a, &b, 0, "u8/s8 common positive value") == 0);
    CHECK(flat_number_set_for_slot_kind(&a, FLAT_SLOT_TYPE_U8, 128, 0, 0.0) == 0);
    CHECK(flat_number_set_for_slot_kind(&b, FLAT_SLOT_TYPE_S8, 0, 127, 0.0) == 0);
    CHECK(check_pair(&a, &b, 1, "u8 128 above s8 max") == 0);
    CHECK(flat_number_set_for_slot_kind(&a, FLAT_SLOT_TYPE_U64, 0, 0, 0.0) == 0);
    CHECK(flat_number_set_for_slot_kind(&b, FLAT_SLOT_TYPE_S64, 0, -1, 0.0) == 0);
    CHECK(check_pair(&a, &b, 1, "unsigned zero above signed minus one") == 0);

    /* f32's 24-bit significand boundary: 2^24+1 cannot round to 2^24. */
    flat_number_set_whole_u64(&a, UINT64_C(16777217));
    flat_number_set_decimal_f32(&b, 16777216.0f);
    CHECK(check_pair(&a, &b, 1, "integer above f32 2^24") == 0);
    flat_number_set_whole_u64(&a, UINT64_C(16777216));
    flat_number_set_decimal_f32(&b, 16777216.0f);
    CHECK(check_pair(&a, &b, 0, "integer equals f32 2^24") == 0);
    flat_number_set_whole_i64(&a, -16777217);
    flat_number_set_decimal_f32(&b, -16777216.0f);
    CHECK(check_pair(&a, &b, -1, "negative integer below f32 -2^24") == 0);

    /* NaN is unordered: equality is false and relational compare reports panic. */
    flat_number_set_decimal_f64(&a, NAN);
    flat_number_set_whole_i64(&b, 0);
    CHECK(!flat_number_equals(&a, &b));
    CHECK(flat_number_compare_rel(&a, &b, 0, panic, sizeof panic) == -2);
    CHECK(strcmp(panic, "type.compare.nan") == 0);
    flat_number_set_decimal_f64(&a, INFINITY);
    flat_number_set_whole_u64(&b, UINT64_MAX);
    CHECK(check_pair(&a, &b, 1, "positive infinity above u64 max") == 0);
    flat_number_set_decimal_f64(&a, -INFINITY);
    flat_number_set_whole_i64(&b, INT64_MIN);
    CHECK(check_pair(&a, &b, -1, "negative infinity below s64 min") == 0);

    puts("AM-NUM regression checks passed");
    return 0;
}
