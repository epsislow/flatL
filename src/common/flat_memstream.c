#include "flat_memstream.h"

#include <stdlib.h>
#include <string.h>

#if defined(__GLIBC__) && !defined(__MINGW32__) && !defined(__MSYS__)
#define FLAT_HAVE_GNU_OPEN_MEMSTREAM 1
#endif

#ifdef FLAT_HAVE_GNU_OPEN_MEMSTREAM
#include <stdio.h>

FILE *flat_open_memstream(char **buf, size_t *len) {
    if (!buf || !len) {
        return NULL;
    }
    *buf = NULL;
    *len = 0;
    return open_memstream(buf, len);
}

int flat_close_memstream(FILE *stream) {
    return stream ? fclose(stream) : -1;
}

#else

enum { FLAT_MEMSTREAM_SLOTS = 16 };

typedef struct {
    int used;
    FILE *fp;
    char **out_buf;
    size_t *out_len;
} FlatMemStreamSlot;

static FlatMemStreamSlot g_ms[FLAT_MEMSTREAM_SLOTS];

static FlatMemStreamSlot *slot_for_fp(FILE *fp) {
    int i;
    for (i = 0; i < FLAT_MEMSTREAM_SLOTS; i++) {
        if (g_ms[i].used && g_ms[i].fp == fp) {
            return &g_ms[i];
        }
    }
    return NULL;
}

FILE *flat_open_memstream(char **buf, size_t *len) {
    int i;
    FILE *fp;

    if (!buf || !len) {
        return NULL;
    }
    *buf = NULL;
    *len = 0;
    for (i = 0; i < FLAT_MEMSTREAM_SLOTS; i++) {
        if (!g_ms[i].used) {
            fp = tmpfile();
            if (!fp) {
                return NULL;
            }
            g_ms[i].used = 1;
            g_ms[i].fp = fp;
            g_ms[i].out_buf = buf;
            g_ms[i].out_len = len;
            return fp;
        }
    }
    return NULL;
}

int flat_close_memstream(FILE *stream) {
    FlatMemStreamSlot *sl;
    long end;
    char *data;
    size_t n;

    if (!stream) {
        return -1;
    }
    sl = slot_for_fp(stream);
    if (!sl) {
        return fclose(stream);
    }
    if (fseek(stream, 0, SEEK_END) != 0) {
        fclose(stream);
        sl->used = 0;
        sl->fp = NULL;
        return -1;
    }
    end = ftell(stream);
    if (end < 0) {
        fclose(stream);
        sl->used = 0;
        sl->fp = NULL;
        return -1;
    }
    n = (size_t)end;
    if (fseek(stream, 0, SEEK_SET) != 0) {
        fclose(stream);
        sl->used = 0;
        sl->fp = NULL;
        return -1;
    }
    data = NULL;
    if (n > 0) {
        data = (char *)malloc(n);
        if (!data) {
            fclose(stream);
            sl->used = 0;
            sl->fp = NULL;
            return -1;
        }
        if (fread(data, 1, n, stream) != n) {
            free(data);
            fclose(stream);
            sl->used = 0;
            sl->fp = NULL;
            return -1;
        }
    }
    fclose(stream);
    *sl->out_buf = data;
    *sl->out_len = n;
    sl->used = 0;
    sl->fp = NULL;
    sl->out_buf = NULL;
    sl->out_len = NULL;
    return 0;
}

#endif
