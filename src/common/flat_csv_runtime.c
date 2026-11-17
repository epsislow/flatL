#include "flat_csv_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#define FLAT_CSV_MAX_FIELDS FLAT_LIST_MAX_ELEMS
#define FLAT_CSV_SAMPLE_SZ 65536
#define FLAT_CSV_ARENA_SZ 262144
#define FLAT_CSV_RECORD_SZ 65536

typedef struct {
    unsigned char in_use;
    FlatCsvSessionKind kind;
    uint32_t fp_id;
    uint32_t id;
    FlatCsvFormat fmt;
    long pos;
    int cursor_row;
    int cursor_col;
    int eof;
    int row_count_cached;
    int row_count;
    int cols_cached_row;
    int cols_count;
    long *line_offsets;
    int line_offset_count;
    int line_offset_cap;
    char *write_buf;
    size_t write_len;
    size_t write_cap;
    char *text_buf;
    size_t text_len;
    int text_mode;
} FlatCsvSession;

struct FlatCsvRuntime {
    FlatCsvSession sessions[FLAT_CSV_MAX_SESSIONS];
    uint32_t next_id;
};

static int read_allowed_mode(FlatFileMode mode) {
    return mode == FLAT_FILE_MODE_READ || mode == FLAT_FILE_MODE_RDWR;
}

static int write_allowed_mode(FlatFileMode mode) {
    return mode == FLAT_FILE_MODE_CREATE || mode == FLAT_FILE_MODE_WRITE_AT0 ||
           mode == FLAT_FILE_MODE_RDWR;
}

static FlatCsvSession *sess_by_id(FlatCsvRuntime *rt, uint32_t sid) {
    int i;
    if (!rt || sid < 1) {
        return NULL;
    }
    for (i = 0; i < FLAT_CSV_MAX_SESSIONS; i++) {
        if (rt->sessions[i].in_use && rt->sessions[i].id == sid) {
            return &rt->sessions[i];
        }
    }
    return NULL;
}

static void sess_clear(FlatCsvSession *s) {
    if (!s) {
        return;
    }
    free(s->line_offsets);
    free(s->write_buf);
    free(s->text_buf);
    memset(s, 0, sizeof *s);
}

FlatCsvRuntime *flat_csv_runtime_create(void) {
    FlatCsvRuntime *rt = calloc(1, sizeof *rt);
    if (rt) {
        rt->next_id = 1;
    }
    return rt;
}

void flat_csv_runtime_destroy(FlatCsvRuntime *rt) {
    int i;
    if (!rt) {
        return;
    }
    for (i = 0; i < FLAT_CSV_MAX_SESSIONS; i++) {
        sess_clear(&rt->sessions[i]);
    }
    free(rt);
}

int flat_csv_fp_busy(const FlatCsvRuntime *rt, uint32_t fp_id) {
    int i;
    if (!rt) {
        return 0;
    }
    for (i = 0; i < FLAT_CSV_MAX_SESSIONS; i++) {
        if (rt->sessions[i].in_use && rt->sessions[i].fp_id == fp_id) {
            return 1;
        }
    }
    return 0;
}

int flat_csv_session_live(const FlatCsvRuntime *rt, uint32_t id, int *out_kind) {
    int i;
    if (!rt || !out_kind) {
        return 0;
    }
    for (i = 0; i < FLAT_CSV_MAX_SESSIONS; i++) {
        if (rt->sessions[i].in_use && rt->sessions[i].id == id) {
            *out_kind = (int)rt->sessions[i].kind;
            return 1;
        }
    }
    return 0;
}

int flat_csv_fp_session_kind(const FlatCsvRuntime *rt, uint32_t fp_id) {
    int i;
    if (!rt) {
        return 0;
    }
    for (i = 0; i < FLAT_CSV_MAX_SESSIONS; i++) {
        if (rt->sessions[i].in_use && rt->sessions[i].fp_id == fp_id) {
            return (int)rt->sessions[i].kind;
        }
    }
    return 0;
}

static int panic_msg(char *panic, size_t panic_sz, const char *msg) {
    if (panic && panic_sz) {
        snprintf(panic, panic_sz, "%s", msg);
    }
    return -1;
}

static FILE *sess_fp(const FlatCsvSession *s, FlatFileTable *files) {
    if (!s || !files) {
        return NULL;
    }
    return flat_file_fp(files, s->fp_id);
}

static long file_data_start(FlatCsvFormat *fmt, FILE *fp) {
    unsigned char b[3];
    if (fmt->encoding == FLAT_CSV_ENC_UTF8_BOM) {
        long cur = ftell(fp);
        if (fseek(fp, 0, SEEK_SET) == 0 && fread(b, 1, 3, fp) == 3 && b[0] == 0xef &&
            b[1] == 0xbb && b[2] == 0xbf) {
            return 3;
        }
        fseek(fp, cur, SEEK_SET);
    }
    return 0;
}

static int read_sample(FILE *fp, char *buf, size_t buf_sz, size_t *out_len) {
    size_t n;
    if (!fp || !buf || !out_len) {
        return -1;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        return -1;
    }
    n = fread(buf, 1, buf_sz, fp);
    *out_len = n;
    return 0;
}

static void apply_detect(FlatCsvFormat *fmt, const char *sample, size_t sample_len) {
    FlatCsvEncoding enc;
    FlatCsvLineEnding le;
    char delim;
    const char *body;
    size_t body_len;
    char *decoded = NULL;

    flat_csv_detect_encoding((const unsigned char *)sample, sample_len, &enc);
    if (enc == FLAT_CSV_ENC_UTF16_LE || enc == FLAT_CSV_ENC_UTF16_BE) {
        if (flat_csv_utf16_to_utf8((const unsigned char *)sample, sample_len, enc, &decoded,
                                   &body_len) != 0) {
            decoded = NULL;
            body_len = 0;
        }
        body = decoded ? decoded : sample;
        if (!decoded) {
            body_len = sample_len;
        }
    } else {
        size_t bom = flat_csv_strip_utf8_bom((const unsigned char *)sample, sample_len);
        body = sample + bom;
        body_len = sample_len - bom;
    }
    flat_csv_detect_line_ending(body, body_len, &le);
    flat_csv_detect_delimiter(body, body_len, fmt->quote, &delim);
    free(decoded);
    if (fmt->separator_auto) {
        fmt->separator = delim;
    }
    if (fmt->line_ending_auto) {
        fmt->line_ending = le;
    }
    if (fmt->encoding_auto) {
        fmt->encoding = enc;
    }
}

static int sess_load_utf8_text(FlatCsvSession *s, FILE *fp, char *panic, size_t panic_sz) {
    unsigned char *raw = NULL;
    long sz;
    size_t n;

    if (!s || !fp) {
        return panic_msg(panic, panic_sz, "csv.reader");
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        return panic_msg(panic, panic_sz, "csv.reader");
    }
    sz = ftell(fp);
    if (sz < 0) {
        return panic_msg(panic, panic_sz, "csv.reader");
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        return panic_msg(panic, panic_sz, "csv.reader");
    }
    if (sz > 0) {
        raw = malloc((size_t)sz);
        if (!raw) {
            return panic_msg(panic, panic_sz, "csv.reader");
        }
        n = fread(raw, 1, (size_t)sz, fp);
        if ((long)n != sz) {
            free(raw);
            return panic_msg(panic, panic_sz, "csv.reader");
        }
    }
    if (flat_csv_utf16_to_utf8(raw, (size_t)sz, s->fmt.encoding, &s->text_buf, &s->text_len) !=
        0) {
        free(raw);
        return panic_msg(panic, panic_sz, "csv.reader");
    }
    free(raw);
    s->text_mode = 1;
    s->row_count_cached = 0;
    free(s->line_offsets);
    s->line_offsets = NULL;
    return 0;
}

static int sess_getc(FlatCsvSession *s, FILE *fp) {
    if (s->text_mode) {
        if ((size_t)s->pos >= s->text_len) {
            return EOF;
        }
        return (unsigned char)s->text_buf[(size_t)s->pos++];
    }
    return fgetc(fp);
}

static void sess_ungetc(FlatCsvSession *s, FILE *fp, int c) {
    if (c == EOF) {
        return;
    }
    if (s->text_mode) {
        if ((size_t)s->pos > 0) {
            s->pos--;
        }
        return;
    }
    ungetc(c, fp);
}

static long sess_tell(FlatCsvSession *s, FILE *fp) {
    if (s->text_mode) {
        return s->pos;
    }
    return ftell(fp);
}

static int sess_seek_set(FlatCsvSession *s, FILE *fp, long pos) {
    if (s->text_mode) {
        if (pos < 0 || (size_t)pos > s->text_len) {
            return -1;
        }
        s->pos = pos;
        return 0;
    }
    return fseek(fp, pos, SEEK_SET);
}

static int sess_ensure_detect(FlatCsvSession *s, FILE *fp) {
    char sample[FLAT_CSV_SAMPLE_SZ];
    size_t n;
    if (!s->fmt.separator_auto && !s->fmt.line_ending_auto && !s->fmt.encoding_auto &&
        !s->fmt.quote_auto) {
        return 0;
    }
    if (read_sample(fp, sample, sizeof sample, &n) != 0) {
        return -1;
    }
    apply_detect(&s->fmt, sample, n);
    return 0;
}

static int fp_busy_other(FlatCsvRuntime *rt, uint32_t fp_id) {
    return flat_csv_fp_busy(rt, fp_id);
}

static int alloc_slot(FlatCsvRuntime *rt) {
    int i;
    for (i = 0; i < FLAT_CSV_MAX_SESSIONS; i++) {
        if (!rt->sessions[i].in_use) {
            return i;
        }
    }
    return -1;
}

uint32_t flat_csv_reader_open(FlatCsvRuntime *rt, FlatFileTable *files, uint32_t fp_id,
                              char *panic, size_t panic_sz) {
    FlatCsvSession *s;
    int slot;
    FILE *fp;

    if (!rt || !files || !flat_file_is_live(files, fp_id)) {
        panic_msg(panic, panic_sz, "csv.fp.closed");
        return 0;
    }
    if (!read_allowed_mode(files->slots[fp_id].mode)) {
        panic_msg(panic, panic_sz, "csv.reader.mode");
        return 0;
    }
    if (fp_busy_other(rt, fp_id)) {
        panic_msg(panic, panic_sz, "csv.fp.busy");
        return 0;
    }
    slot = alloc_slot(rt);
    if (slot < 0) {
        panic_msg(panic, panic_sz, "csv.reader");
        return 0;
    }
    fp = flat_file_fp(files, fp_id);
    s = &rt->sessions[slot];
    memset(s, 0, sizeof *s);
    s->in_use = 1;
    s->kind = FLAT_CSV_SESS_READER;
    s->fp_id = fp_id;
    s->id = rt->next_id++;
    flat_csv_format_init_auto(&s->fmt);
    if (sess_ensure_detect(s, fp) != 0) {
        sess_clear(s);
        panic_msg(panic, panic_sz, "csv.reader");
        return 0;
    }
    if (s->fmt.encoding == FLAT_CSV_ENC_UTF16_LE || s->fmt.encoding == FLAT_CSV_ENC_UTF16_BE) {
        if (sess_load_utf8_text(s, fp, panic, panic_sz) != 0) {
            sess_clear(s);
            return 0;
        }
        s->pos = 0;
    } else {
        s->pos = file_data_start(&s->fmt, fp);
    }
    s->cursor_row = 0;
    s->cursor_col = 0;
    flat_file_retain(files, fp_id);
    return s->id;
}

static int writer_truncate(FILE *fp) {
    if (!fp) {
        return -1;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        return -1;
    }
#ifdef _WIN32
    if (_chsize(_fileno(fp), 0) != 0) {
        return -1;
    }
#else
    if (ftruncate(fileno(fp), 0) != 0) {
        return -1;
    }
#endif
    return 0;
}

uint32_t flat_csv_writer_open(FlatCsvRuntime *rt, FlatFileTable *files, uint32_t fp_id,
                              char *panic, size_t panic_sz) {
    FlatCsvSession *s;
    int slot;
    FILE *fp;

    if (!rt || !files || !flat_file_is_live(files, fp_id)) {
        panic_msg(panic, panic_sz, "csv.fp.closed");
        return 0;
    }
    if (files->slots[fp_id].mode == FLAT_FILE_MODE_READ ||
        files->slots[fp_id].mode == FLAT_FILE_MODE_APPEND) {
        panic_msg(panic, panic_sz, "csv.writer.mode");
        return 0;
    }
    if (!write_allowed_mode(files->slots[fp_id].mode)) {
        panic_msg(panic, panic_sz, "csv.writer.mode");
        return 0;
    }
    if (fp_busy_other(rt, fp_id)) {
        panic_msg(panic, panic_sz, "csv.fp.busy");
        return 0;
    }
    slot = alloc_slot(rt);
    if (slot < 0) {
        panic_msg(panic, panic_sz, "csv.writer");
        return 0;
    }
    fp = flat_file_fp(files, fp_id);
    if (writer_truncate(fp) != 0) {
        panic_msg(panic, panic_sz, "csv.writer");
        return 0;
    }
    s = &rt->sessions[slot];
    memset(s, 0, sizeof *s);
    s->in_use = 1;
    s->kind = FLAT_CSV_SESS_WRITER;
    s->fp_id = fp_id;
    s->id = rt->next_id++;
    flat_csv_format_init_auto(&s->fmt);
    flat_file_retain(files, fp_id);
    return s->id;
}

int flat_csv_close(FlatCsvRuntime *rt, uint32_t sess_id, FlatFileTable *files, char *panic,
                   size_t panic_sz) {
    FlatCsvSession *s = sess_by_id(rt, sess_id);
    if (!s) {
        return panic_msg(panic, panic_sz, "csv.close");
    }
    if (s->kind == FLAT_CSV_SESS_WRITER) {
        if (flat_csv_writer_flush(rt, sess_id, files, panic, panic_sz) != 0) {
            return -1;
        }
    }
    if (files) {
        flat_file_release(files, s->fp_id);
    }
    sess_clear(s);
    return 0;
}

static int ensure_line_index(FlatCsvSession *s, FILE *fp) {
    long start;
    long mark;
    int cap;
    int in_quotes = 0;

    if (s->row_count_cached && s->line_offsets) {
        return 0;
    }
    start = s->text_mode ? 0 : file_data_start(&s->fmt, fp);
    mark = sess_tell(s, fp);
    if (sess_seek_set(s, fp, start) != 0) {
        return -1;
    }
    cap = 32;
    s->line_offsets = malloc((size_t)cap * sizeof(long));
    if (!s->line_offsets) {
        return -1;
    }
    s->line_offset_count = 0;
    s->line_offset_cap = cap;
    s->line_offsets[s->line_offset_count++] = start;
    for (;;) {
        int c = sess_getc(s, fp);
        if (c == EOF) {
            break;
        }
        if (in_quotes) {
            if (c == (unsigned char)s->fmt.quote) {
                int n = sess_getc(s, fp);
                if (n == (unsigned char)s->fmt.quote) {
                    continue;
                }
                if (n != EOF) {
                    sess_ungetc(s, fp, n);
                }
                in_quotes = 0;
            }
            continue;
        }
        if (c == (unsigned char)s->fmt.quote) {
            in_quotes = 1;
            continue;
        }
        if (c == '\r' || c == '\n') {
            if (c == '\r') {
                int n = sess_getc(s, fp);
                if (n != '\n' && n != EOF) {
                    sess_ungetc(s, fp, n);
                }
            }
            if (s->line_offset_count >= s->line_offset_cap) {
                s->line_offset_cap += 32;
                s->line_offsets =
                    realloc(s->line_offsets, (size_t)s->line_offset_cap * sizeof(long));
                if (!s->line_offsets) {
                    return -1;
                }
            }
            s->line_offsets[s->line_offset_count++] = sess_tell(s, fp);
            continue;
        }
    }
    {
        int physical = 0;
        s->row_count = s->line_offset_count > 0 ? s->line_offset_count : 1;
        if (!s->text_mode &&
            flat_csv_count_physical_lines_file(fp, mark, s->fmt.line_ending, s->fmt.quote,
                                               &physical) == 0 &&
            physical > s->row_count) {
            s->row_count = physical;
        }
    }
    s->row_count_cached = 1;
    sess_seek_set(s, fp, mark);
    return 0;
}

int flat_csv_reader_reset(FlatCsvRuntime *rt, FlatFileTable *files, uint32_t rid) {
    FlatCsvSession *s = sess_by_id(rt, rid);
    FILE *fp;
    if (!s || s->kind != FLAT_CSV_SESS_READER) {
        return -1;
    }
    fp = sess_fp(s, files);
    if (!fp) {
        return -1;
    }
    s->cursor_row = 0;
    s->cursor_col = 0;
    s->eof = 0;
    s->cols_cached_row = -1;
    s->row_count_cached = 0;
    free(s->line_offsets);
    s->line_offsets = NULL;
    s->line_offset_count = 0;
    if (s->text_mode) {
        s->pos = 0;
    } else {
        s->pos = file_data_start(&s->fmt, fp);
    }
    return 0;
}

int flat_csv_reader_is_eof(const FlatCsvRuntime *rt, uint32_t rid, int *out_eof) {
    FlatCsvSession *s = sess_by_id((FlatCsvRuntime *)rt, rid);
    if (!s || s->kind != FLAT_CSV_SESS_READER || !out_eof) {
        return -1;
    }
    *out_eof = s->eof;
    return 0;
}

static int sess_with_detect_and_lines(FlatCsvSession *s, FlatFileTable *files) {
    FILE *fp = sess_fp(s, files);
    if (!fp) {
        return -1;
    }
    if (sess_ensure_detect(s, fp) != 0) {
        return -1;
    }
    if (ensure_line_index(s, fp) != 0) {
        return -1;
    }
    return 0;
}

int flat_csv_reader_rows(FlatCsvRuntime *rt, FlatFileTable *files, uint32_t rid, int *out_rows) {
    FlatCsvSession *s = sess_by_id(rt, rid);
    if (!s || s->kind != FLAT_CSV_SESS_READER || !out_rows) {
        return -1;
    }
    if (sess_with_detect_and_lines(s, files) != 0) {
        return -1;
    }
    *out_rows = s->row_count;
    return 0;
}

int flat_csv_reader_get_row(const FlatCsvRuntime *rt, uint32_t rid, int *out_row) {
    FlatCsvSession *s = sess_by_id((FlatCsvRuntime *)rt, rid);
    if (!s || s->kind != FLAT_CSV_SESS_READER || !out_row) {
        return -1;
    }
    *out_row = s->cursor_row;
    return 0;
}

int flat_csv_reader_get_col(const FlatCsvRuntime *rt, uint32_t rid, int *out_col) {
    FlatCsvSession *s = sess_by_id((FlatCsvRuntime *)rt, rid);
    if (!s || s->kind != FLAT_CSV_SESS_READER || !out_col) {
        return -1;
    }
    *out_col = s->cursor_col;
    return 0;
}

int flat_csv_reader_set_row(FlatCsvRuntime *rt, FlatFileTable *files, uint32_t rid, int row,
                            char *panic, size_t panic_sz) {
    FlatCsvSession *s = sess_by_id(rt, rid);
    if (!s || s->kind != FLAT_CSV_SESS_READER) {
        return panic_msg(panic, panic_sz, "csv.setRow");
    }
    if (row < 0) {
        return panic_msg(panic, panic_sz, "csv.setRow");
    }
    if (sess_with_detect_and_lines(s, files) != 0) {
        return panic_msg(panic, panic_sz, "csv.setRow");
    }
    if (row >= s->line_offset_count) {
        return panic_msg(panic, panic_sz, "csv.setRow");
    }
    s->cursor_row = row;
    s->cursor_col = 0;
    s->pos = s->line_offsets[row];
    s->cols_cached_row = -1;
    s->eof = 0;
    return 0;
}

static int read_record_into_buf(FILE *fp, FlatCsvSession *s, char *buf, size_t buf_sz,
                                size_t *out_len, int *at_eof) {
    size_t o = 0;
    int in_quotes = 0;
    if (sess_seek_set(s, fp, s->pos) != 0) {
        return -1;
    }
    for (;;) {
        int c = sess_getc(s, fp);
        if (c == EOF) {
            *at_eof = 1;
            break;
        }
        if (in_quotes) {
            if (c == (unsigned char)s->fmt.quote) {
                int n = sess_getc(s, fp);
                if (n == (unsigned char)s->fmt.quote) {
                    if (o + 1 >= buf_sz) {
                        return -1;
                    }
                    buf[o++] = (char)s->fmt.quote;
                    continue;
                }
                if (n != EOF) {
                    sess_ungetc(s, fp, n);
                }
                in_quotes = 0;
                continue;
            }
            if (o + 1 >= buf_sz) {
                return -1;
            }
            buf[o++] = (char)c;
            continue;
        }
        if (c == (unsigned char)s->fmt.quote) {
            in_quotes = 1;
            continue;
        }
        if (c == '\r' || c == '\n') {
            if (c == '\r') {
                int n = sess_getc(s, fp);
                if (n != '\n' && n != EOF) {
                    sess_ungetc(s, fp, n);
                }
            }
            s->pos = sess_tell(s, fp);
            break;
        }
        if (o + 1 >= buf_sz) {
            return -1;
        }
        buf[o++] = (char)c;
    }
    if (o == 0 && *at_eof) {
        s->eof = 1;
        return 1;
    }
    *out_len = o;
    *at_eof = 0;
    return 0;
}

static int parse_line_fields(FlatCsvSession *s, const char *rec, size_t rlen, int *out_count) {
    char arena_buf[FLAT_CSV_ARENA_SZ];
    FlatCsvArena arena = {arena_buf, sizeof arena_buf, 0};
    char *fields[FLAT_CSV_MAX_FIELDS];
    size_t lens[FLAT_CSV_MAX_FIELDS];
    size_t pos = 0;
    int nfields = 0;

    if (flat_csv_parse_record(rec, rlen, &pos, s->fmt.quote, s->fmt.separator, &arena, fields,
                              lens, FLAT_CSV_MAX_FIELDS, &nfields) != 0) {
        return -1;
    }
    if (nfields == 0 && rlen == 0) {
        nfields = 1;
    }
    *out_count = nfields;
    if (s->cols_cached_row != s->cursor_row) {
        s->cols_count = nfields;
        s->cols_cached_row = s->cursor_row;
    }
    return 0;
}

int flat_csv_reader_cols(FlatCsvRuntime *rt, FlatFileTable *files, uint32_t rid, int *out_cols) {
    FlatCsvSession *s = sess_by_id(rt, rid);
    char rec[FLAT_CSV_RECORD_SZ];
    size_t rlen;
    int at_eof = 0;
    int nfields;
    FILE *fp;
    long save_pos;

    if (!s || s->kind != FLAT_CSV_SESS_READER || !out_cols) {
        return -1;
    }
    if (s->cols_cached_row == s->cursor_row) {
        *out_cols = s->cols_count;
        return 0;
    }
    fp = sess_fp(s, files);
    if (!fp || sess_ensure_detect(s, fp) != 0) {
        return -1;
    }
    save_pos = s->pos;
    if (read_record_into_buf(fp, s, rec, sizeof rec, &rlen, &at_eof) != 0) {
        s->pos = save_pos;
        return -1;
    }
    s->pos = save_pos;
    if (parse_line_fields(s, rec, rlen, &nfields) != 0) {
        return -1;
    }
    *out_cols = nfields;
    return 0;
}

static int list_from_fields(FlatCollectionPool *coll, FlatStringPool *pool, char **fields,
                            size_t *lens, int from, int count, uint32_t *out_id) {
    FlatValue items[FLAT_CSV_MAX_FIELDS];
    int i;
    for (i = 0; i < count; i++) {
        uint32_t ref;
        if (flat_pool_alloc_copy(pool, fields[from + i], lens[from + i], &ref) != 0) {
            return -1;
        }
        if (flat_value_set_str_ref(&items[i], ref) != 0) {
            return -1;
        }
    }
    return flat_list_build(coll, pool, items, count, out_id);
}

int flat_csv_reader_next(FlatCsvRuntime *rt, FlatFileTable *files, uint32_t rid,
                         FlatCollectionPool *coll, FlatStringPool *pool, uint32_t *out_list_id,
                         char *panic, size_t panic_sz) {
    FlatCsvSession *s = sess_by_id(rt, rid);
    char rec[FLAT_CSV_RECORD_SZ];
    char arena_buf[FLAT_CSV_ARENA_SZ];
    FlatCsvArena arena = {arena_buf, sizeof arena_buf, 0};
    char *fields[FLAT_CSV_MAX_FIELDS];
    size_t lens[FLAT_CSV_MAX_FIELDS];
    size_t pos = 0;
    int nfields = 0;
    size_t rlen;
    int at_eof = 0;
    FILE *fp;

    if (!s || s->kind != FLAT_CSV_SESS_READER || !coll || !pool || !out_list_id) {
        return panic_msg(panic, panic_sz, "csv.next");
    }
    if (s->eof) {
        return panic_msg(panic, panic_sz, "csv.next");
    }
    fp = sess_fp(s, files);
    if (!fp || sess_ensure_detect(s, fp) != 0) {
        return panic_msg(panic, panic_sz, "csv.next");
    }
    if (read_record_into_buf(fp, s, rec, sizeof rec, &rlen, &at_eof) == 1) {
        return panic_msg(panic, panic_sz, "csv.next");
    }
    if (flat_csv_parse_record(rec, rlen, &pos, s->fmt.quote, s->fmt.separator, &arena, fields, lens,
                              FLAT_CSV_MAX_FIELDS, &nfields) != 0) {
        return panic_msg(panic, panic_sz, "csv.next");
    }
    if (nfields > FLAT_LIST_MAX_ELEMS) {
        return panic_msg(panic, panic_sz, "csv.next");
    }
    if (nfields == 0) {
        nfields = 1;
        fields[0] = arena_buf;
        lens[0] = 0;
        arena_buf[0] = '\0';
    }
    if (list_from_fields(coll, pool, fields, lens, 0, nfields, out_list_id) != 0) {
        return panic_msg(panic, panic_sz, "csv.next");
    }
    s->cursor_row++;
    s->cursor_col = 0;
    s->cols_cached_row = -1;
  if (at_eof && rlen == 0) {
        s->eof = 1;
    }
    return 0;
}

int flat_csv_reader_next_cells(FlatCsvRuntime *rt, FlatFileTable *files, uint32_t rid, int len,
                               FlatCollectionPool *coll, FlatStringPool *pool,
                               uint32_t *out_list_id, char *panic, size_t panic_sz) {
    FlatCsvSession *s = sess_by_id(rt, rid);
    char rec[FLAT_CSV_RECORD_SZ];
    char arena_buf[FLAT_CSV_ARENA_SZ];
    FlatCsvArena arena = {arena_buf, sizeof arena_buf, 0};
    char *fields[FLAT_CSV_MAX_FIELDS];
    size_t lens[FLAT_CSV_MAX_FIELDS];
    size_t pos = 0;
    int nfields = 0;
    size_t rlen;
    int at_eof = 0;
    FILE *fp;
    long save_pos;

    if (!s || s->kind != FLAT_CSV_SESS_READER || len < 1 || !coll || !pool || !out_list_id) {
        return panic_msg(panic, panic_sz, "csv.nextCells");
    }
    fp = sess_fp(s, files);
    if (!fp || sess_ensure_detect(s, fp) != 0) {
        return panic_msg(panic, panic_sz, "csv.nextCells");
    }
    save_pos = s->pos;
    if (read_record_into_buf(fp, s, rec, sizeof rec, &rlen, &at_eof) != 0) {
        s->pos = save_pos;
        return panic_msg(panic, panic_sz, "csv.nextCells");
    }
    s->pos = save_pos;
    if (flat_csv_parse_record(rec, rlen, &pos, s->fmt.quote, s->fmt.separator, &arena, fields, lens,
                              FLAT_CSV_MAX_FIELDS, &nfields) != 0) {
        return panic_msg(panic, panic_sz, "csv.nextCells");
    }
    if (s->cursor_col + len > nfields) {
        return panic_msg(panic, panic_sz, "csv.nextCells");
    }
    if (list_from_fields(coll, pool, fields, lens, s->cursor_col, len, out_list_id) != 0) {
        return panic_msg(panic, panic_sz, "csv.nextCells");
    }
    s->cursor_col += len;
    return 0;
}

static int parse_row_at(FlatCsvSession *s, FILE *fp, int row, FlatCsvArena *arena, char **fields,
                        size_t *lens, int max_fields, int *out_count) {
    char rec[FLAT_CSV_RECORD_SZ];
    size_t rlen = 0;
    int at_eof = 0;
    long save_pos;
    int save_row;
    int save_col;
    int save_eof;
    int save_cols_row;
    int save_cols_count;
    size_t pos = 0;

    if (!s->line_offsets || row < 0 || row >= s->line_offset_count || !arena) {
        return -1;
    }
    arena->used = 0;
    save_pos = s->pos;
    save_row = s->cursor_row;
    save_col = s->cursor_col;
    save_eof = s->eof;
    save_cols_row = s->cols_cached_row;
    save_cols_count = s->cols_count;
    s->cursor_row = row;
    s->cursor_col = 0;
    s->eof = 0;
    s->cols_cached_row = -1;
    s->pos = s->line_offsets[row];
    if (read_record_into_buf(fp, s, rec, sizeof rec, &rlen, &at_eof) != 0) {
        s->pos = save_pos;
        s->cursor_row = save_row;
        s->cursor_col = save_col;
        s->eof = save_eof;
        s->cols_cached_row = save_cols_row;
        s->cols_count = save_cols_count;
        return -1;
    }
    if (flat_csv_parse_record(rec, rlen, &pos, s->fmt.quote, s->fmt.separator, arena, fields, lens,
                              max_fields, out_count) != 0) {
        s->pos = save_pos;
        s->cursor_row = save_row;
        s->cursor_col = save_col;
        s->eof = save_eof;
        s->cols_cached_row = save_cols_row;
        s->cols_count = save_cols_count;
        return -1;
    }
    if (*out_count == 0 && rlen == 0) {
        *out_count = 1;
        fields[0] = arena->base;
        lens[0] = 0;
        arena->base[0] = '\0';
        arena->used = 1;
    }
    s->pos = save_pos;
    s->cursor_row = save_row;
    s->cursor_col = save_col;
    s->eof = save_eof;
    s->cols_cached_row = save_cols_row;
    s->cols_count = save_cols_count;
    return 0;
}

int flat_csv_reader_get_cells(FlatCsvRuntime *rt, FlatFileTable *files, uint32_t rid, int x0,
                               int y0, int xLen, int yLen, int as_object,
                               FlatCollectionPool *coll, FlatStringPool *pool, uint32_t *out_coll_id,
                               int *out_is_object, char *panic, size_t panic_sz) {
    FlatCsvSession *s = sess_by_id(rt, rid);
    FILE *fp;
    int r;
    int y;

    if (!s || s->kind != FLAT_CSV_SESS_READER || !coll || !pool || !out_coll_id || !out_is_object) {
        return panic_msg(panic, panic_sz, "csv.getCells");
    }
    if (xLen < 1 || yLen < 1) {
        return panic_msg(panic, panic_sz, "bounds");
    }
    if (xLen > FLAT_LIST_MAX_ELEMS || yLen > FLAT_LIST_MAX_ELEMS ||
        xLen > FLAT_LIST_MAX_ELEMS / yLen) {
        return panic_msg(panic, panic_sz, "csv.getCells");
    }
    fp = sess_fp(s, files);
    if (!fp || sess_with_detect_and_lines(s, files) != 0) {
        return panic_msg(panic, panic_sz, "csv.getCells");
    }
    if (y0 < 0 || x0 < 0 || yLen > s->line_offset_count || y0 > s->line_offset_count - yLen) {
        return panic_msg(panic, panic_sz, "bounds");
    }

    if (!as_object) {
        FlatValue row_vals[64];
        uint32_t row_ids[64];
        if (yLen > 64) {
            return panic_msg(panic, panic_sz, "csv.getCells");
        }
        {
            char arena_buf[FLAT_CSV_RECORD_SZ];
            FlatCsvArena arena = {arena_buf, sizeof arena_buf, 0};
            for (y = 0; y < yLen; y++) {
            char *fields[FLAT_CSV_MAX_FIELDS];
            size_t lens[FLAT_CSV_MAX_FIELDS];
            int nfields = 0;
            if (parse_row_at(s, fp, y0 + y, &arena, fields, lens, FLAT_CSV_MAX_FIELDS,
                             &nfields) != 0) {
                return panic_msg(panic, panic_sz, "csv.getCells");
            }
            if (x0 + xLen > nfields) {
                return panic_msg(panic, panic_sz, "bounds");
            }
            if (list_from_fields(coll, pool, fields, lens, x0, xLen, &row_ids[y]) != 0) {
                return panic_msg(panic, panic_sz, "csv.getCells");
            }
            if (flat_value_set_list(&row_vals[y], row_ids[y]) != 0) {
                return panic_msg(panic, panic_sz, "csv.getCells");
            }
            flat_value_retain(coll, pool, &row_vals[y]);
            }
        }
        if (yLen == 1) {
            *out_coll_id = row_ids[0];
        } else if (flat_list_build(coll, pool, row_vals, yLen, out_coll_id) != 0) {
            return panic_msg(panic, panic_sz, "csv.getCells");
        }
        for (y = 0; y < yLen; y++) {
            flat_value_release(coll, pool, &row_vals[y]);
        }
        *out_is_object = 0;
        return 0;
    }

    {
        FlatValue cols[FLAT_CSV_MAX_FIELDS];
        char key_bufs[FLAT_CSV_MAX_FIELDS][FLAT_OBJ_KEY_MAX + 1];
        if (xLen > FLAT_CSV_MAX_FIELDS) {
            return panic_msg(panic, panic_sz, "csv.getCells");
        }
        char arena_buf[FLAT_CSV_RECORD_SZ];
        FlatCsvArena arena = {arena_buf, sizeof arena_buf, 0};
        for (r = 0; r < xLen; r++) {
            FlatValue cells[64];
            uint32_t col_id;
            if (yLen > 64) {
                return panic_msg(panic, panic_sz, "csv.getCells");
            }
            snprintf(key_bufs[r], sizeof key_bufs[r], "%d", r);
            for (y = 0; y < yLen; y++) {
                char *fields[FLAT_CSV_MAX_FIELDS];
                size_t lens[FLAT_CSV_MAX_FIELDS];
                int nfields = 0;
                uint32_t ref;
                if (parse_row_at(s, fp, y0 + y, &arena, fields, lens, FLAT_CSV_MAX_FIELDS,
                                 &nfields) != 0) {
                    return panic_msg(panic, panic_sz, "csv.getCells");
                }
                if (x0 + xLen > nfields) {
                    return panic_msg(panic, panic_sz, "bounds");
                }
                if (flat_pool_alloc_copy(pool, fields[x0 + r], lens[x0 + r], &ref) != 0) {
                    return panic_msg(panic, panic_sz, "csv.getCells");
                }
                if (flat_value_set_str_ref(&cells[y], ref) != 0) {
                    return panic_msg(panic, panic_sz, "csv.getCells");
                }
            }
            if (flat_list_build(coll, pool, cells, yLen, &col_id) != 0) {
                return panic_msg(panic, panic_sz, "csv.getCells");
            }
            if (flat_value_set_list(&cols[r], col_id) != 0) {
                return panic_msg(panic, panic_sz, "csv.getCells");
            }
            flat_value_retain(coll, pool, &cols[r]);
        }
        if (flat_obj_build(coll, pool, key_bufs, cols, xLen, out_coll_id) != 0) {
            for (r = 0; r < xLen; r++) {
                flat_value_release(coll, pool, &cols[r]);
            }
            return panic_msg(panic, panic_sz, "csv.getCells");
        }
        for (r = 0; r < xLen; r++) {
            flat_value_release(coll, pool, &cols[r]);
        }
        *out_is_object = 1;
        return 0;
    }
}

static int format_set_common(FlatCsvSession *s, const char *val, int kind, char *panic,
                             size_t panic_sz) {
    int clear = 0;
    if (!s) {
        return panic_msg(panic, panic_sz, "csv.set");
    }
    if (kind == 0) {
        char sep;
        if (flat_csv_parse_separator_setting(val, &sep, &clear) != 0) {
            return panic_msg(panic, panic_sz, "csv.setSeparator");
        }
        if (clear) {
            s->fmt.separator_auto = 1;
        } else {
            s->fmt.separator = sep;
            s->fmt.separator_auto = 0;
        }
    } else if (kind == 1) {
        FlatCsvLineEnding le;
        if (flat_csv_parse_line_endings_setting(val, &le, &clear) != 0) {
            return panic_msg(panic, panic_sz, "csv.setLineEndings");
        }
        if (clear) {
            s->fmt.line_ending_auto = 1;
            s->row_count_cached = 0;
        } else {
            s->fmt.line_ending = le;
            s->fmt.line_ending_auto = 0;
            s->row_count_cached = 0;
        }
    } else if (kind == 2) {
        FlatCsvEncoding enc;
        if (flat_csv_parse_encoding_setting(val, &enc, &clear) != 0) {
            return panic_msg(panic, panic_sz, "csv.setEncoding");
        }
        if (clear) {
            s->fmt.encoding_auto = 1;
        } else {
            s->fmt.encoding = enc;
            s->fmt.encoding_auto = 0;
        }
    } else {
        char q;
        if (flat_csv_parse_quote_setting(val, &q, &clear) != 0) {
            return panic_msg(panic, panic_sz, "csv.setQuote");
        }
        if (clear) {
            s->fmt.quote_auto = 1;
            s->fmt.quote = '"';
        } else {
            s->fmt.quote = q;
            s->fmt.quote_auto = 0;
        }
    }
    return 0;
}

int flat_csv_format_set_separator(FlatCsvRuntime *rt, uint32_t sess_id, const char *val, char *panic,
                                  size_t panic_sz) {
    FlatCsvSession *s = sess_by_id(rt, sess_id);
    return format_set_common(s, val, 0, panic, panic_sz);
}

int flat_csv_format_set_line_endings(FlatCsvRuntime *rt, uint32_t sess_id, const char *val,
                                     char *panic, size_t panic_sz) {
    FlatCsvSession *s = sess_by_id(rt, sess_id);
    return format_set_common(s, val, 1, panic, panic_sz);
}

int flat_csv_format_set_encoding(FlatCsvRuntime *rt, uint32_t sess_id, const char *val, char *panic,
                                 size_t panic_sz) {
    FlatCsvSession *s = sess_by_id(rt, sess_id);
    return format_set_common(s, val, 2, panic, panic_sz);
}

int flat_csv_format_set_quote(FlatCsvRuntime *rt, uint32_t sess_id, const char *val, char *panic,
                              size_t panic_sz) {
    FlatCsvSession *s = sess_by_id(rt, sess_id);
    return format_set_common(s, val, 3, panic, panic_sz);
}

static int writer_append(FlatCsvSession *s, const char *data, size_t len) {
    if (s->write_len + len > s->write_cap) {
        size_t ncap = s->write_cap ? s->write_cap * 2 : 4096;
        while (ncap < s->write_len + len) {
            ncap *= 2;
        }
        char *nu = realloc(s->write_buf, ncap);
        if (!nu) {
            return -1;
        }
        s->write_buf = nu;
        s->write_cap = ncap;
    }
    memcpy(s->write_buf + s->write_len, data, len);
    s->write_len += len;
    return 0;
}

static int value_to_cstr(const FlatValue *v, FlatStringPool *pool, char *buf, size_t buf_sz) {
    if (flat_value_get_cstr(v, pool, buf, buf_sz) != 0) {
        return -1;
    }
    return 0;
}

int flat_csv_writer_write_rows(FlatCsvRuntime *rt, uint32_t wid, const FlatValue *rows_val,
                               FlatCollectionPool *coll, FlatStringPool *pool, char *panic,
                               size_t panic_sz) {
    FlatCsvSession *s = sess_by_id(rt, wid);
    int nrows;
    int r;
    if (!s || s->kind != FLAT_CSV_SESS_WRITER || rows_val->tag != VAL_LIST) {
        return panic_msg(panic, panic_sz, "csv.writeRows");
    }
    if (flat_list_len(coll, rows_val->u.coll_id, &nrows) != 0) {
        return panic_msg(panic, panic_sz, "csv.writeRows");
    }
    for (r = 0; r < nrows; r++) {
        FlatValue rowv;
        int ncols;
        int c;
        const char *fields[FLAT_CSV_MAX_FIELDS];
        size_t lens[FLAT_CSV_MAX_FIELDS];
        char field_bufs[FLAT_CSV_MAX_FIELDS][512];
        char line[131072];
        size_t line_len;
        if (flat_list_get(coll, rows_val->u.coll_id, r, &rowv) != 0 ||
            rowv.tag != VAL_LIST) {
            return panic_msg(panic, panic_sz, "csv.writeRows");
        }
        if (flat_list_len(coll, rowv.u.coll_id, &ncols) != 0) {
            return panic_msg(panic, panic_sz, "csv.writeRows");
        }
        if (ncols > FLAT_CSV_MAX_FIELDS) {
            return panic_msg(panic, panic_sz, "csv.writeRows");
        }
        for (c = 0; c < ncols; c++) {
            FlatValue cell;
            if (flat_list_get(coll, rowv.u.coll_id, c, &cell) != 0) {
                return panic_msg(panic, panic_sz, "csv.writeRows");
            }
            if (value_to_cstr(&cell, pool, field_bufs[c], sizeof field_bufs[c]) != 0) {
                return panic_msg(panic, panic_sz, "csv.writeRows");
            }
            fields[c] = field_bufs[c];
            lens[c] = strlen(fields[c]);
        }
        if (flat_csv_serialize_row(fields, lens, ncols, s->fmt.separator, s->fmt.quote,
                                   s->fmt.line_ending, line, sizeof line, &line_len) != 0) {
            return panic_msg(panic, panic_sz, "csv.writeRows");
        }
        if (writer_append(s, line, line_len) != 0) {
            return panic_msg(panic, panic_sz, "csv.writeRows");
        }
    }
    return 0;
}

int flat_csv_writer_write_cols(FlatCsvRuntime *rt, uint32_t wid, const FlatValue *cols_val,
                               FlatCollectionPool *coll, FlatStringPool *pool, char *panic,
                               size_t panic_sz) {
    FlatCsvSession *s = sess_by_id(rt, wid);
    int nkeys;
    int nrows = -1;
    int k;
    if (!s || s->kind != FLAT_CSV_SESS_WRITER || cols_val->tag != VAL_OBJECT) {
        return panic_msg(panic, panic_sz, "csv.writeCols");
    }
    if (flat_obj_len(coll, cols_val->u.coll_id, &nkeys) != 0 || nkeys < 1) {
        return panic_msg(panic, panic_sz, "csv.writeCols");
    }
    for (k = 0; k < nkeys; k++) {
        const char *key;
        FlatValue colv;
        int clen;
        if (flat_obj_entry_at(coll, cols_val->u.coll_id, k, &key, &colv) != 0 ||
            colv.tag != VAL_LIST) {
            return panic_msg(panic, panic_sz, "csv.writeCols");
        }
        if (flat_list_len(coll, colv.u.coll_id, &clen) != 0) {
            return panic_msg(panic, panic_sz, "csv.writeCols");
        }
        if (nrows < 0) {
            nrows = clen;
        } else if (clen != nrows) {
            return panic_msg(panic, panic_sz, "csv.writeCols.length");
        }
    }
    {
        const char *hfields[FLAT_CSV_MAX_FIELDS];
        size_t hlens[FLAT_CSV_MAX_FIELDS];
        char line[131072];
        size_t line_len;
        for (k = 0; k < nkeys; k++) {
            const char *key;
            FlatValue colv;
            if (flat_obj_entry_at(coll, cols_val->u.coll_id, k, &key, &colv) != 0) {
                return panic_msg(panic, panic_sz, "csv.writeCols");
            }
            hfields[k] = key;
            hlens[k] = strlen(key);
        }
        if (flat_csv_serialize_row(hfields, hlens, nkeys, s->fmt.separator, s->fmt.quote,
                                   s->fmt.line_ending, line, sizeof line, &line_len) != 0) {
            return panic_msg(panic, panic_sz, "csv.writeCols");
        }
        if (writer_append(s, line, line_len) != 0) {
            return panic_msg(panic, panic_sz, "csv.writeCols");
        }
    }
    {
        int r;
        for (r = 0; r < nrows; r++) {
            char line[131072];
            const char *fields[FLAT_CSV_MAX_FIELDS];
            size_t lens[FLAT_CSV_MAX_FIELDS];
            char field_bufs[FLAT_CSV_MAX_FIELDS][512];
            size_t line_len;
            int c = 0;
            for (k = 0; k < nkeys; k++) {
                const char *key;
                FlatValue colv;
                FlatValue cell;
                (void)key;
                if (flat_obj_entry_at(coll, cols_val->u.coll_id, k, &key, &colv) != 0) {
                    return panic_msg(panic, panic_sz, "csv.writeCols");
                }
                if (flat_list_get(coll, colv.u.coll_id, r, &cell) != 0) {
                    return panic_msg(panic, panic_sz, "csv.writeCols");
                }
                if (value_to_cstr(&cell, pool, field_bufs[c], sizeof field_bufs[c]) != 0) {
                    return panic_msg(panic, panic_sz, "csv.writeCols");
                }
                fields[c] = field_bufs[c];
                lens[c] = strlen(fields[c]);
                c++;
            }
            if (flat_csv_serialize_row(fields, lens, nkeys, s->fmt.separator, s->fmt.quote,
                                       s->fmt.line_ending, line, sizeof line, &line_len) != 0) {
                return panic_msg(panic, panic_sz, "csv.writeCols");
            }
            if (writer_append(s, line, line_len) != 0) {
                return panic_msg(panic, panic_sz, "csv.writeCols");
            }
        }
    }
    return 0;
}

static FlatCsvEncoding writer_output_encoding(const FlatCsvSession *s) {
    if (!s) {
        return FLAT_CSV_ENC_UTF8;
    }
    if (s->fmt.encoding_auto) {
        return FLAT_CSV_ENC_UTF8;
    }
    return s->fmt.encoding;
}

static int writer_fwrite_all(FILE *fp, const void *data, size_t len) {
    const unsigned char *p = data;
    size_t left = len;
    while (left > 0) {
        size_t n = fwrite(p, 1, left, fp);
        if (n == 0) {
            return -1;
        }
        p += n;
        left -= n;
    }
    return 0;
}

int flat_csv_writer_flush(FlatCsvRuntime *rt, uint32_t wid, FlatFileTable *files, char *panic,
                          size_t panic_sz) {
    FlatCsvSession *s = sess_by_id(rt, wid);
    FILE *fp;
    FlatCsvEncoding enc;
    if (!s || s->kind != FLAT_CSV_SESS_WRITER) {
        return panic_msg(panic, panic_sz, "csv.flush");
    }
    fp = sess_fp(s, files);
    if (!fp) {
        return panic_msg(panic, panic_sz, "csv.fp.closed");
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        return panic_msg(panic, panic_sz, "csv.flush");
    }
    enc = writer_output_encoding(s);
    if (enc == FLAT_CSV_ENC_UTF16_LE || enc == FLAT_CSV_ENC_UTF16_BE) {
        unsigned char *out = NULL;
        size_t out_len = 0;
        if (flat_csv_utf8_to_utf16(s->write_buf, s->write_len, enc, &out, &out_len) != 0) {
            free(out);
            return panic_msg(panic, panic_sz, "csv.flush");
        }
        if (writer_fwrite_all(fp, out, out_len) != 0) {
            free(out);
            return panic_msg(panic, panic_sz, "csv.flush");
        }
        free(out);
    } else {
        if (enc == FLAT_CSV_ENC_UTF8_BOM) {
            static const unsigned char bom[3] = {0xef, 0xbb, 0xbf};
            if (writer_fwrite_all(fp, bom, sizeof bom) != 0) {
                return panic_msg(panic, panic_sz, "csv.flush");
            }
        }
        if (s->write_len > 0 && writer_fwrite_all(fp, s->write_buf, s->write_len) != 0) {
            return panic_msg(panic, panic_sz, "csv.flush");
        }
    }
    fflush(fp);
    return 0;
}