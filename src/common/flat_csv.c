#include "flat_csv.h"

#include <ctype.h>
#include <string.h>

void flat_csv_format_init_auto(FlatCsvFormat *fmt) {
    if (!fmt) {
        return;
    }
    memset(fmt, 0, sizeof *fmt);
    fmt->separator = ',';
    fmt->quote = '"';
    fmt->line_ending = FLAT_CSV_LE_LF;
    fmt->encoding = FLAT_CSV_ENC_UTF8;
    fmt->separator_auto = 1;
    fmt->quote_auto = 1;
    fmt->line_ending_auto = 1;
    fmt->encoding_auto = 1;
}

size_t flat_csv_strip_utf8_bom(const unsigned char *data, size_t len) {
    if (len >= 3 && data[0] == 0xef && data[1] == 0xbb && data[2] == 0xbf) {
        return 3;
    }
    return 0;
}

int flat_csv_count_unquoted(const char *line, size_t line_len, char delim, char quote) {
    int n = 0;
    int q = 0;
    size_t i;
    for (i = 0; i < line_len; i++) {
        char c = line[i];
        if (q) {
            if (c == quote) {
                if (i + 1 < line_len && line[i + 1] == quote) {
                    i++;
                    continue;
                }
                q = 0;
            }
            continue;
        }
        if (c == quote) {
            q = 1;
            continue;
        }
        if (c == delim) {
            n++;
        }
    }
    return n;
}

static int score_delimiter_lines(const char **lines, const int *line_lens, int line_count,
                                 char delim, char quote, int *out_cols) {
    int i;
    double mode;
    double variance = 0.0;
    int consistent = 1;
    int cols0 = 0;
    int counts[32];

    if (!lines || line_count <= 0 || line_count > 32) {
        if (out_cols) {
            *out_cols = 0;
        }
        return -1;
    }
    mode = 0.0;
    for (i = 0; i < line_count; i++) {
        counts[i] = flat_csv_count_unquoted(lines[i], (size_t)line_lens[i], delim, quote) + 1;
        mode += (double)counts[i];
    }
    mode /= (double)line_count;
    cols0 = counts[0];
    for (i = 0; i < line_count; i++) {
        variance += (double)((counts[i] > (int)mode) ? counts[i] - (int)mode
                                                      : (int)mode - counts[i]);
        if (counts[i] - cols0 > 1 || cols0 - counts[i] > 1) {
            consistent = 0;
        }
    }
    if (out_cols) {
        *out_cols = cols0;
    }
    if (!consistent) {
        return -1;
    }
    return (int)((double)line_count * 10.0 - variance);
}

int flat_csv_detect_delimiter(const char *sample, size_t sample_len, char quote, char *out_delim) {
    const char *delims[] = {",", ";", "\t"};
    char delim_chars[] = {',', ';', '\t'};
    const char *lines[32];
    int line_lens[32];
    int line_count = 0;
    size_t i;
    size_t start = 0;
    int best_score = -1;
    int best_cols = 0;
    char best = ',';

    if (!sample || !out_delim) {
        return -1;
    }
    for (i = 0; i < sample_len && line_count < 32; i++) {
        if (sample[i] == '\r' && i + 1 < sample_len && sample[i + 1] == '\n') {
            lines[line_count] = sample + start;
            line_lens[line_count] = (int)(i - start);
            line_count++;
            i++;
            start = i + 1;
        } else if (sample[i] == '\n' || sample[i] == '\r') {
            lines[line_count] = sample + start;
            line_lens[line_count] = (int)(i - start);
            line_count++;
            start = i + 1;
        }
    }
    if (start < sample_len && line_count < 32) {
        lines[line_count] = sample + start;
        line_lens[line_count] = (int)(sample_len - start);
        line_count++;
    }
    {
        int filtered = 0;
        for (i = 0; i < (size_t)line_count; i++) {
            size_t k;
            int ws_only = 1;
            for (k = 0; k < (size_t)line_lens[i]; k++) {
                if (!isspace((unsigned char)lines[i][k])) {
                    ws_only = 0;
                    break;
                }
            }
            if (!ws_only) {
                lines[filtered] = lines[i];
                line_lens[filtered] = line_lens[i];
                filtered++;
            }
        }
        line_count = filtered > 20 ? 20 : filtered;
    }
    for (i = 0; i < 3; i++) {
        int score;
        int cols = 0;
        score = score_delimiter_lines(lines, line_lens, line_count, delim_chars[i], quote, &cols);
        if (score > best_score || (score == best_score && cols > best_cols)) {
            best_score = score;
            best_cols = cols;
            best = delim_chars[i];
        }
        (void)delims[i];
    }
    *out_delim = best;
    return 0;
}

int flat_csv_detect_line_ending(const char *sample, size_t sample_len, FlatCsvLineEnding *out_le) {
    size_t i;
    if (!sample || !out_le) {
        return -1;
    }
    for (i = 0; i + 1 < sample_len; i++) {
        if (sample[i] == '\r' && sample[i + 1] == '\n') {
            *out_le = FLAT_CSV_LE_CRLF;
            return 0;
        }
    }
    for (i = 0; i < sample_len; i++) {
        if (sample[i] == '\r') {
            *out_le = FLAT_CSV_LE_CR;
            return 0;
        }
        if (sample[i] == '\n') {
            *out_le = FLAT_CSV_LE_LF;
            return 0;
        }
    }
    *out_le = FLAT_CSV_LE_LF;
    return 0;
}

int flat_csv_detect_encoding(const unsigned char *data, size_t data_len, FlatCsvEncoding *out_enc) {
    if (!out_enc) {
        return -1;
    }
    if (data_len >= 3 && data[0] == 0xef && data[1] == 0xbb && data[2] == 0xbf) {
        *out_enc = FLAT_CSV_ENC_UTF8_BOM;
    } else {
        *out_enc = FLAT_CSV_ENC_UTF8;
    }
    return 0;
}

static int arena_push_field(FlatCsvArena *arena, const char *bytes, size_t len, char **out_ptr) {
    if (!arena || !out_ptr) {
        return -1;
    }
    if (arena->used + len + 1 > arena->cap) {
        return -1;
    }
    *out_ptr = arena->base + arena->used;
    if (len > 0 && bytes) {
        memcpy(arena->base + arena->used, bytes, len);
    }
    arena->base[arena->used + len] = '\0';
    arena->used += len + 1;
    return 0;
}

int flat_csv_parse_record(const char *buf, size_t buf_len, size_t *pos, char quote, char sep,
                          FlatCsvArena *arena, char **out_fields, size_t *out_lens, int max_fields,
                          int *out_count) {
    int count = 0;
    char field[8192];
    size_t flen = 0;
    size_t i;
    int in_quotes = 0;

    if (!buf || !pos || !arena || !out_fields || !out_lens || !out_count || max_fields <= 0) {
        return -1;
    }
    if (*pos > buf_len) {
        return -1;
    }
    arena->used = 0;
    i = *pos;
    while (i <= buf_len) {
        if (i == buf_len) {
            if (flen > 0 || count > 0) {
                if (count >= max_fields) {
                    return -1;
                }
                if (arena_push_field(arena, field, flen, &out_fields[count]) != 0) {
                    return -1;
                }
                out_lens[count] = flen;
                count++;
            }
            *pos = i;
            *out_count = count;
            return count > 0 ? 0 : 1;
        }
        {
            char c = buf[i];
            if (in_quotes) {
                if (c == quote) {
                    if (i + 1 < buf_len && buf[i + 1] == quote) {
                        if (flen + 1 >= sizeof field) {
                            return -1;
                        }
                        field[flen++] = quote;
                        i += 2;
                        continue;
                    }
                    in_quotes = 0;
                    i++;
                    continue;
                }
                if (flen + 1 >= sizeof field) {
                    return -1;
                }
                field[flen++] = c;
                i++;
                continue;
            }
            if (c == quote) {
                in_quotes = 1;
                i++;
                continue;
            }
            if (c == sep) {
                if (count >= max_fields) {
                    return -1;
                }
                if (arena_push_field(arena, field, flen, &out_fields[count]) != 0) {
                    return -1;
                }
                out_lens[count] = flen;
                count++;
                flen = 0;
                i++;
                continue;
            }
            if (c == '\r') {
                size_t adv = 1;
                if (i + 1 < buf_len && buf[i + 1] == '\n') {
                    adv = 2;
                }
                if (count >= max_fields) {
                    return -1;
                }
                if (arena_push_field(arena, field, flen, &out_fields[count]) != 0) {
                    return -1;
                }
                out_lens[count] = flen;
                count++;
                *pos = i + adv;
                *out_count = count;
                return 0;
            }
            if (c == '\n') {
                if (count >= max_fields) {
                    return -1;
                }
                if (arena_push_field(arena, field, flen, &out_fields[count]) != 0) {
                    return -1;
                }
                out_lens[count] = flen;
                count++;
                *pos = i + 1;
                *out_count = count;
                return 0;
            }
            if (flen + 1 >= sizeof field) {
                return -1;
            }
            field[flen++] = c;
            i++;
        }
    }
    return -1;
}

const char *flat_csv_le_bytes(FlatCsvLineEnding le, size_t *out_len) {
    if (!out_len) {
        return "";
    }
    switch (le) {
    case FLAT_CSV_LE_CRLF:
        *out_len = 2;
        return "\r\n";
    case FLAT_CSV_LE_CR:
        *out_len = 1;
        return "\r";
    default:
        *out_len = 1;
        return "\n";
    }
}

int flat_csv_le_from_text(const char *text, size_t text_len, FlatCsvLineEnding le, char quote,
                          int *in_quotes_io, int *out_consumed) {
    if (!text || !in_quotes_io || !out_consumed) {
        return -1;
    }
    if (*in_quotes_io) {
        *out_consumed = 0;
        return 0;
    }
    if (le == FLAT_CSV_LE_CRLF && text_len >= 2 && text[0] == '\r' && text[1] == '\n') {
        *out_consumed = 2;
        return 1;
    }
    if (le == FLAT_CSV_LE_LF && text_len >= 1 && text[0] == '\n') {
        *out_consumed = 1;
        return 1;
    }
    if (le == FLAT_CSV_LE_CR && text_len >= 1 && text[0] == '\r') {
        if (text_len >= 2 && text[1] == '\n') {
            return 0;
        }
        *out_consumed = 1;
        return 1;
    }
    (void)quote;
    *out_consumed = 0;
    return 0;
}

int flat_csv_count_physical_lines_file(FILE *fp, long bookmark, FlatCsvLineEnding le, char quote,
                                       int *out_count) {
    char chunk[4096];
    int in_quotes = 0;
    int lines = 0;
    int saw_any = 0;
    int trailing_le = 0;

    if (!fp || !out_count) {
        return -1;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        return -1;
    }
    for (;;) {
        size_t n = fread(chunk, 1, sizeof chunk, fp);
        size_t i;
        if (n == 0) {
            break;
        }
        saw_any = 1;
        for (i = 0; i < n; i++) {
            char c = chunk[i];
            if (in_quotes) {
                if (c == quote) {
                    if (i + 1 < n && chunk[i + 1] == quote) {
                        i++;
                        continue;
                    }
                    in_quotes = 0;
                }
                continue;
            }
            if (c == quote) {
                in_quotes = 1;
                continue;
            }
            if (c == '\r' || c == '\n') {
                lines++;
                trailing_le = 1;
                if (c == '\r' && i + 1 < n && chunk[i + 1] == '\n') {
                    i++;
                }
                continue;
            }
            trailing_le = 0;
            (void)le;
        }
        if (n < sizeof chunk) {
            break;
        }
    }
    /* Last row without trailing LE still counts (align splitPhysicalLines in FLEditor). */
    if (saw_any && !trailing_le) {
        lines++;
    }
    if (fseek(fp, bookmark, SEEK_SET) != 0) {
        return -1;
    }
    *out_count = lines > 0 ? lines : (saw_any ? 1 : 0);
    return 0;
}

static int setting_is_auto(const char *s) {
    return s && s[0] == '\0';
}

int flat_csv_parse_separator_setting(const char *s, char *out_sep, int *clear_auto) {
    char buf[16];
    size_t i;
    if (!s || !out_sep || !clear_auto) {
        return -1;
    }
    if (setting_is_auto(s)) {
        *clear_auto = 1;
        return 0;
    }
    snprintf(buf, sizeof buf, "%s", s);
    for (i = 0; buf[i]; i++) {
        buf[i] = (char)tolower((unsigned char)buf[i]);
    }
    if (strcmp(buf, "tab") == 0 || strcmp(buf, "\\t") == 0) {
        *out_sep = '\t';
        *clear_auto = 0;
        return 0;
    }
    if (strcmp(buf, "comma") == 0 || strcmp(buf, ",") == 0) {
        *out_sep = ',';
        *clear_auto = 0;
        return 0;
    }
    if (strcmp(buf, "semicolon") == 0 || strcmp(buf, ";") == 0) {
        *out_sep = ';';
        *clear_auto = 0;
        return 0;
    }
    if (strlen(s) == 1) {
        *out_sep = s[0];
        *clear_auto = 0;
        return 0;
    }
    return -1;
}

int flat_csv_parse_line_endings_setting(const char *s, FlatCsvLineEnding *out_le, int *clear_auto) {
    if (!s || !out_le || !clear_auto) {
        return -1;
    }
    if (setting_is_auto(s)) {
        *clear_auto = 1;
        return 0;
    }
    if (strcmp(s, "LF") == 0) {
        *out_le = FLAT_CSV_LE_LF;
        *clear_auto = 0;
        return 0;
    }
    if (strcmp(s, "CRLF") == 0) {
        *out_le = FLAT_CSV_LE_CRLF;
        *clear_auto = 0;
        return 0;
    }
    if (strcmp(s, "CR") == 0) {
        *out_le = FLAT_CSV_LE_CR;
        *clear_auto = 0;
        return 0;
    }
    return -1;
}

int flat_csv_parse_encoding_setting(const char *s, FlatCsvEncoding *out_enc, int *clear_auto) {
    if (!s || !out_enc || !clear_auto) {
        return -1;
    }
    if (setting_is_auto(s)) {
        *clear_auto = 1;
        return 0;
    }
    if (strcmp(s, "UTF-8") == 0) {
        *out_enc = FLAT_CSV_ENC_UTF8;
        *clear_auto = 0;
        return 0;
    }
    if (strcmp(s, "UTF-8-BOM") == 0) {
        *out_enc = FLAT_CSV_ENC_UTF8_BOM;
        *clear_auto = 0;
        return 0;
    }
    return -1;
}

int flat_csv_parse_quote_setting(const char *s, char *out_quote, int *clear_auto) {
    if (!s || !out_quote || !clear_auto) {
        return -1;
    }
    if (setting_is_auto(s)) {
        *clear_auto = 1;
        return 0;
    }
    if (strlen(s) != 1) {
        return -1;
    }
    *out_quote = s[0];
    *clear_auto = 0;
    return 0;
}

int flat_csv_needs_quote(const char *field, size_t len, char delim, char quote) {
    size_t i;
    if (!field) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        char c = field[i];
        if (c == quote || c == delim || c == '\n' || c == '\r') {
            return 1;
        }
    }
    return 0;
}

int flat_csv_serialize_field(const char *field, size_t len, char delim, char quote, char *out,
                             size_t out_sz, size_t *out_len) {
    size_t o = 0;
    size_t i;
    if (!out || !out_len) {
        return -1;
    }
    if (!field) {
        len = 0;
    }
    if (!flat_csv_needs_quote(field, len, delim, quote)) {
        if (len >= out_sz) {
            return -1;
        }
        memcpy(out, field, len);
        *out_len = len;
        return 0;
    }
    if (o + 1 >= out_sz) {
        return -1;
    }
    out[o++] = quote;
    for (i = 0; i < len; i++) {
        if (field[i] == quote) {
            if (o + 2 >= out_sz) {
                return -1;
            }
            out[o++] = quote;
            out[o++] = quote;
        } else {
            if (o + 1 >= out_sz) {
                return -1;
            }
            out[o++] = field[i];
        }
    }
    if (o + 1 >= out_sz) {
        return -1;
    }
    out[o++] = quote;
    *out_len = o;
    return 0;
}

int flat_csv_serialize_row(const char **fields, const size_t *lens, int count, char delim,
                           char quote, FlatCsvLineEnding le, char *out, size_t out_sz,
                           size_t *out_len) {
    size_t o = 0;
    int i;
    const char *le_bytes;
    size_t le_len;
    if (!fields || !lens || !out || !out_len || count < 0) {
        return -1;
    }
    le_bytes = flat_csv_le_bytes(le, &le_len);
    for (i = 0; i < count; i++) {
        size_t flen;
        if (i > 0) {
            if (o + 1 >= out_sz) {
                return -1;
            }
            out[o++] = delim;
        }
        if (flat_csv_serialize_field(fields[i], lens[i], delim, quote, out + o, out_sz - o,
                                     &flen) != 0) {
            return -1;
        }
        o += flen;
    }
    if (o + le_len >= out_sz) {
        return -1;
    }
    memcpy(out + o, le_bytes, le_len);
    o += le_len;
    *out_len = o;
    return 0;
}