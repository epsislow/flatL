#ifndef FLAT_CSV_H
#define FLAT_CSV_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef enum {
    FLAT_CSV_LE_LF = 0,
    FLAT_CSV_LE_CRLF,
    FLAT_CSV_LE_CR
} FlatCsvLineEnding;

typedef enum {
    FLAT_CSV_ENC_UTF8 = 0,
    FLAT_CSV_ENC_UTF8_BOM,
    FLAT_CSV_ENC_UTF16_LE,
    FLAT_CSV_ENC_UTF16_BE
} FlatCsvEncoding;

typedef struct {
    char separator;
    char quote;
    FlatCsvLineEnding line_ending;
    FlatCsvEncoding encoding;
    unsigned char separator_auto;
    unsigned char quote_auto;
    unsigned char line_ending_auto;
    unsigned char encoding_auto;
} FlatCsvFormat;

void flat_csv_format_init_auto(FlatCsvFormat *fmt);

/*
 * Parse one CSV record from buf at *pos.
 * Field bytes are appended to arena; out_fields[i] point into arena (nul-terminated).
 * Returns 0 on success, 1 when at end with no record, -1 on error.
 */
typedef struct {
    char *base;
    size_t cap;
    size_t used;
} FlatCsvArena;

int flat_csv_parse_record(const char *buf, size_t buf_len, size_t *pos, char quote, char sep,
                          FlatCsvArena *arena, char **out_fields, size_t *out_lens, int max_fields,
                          int *out_count);

int flat_csv_count_unquoted(const char *line, size_t line_len, char delim, char quote);

int flat_csv_detect_delimiter(const char *sample, size_t sample_len, char quote, char *out_delim);
int flat_csv_detect_line_ending(const char *sample, size_t sample_len, FlatCsvLineEnding *out_le);
int flat_csv_detect_encoding(const unsigned char *data, size_t data_len, FlatCsvEncoding *out_enc);

/* Physical line count outside quotes; trailing empty line counted if file ends with LE. */
int flat_csv_count_physical_lines_file(FILE *fp, long bookmark, FlatCsvLineEnding le, char quote,
                                       int *out_count);

int flat_csv_parse_separator_setting(const char *s, char *out_sep, int *clear_auto);
int flat_csv_parse_line_endings_setting(const char *s, FlatCsvLineEnding *out_le, int *clear_auto);
int flat_csv_parse_encoding_setting(const char *s, FlatCsvEncoding *out_enc, int *clear_auto);
int flat_csv_parse_quote_setting(const char *s, char *out_quote, int *clear_auto);

const char *flat_csv_le_bytes(FlatCsvLineEnding le, size_t *out_len);
int flat_csv_le_from_text(const char *text, size_t text_len, FlatCsvLineEnding le, char quote,
                          int *in_quotes_io, int *out_consumed);

int flat_csv_needs_quote(const char *field, size_t len, char delim, char quote);
int flat_csv_serialize_field(const char *field, size_t len, char delim, char quote, char *out,
                             size_t out_sz, size_t *out_len);
int flat_csv_serialize_row(const char **fields, const size_t *lens, int count, char delim,
                           char quote, FlatCsvLineEnding le, char *out, size_t out_sz,
                           size_t *out_len);

size_t flat_csv_strip_utf8_bom(const unsigned char *data, size_t len);

/* Decodes UTF-16 (per enc, BOM optional). Allocates *out_utf8; caller frees. */
int flat_csv_utf16_to_utf8(const unsigned char *data, size_t data_len, FlatCsvEncoding enc,
                           char **out_utf8, size_t *out_len);

/* Encodes UTF-8 text to UTF-16 with BOM. Allocates *out_bytes; caller frees. */
int flat_csv_utf8_to_utf16(const char *utf8, size_t utf8_len, FlatCsvEncoding enc,
                           unsigned char **out_bytes, size_t *out_len);

#endif