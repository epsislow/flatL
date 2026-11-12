#ifndef FLAT_NUMBER_H
#define FLAT_NUMBER_H

#include "flat_slot_type.h"
#include "flat_value.h"

#include <stdio.h>

typedef enum {
    FLAT_NUM_CLASS_DECIMAL = 0,
    FLAT_NUM_CLASS_WHOLE = 1
} FlatNumClass;

typedef enum {
    FLAT_NUM_STOR_F64 = 0,
    FLAT_NUM_STOR_F32,
    FLAT_NUM_STOR_U8,
    FLAT_NUM_STOR_U16,
    FLAT_NUM_STOR_U32,
    FLAT_NUM_STOR_U64,
    FLAT_NUM_STOR_S8,
    FLAT_NUM_STOR_S16,
    FLAT_NUM_STOR_S32,
    FLAT_NUM_STOR_S64
} FlatNumStor;

int flat_number_is_number(const FlatValue *v);
FlatNumClass flat_number_class(const FlatValue *v);
FlatNumStor flat_number_stor(const FlatValue *v);

void flat_number_set_decimal_f64(FlatValue *v, double d);
void flat_number_set_decimal_f32(FlatValue *v, float f);

void flat_number_set_whole_u64(FlatValue *v, uint64_t n);
void flat_number_set_whole_i64(FlatValue *v, int64_t n);

int flat_number_set_for_slot_kind(FlatValue *v, FlatSlotTypeKind kind, uint64_t u, int64_t i,
                                  double frac);

void flat_number_init_legacy_double(FlatValue *v, double d);

double flat_number_as_f64(const FlatValue *v);
int flat_number_has_nan(const FlatValue *v);

int flat_number_descriptor_equal(const FlatValue *a, const FlatValue *b);
int flat_number_equals(const FlatValue *a, const FlatValue *b);
int flat_number_compare_rel(const FlatValue *a, const FlatValue *b, int op, char *panic,
                            size_t panic_sz);

int flat_number_coerce_for_slot(const FlatValue *src, FlatSlotTypeKind kind, FlatValue *out,
                                  char *panic, size_t panic_sz);

int flat_number_parse_untyped(const char *token, FlatValue *out);
int flat_number_parse_for_slot_kind(const char *token, FlatSlotTypeKind kind, FlatValue *out,
                                    char *panic, size_t panic_sz);

int flat_number_parse_literal_with_optional_suffix(const char *lit, FlatValue *out,
                                                   FlatSlotTypeKind *suffix_kind, int *had_suffix,
                                                   char *panic, size_t panic_sz);

int flat_number_literal_suffix_matches_slot(const FlatSlotType *contract,
                                            FlatSlotTypeKind suffix_kind);

int flat_number_is_negative_literal_token(const char *token);
int flat_number_is_negative_value(const FlatValue *v);

void flat_number_fprint_decimal(FILE *out, const FlatValue *v);

FlatSlotTypeKind flat_number_stor_to_slot_kind(FlatNumStor stor);
int flat_number_kind_to_stor(FlatSlotTypeKind kind, FlatNumStor *out);

#endif
