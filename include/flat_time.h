#ifndef FLAT_TIME_H
#define FLAT_TIME_H

#include <stddef.h>

typedef enum {
    FLAT_TIME_FMT_FIELD,
    FLAT_TIME_FMT_LABELED
} FlatTimeDisplayStyle;

double flat_time_monotonic_ms(void);

void flat_time_format_duration_display(char *buf, size_t buf_sz, double ms,
                                       FlatTimeDisplayStyle style);

void flat_time_append_field(char *line, size_t line_sz, const char *key, double ms);

void flat_time_append_int_field(char *line, size_t line_sz, const char *key, int value);

/* Append " key=value" using §2.2 for ms; returns 0 on success. */
int flat_time_finish_line(char *line, size_t line_sz);

void flat_time_line_begin(char *line, size_t line_sz);

/* Print e.g. "flc: time_ms link=… compile=… total=… units=1 fmod=0\n" to stderr. */
void flat_time_fprintf_time_ms(const char *prefix, const char *body);

void flat_time_print_tests_footer(double ms);

/* Parse "flc: time_ms" or "flatl: time_ms" in merged capture; optional units/fmod. */
int flat_time_parse_tool_line(const char *capture, const char *tool_prefix, int *units_out,
                              int *fmod_out, int *have_units, int *have_fmod);

/* Parse duration field on a time_ms line (integer ms or M:SS,mmm). */
int flat_time_parse_field_ms(const char *line, const char *key, double *ms_out);

void flat_time_print_runner_suffix(const char *body);

int flat_time_env_enabled(void);

#endif
