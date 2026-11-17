#include "flat_csv.h"

#include <ctype.h>
#include <stdlib.h>
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

static int utf16_ascii_le_score(const unsigned char *data, size_t data_len) {
    size_t pairs = 0;
    size_t hits = 0;
    size_t i;

    if (data_len < 4 || (data_len & 1u)) {
        return 0;
    }
    for (i = 0; i + 1 < data_len; i += 2) {
        pairs++;
        if (data[i + 1] == 0x00 && data[i] != 0x00 && data[i] < 0x80u) {
            hits++;
        }
    }
    if (pairs < 2) {
        return 0;
    }
    return (hits * 2 >= pairs) ? 1 : 0;
}

static int utf16_ascii_be_score(const unsigned char *data, size_t data_len) {
    size_t pairs = 0;
    size_t hits = 0;
    size_t i;

    if (data_len < 4 || (data_len & 1u)) {
        return 0;
    }
    for (i = 0; i + 1 < data_len; i += 2) {
        pairs++;
        if (data[i] == 0x00 && data[i + 1] != 0x00 && data[i + 1] < 0x80u) {
            hits++;
        }
    }
    if (pairs < 2) {
        return 0;
    }
    return (hits * 2 >= pairs) ? 1 : 0;
}

int flat_csv_detect_encoding(const unsigned char *data, size_t data_len, FlatCsvEncoding *out_enc) {
    if (!out_enc) {
        return -1;
    }
    if (data_len >= 2 && data[0] == 0xff && data[1] == 0xfe) {
        *out_enc = FLAT_CSV_ENC_UTF16_LE;
    } else if (data_len >= 2 && data[0] == 0xfe && data[1] == 0xff) {
        *out_enc = FLAT_CSV_ENC_UTF16_BE;
    } else if (data_len >= 3 && data[0] == 0xef && data[1] == 0xbb && data[2] == 0xbf) {
        *out_enc = FLAT_CSV_ENC_UTF8_BOM;
    } else if (utf16_ascii_le_score(data, data_len)) {
        *out_enc = FLAT_CSV_ENC_UTF16_LE;
    } else if (utf16_ascii_be_score(data, data_len)) {
        *out_enc = FLAT_CSV_ENC_UTF16_BE;
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
    if (strcmp(s, "UTF-16-LE") == 0) {
        *out_enc = FLAT_CSV_ENC_UTF16_LE;
        *clear_auto = 0;
        return 0;
    }
    if (strcmp(s, "UTF-16-BE") == 0) {
        *out_enc = FLAT_CSV_ENC_UTF16_BE;
        *clear_auto = 0;
        return 0;
    }
    return -1;
}

static size_t utf16_bom_skip(const unsigned char *data, size_t data_len, FlatCsvEncoding enc) {
    if (data_len >= 2 && data[0] == 0xff && data[1] == 0xfe && enc == FLAT_CSV_ENC_UTF16_LE) {
        return 2;
    }
    if (data_len >= 2 && data[0] == 0xfe && data[1] == 0xff && enc == FLAT_CSV_ENC_UTF16_BE) {
        return 2;
    }
    return 0;
}

static int utf8_push(char **buf, size_t *cap, size_t *used, unsigned int cp) {
    unsigned char seq[4];
    size_t n;
    size_t need;

    if (cp <= 0x7fu) {
        seq[0] = (unsigned char)cp;
        n = 1;
    } else if (cp <= 0x7ffu) {
        seq[0] = (unsigned char)(0xc0u | (cp >> 6));
        seq[1] = (unsigned char)(0x80u | (cp & 0x3fu));
        n = 2;
    } else if (cp <= 0xffffu) {
        seq[0] = (unsigned char)(0xe0u | (cp >> 12));
        seq[1] = (unsigned char)(0x80u | ((cp >> 6) & 0x3fu));
        seq[2] = (unsigned char)(0x80u | (cp & 0x3fu));
        n = 3;
    } else {
        seq[0] = (unsigned char)(0xf0u | (cp >> 18));
        seq[1] = (unsigned char)(0x80u | ((cp >> 12) & 0x3fu));
        seq[2] = (unsigned char)(0x80u | ((cp >> 6) & 0x3fu));
        seq[3] = (unsigned char)(0x80u | (cp & 0x3fu));
        n = 4;
    }
    need = *used + n + 1;
    if (need > *cap) {
        size_t nc = *cap ? *cap * 2 : 4096;
        char *nb;
        while (nc < need) {
            nc *= 2;
        }
        nb = realloc(*buf, nc);
        if (!nb) {
            return -1;
        }
        *buf = nb;
        *cap = nc;
    }
    memcpy(*buf + *used, seq, n);
    *used += n;
    (*buf)[*used] = '\0';
    return 0;
}

int flat_csv_utf16_to_utf8(const unsigned char *data, size_t data_len, FlatCsvEncoding enc,
                           char **out_utf8, size_t *out_len) {
    char *buf = NULL;
    size_t cap = 0;
    size_t used = 0;
    size_t off;
    int le;

    if (!data || !out_utf8 || !out_len) {
        return -1;
    }
    if (enc != FLAT_CSV_ENC_UTF16_LE && enc != FLAT_CSV_ENC_UTF16_BE) {
        return -1;
    }
    le = enc == FLAT_CSV_ENC_UTF16_LE;
    off = utf16_bom_skip(data, data_len, enc);
    while (off + 1 < data_len) {
        uint16_t cu;
        unsigned int cp;
        if (le) {
            cu = (uint16_t)(data[off] | ((uint16_t)data[off + 1] << 8));
        } else {
            cu = (uint16_t)(((uint16_t)data[off] << 8) | data[off + 1]);
        }
        off += 2;
        if (cu >= 0xd800u && cu <= 0xdbffu) {
            uint16_t lo;
            if (off + 1 >= data_len) {
                cp = 0xfffd;
            } else {
                if (le) {
                    lo = (uint16_t)(data[off] | ((uint16_t)data[off + 1] << 8));
                } else {
                    lo = (uint16_t)(((uint16_t)data[off] << 8) | data[off + 1]);
                }
                off += 2;
                if (lo >= 0xdc00u && lo <= 0xdfffu) {
                    cp =
                        0x10000u + (((unsigned int)(cu - 0xd800u) << 10) | (unsigned int)(lo - 0xdc00u));
                } else {
                    cp = 0xfffd;
                }
            }
        } else if (cu >= 0xdc00u && cu <= 0xdfffu) {
            cp = 0xfffd;
        } else {
            cp = cu;
        }
        if (utf8_push(&buf, &cap, &used, cp) != 0) {
            free(buf);
            return -1;
        }
    }
    *out_utf8 = buf ? buf : calloc(1, 1);
    if (!*out_utf8) {
        free(buf);
        return -1;
    }
  if (!buf) {
        (*out_utf8)[0] = '\0';
    }
    *out_len = used;
    return 0;
}

static int utf8_next_cp(const char *s, size_t len, size_t *pos, unsigned int *out_cp) {
    size_t i;
    unsigned char c0;
    unsigned int cp;
    int need;
    int j;

    if (!s || !pos || !out_cp || *pos >= len) {
        return -1;
    }
    i = *pos;
    c0 = (unsigned char)s[i];
    if (c0 < 0x80u) {
        *out_cp = c0;
        *pos = i + 1;
        return 0;
    }
    if ((c0 & 0xe0u) == 0xc0u) {
        need = 2;
        cp = c0 & 0x1fu;
    } else if ((c0 & 0xf0u) == 0xe0u) {
        need = 3;
        cp = c0 & 0x0fu;
    } else if ((c0 & 0xf8u) == 0xf0u) {
        need = 4;
        cp = c0 & 0x07u;
    } else {
        return -1;
    }
    if (i + (size_t)need > len) {
        return -1;
    }
    for (j = 1; j < need; j++) {
        unsigned char c = (unsigned char)s[i + (size_t)j];
        if ((c & 0xc0u) != 0x80u) {
            return -1;
        }
        cp = (cp << 6) | (c & 0x3fu);
    }
    *out_cp = cp;
    *pos = i + (size_t)need;
    return 0;
}

static int u16_push(unsigned char **buf, size_t *cap, size_t *used, uint16_t cu, int le) {
    size_t need;
    unsigned char b[2];

    if (!buf || !cap || !used) {
        return -1;
    }
    if (le) {
        b[0] = (unsigned char)(cu & 0xffu);
        b[1] = (unsigned char)((cu >> 8) & 0xffu);
    } else {
        b[0] = (unsigned char)((cu >> 8) & 0xffu);
        b[1] = (unsigned char)(cu & 0xffu);
    }
    need = *used + 2;
    if (need > *cap) {
        size_t nc = *cap ? *cap * 2 : 4096;
        unsigned char *nb;
        while (nc < need) {
            nc *= 2;
        }
        nb = realloc(*buf, nc);
        if (!nb) {
            return -1;
        }
        *buf = nb;
        *cap = nc;
    }
    memcpy(*buf + *used, b, 2);
    *used += 2;
    return 0;
}

int flat_csv_utf8_to_utf16(const char *utf8, size_t utf8_len, FlatCsvEncoding enc,
                             unsigned char **out_bytes, size_t *out_len) {
    unsigned char *buf = NULL;
    size_t cap = 0;
    size_t used = 0;
    size_t pos = 0;
    int le;

    if (!out_bytes || !out_len) {
        return -1;
    }
    if (enc != FLAT_CSV_ENC_UTF16_LE && enc != FLAT_CSV_ENC_UTF16_BE) {
        return -1;
    }
    if (!utf8) {
        utf8_len = 0;
    }
    le = enc == FLAT_CSV_ENC_UTF16_LE;
    if (u16_push(&buf, &cap, &used, 0xfeffu, le) != 0) {
        free(buf);
        return -1;
    }
    while (pos < utf8_len) {
        unsigned int cp;
        if (utf8_next_cp(utf8, utf8_len, &pos, &cp) != 0) {
            free(buf);
            return -1;
        }
        if (cp <= 0xffffu) {
            if (u16_push(&buf, &cap, &used, (uint16_t)cp, le) != 0) {
                free(buf);
                return -1;
            }
        } else if (cp <= 0x10ffffu) {
            uint32_t v = cp - 0x10000u;
            uint16_t hi = (uint16_t)(0xd800u | (v >> 10));
            uint16_t lo = (uint16_t)(0xdc00u | (v & 0x3ffu));
            if (u16_push(&buf, &cap, &used, hi, le) != 0 ||
                u16_push(&buf, &cap, &used, lo, le) != 0) {
                free(buf);
                return -1;
            }
        } else {
            free(buf);
            return -1;
        }
    }
    *out_bytes = buf ? buf : calloc(2, 1);
    if (!*out_bytes) {
        free(buf);
        return -1;
    }
    *out_len = used;
    return 0;
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