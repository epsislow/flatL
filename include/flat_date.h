#ifndef FLAT_DATE_H
#define FLAT_DATE_H

#include "flat_value.h"

#include <stddef.h>
#include <stdint.h>

typedef struct FlatDateRunState {
    int run_offset_min; /* minutes east of UTC (+180 = UTC+3) */
    int has_run_offset;
    int64_t now_override_sec; /* -1 = use real clock */
} FlatDateRunState;

void flat_date_run_begin(FlatDateRunState *st);

FlatDateRunState *flat_date_global_state(void);

int flat_date_parse_offset_hhmm(const char *s, int *out_min_east);

int flat_date_parse_auto(const char *s, int64_t *out_utc_sec, const FlatDateRunState *st);

int flat_date_parse_strict(const char *s, const char *fmt, int64_t *out_utc_sec,
                           const FlatDateRunState *st);

int flat_date_format(int64_t utc_sec, const char *fmt, char *out, size_t out_sz,
                     const FlatDateRunState *st);

int flat_date_value_to_utc_sec(const FlatValue *v, FlatStringPool *pool, int64_t *out_sec,
                               const FlatDateRunState *st);

int flat_date_add_modifier(int64_t base_sec, const char *mod, int64_t *out_sec);

int flat_date_str_to_date(const char *s, char *out, size_t out_sz);
int flat_date_str_to_time(const char *s, char *out, size_t out_sz);
int flat_date_str_to_datetime(const char *s, char *out, size_t out_sz);

int flat_date_iso_z_from_sec(int64_t utc_sec, char *out, size_t out_sz);
int flat_date_today_string(char *out, size_t out_sz, const FlatDateRunState *st);

int flat_date_time_to_sec(const char *t, int *out_sec);
int flat_date_sec_to_time(int sec, char *out, size_t out_sz);

int64_t flat_date_now_sec(const FlatDateRunState *st);

#endif
