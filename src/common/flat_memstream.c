#include "flat_memstream.h"

#include <stdlib.h>
#include <string.h>

#if defined(__GLIBC__) && !defined(__MINGW32__) && !defined(__MSYS__) && \
    !defined(FLAT_FORCE_MEMSTREAM_FALLBACK)
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

#include <stdatomic.h>
#include <stdio.h>

typedef struct FlatMemStreamSlot {
    FILE *fp;
    char **out_buf;
    size_t *out_len;
    struct FlatMemStreamSlot *next;
} FlatMemStreamSlot;

/* Process-owned lookup registry; stream contents and output targets remain caller-owned. */
static FlatMemStreamSlot *g_ms;
static atomic_flag g_ms_lock = ATOMIC_FLAG_INIT;

static void registry_lock(void) {
    while (atomic_flag_test_and_set_explicit(&g_ms_lock, memory_order_acquire)) { }
}
static void registry_unlock(void) {
    atomic_flag_clear_explicit(&g_ms_lock, memory_order_release);
}

FILE *flat_open_memstream(char **buf, size_t *len) {
    FILE *fp;
    FlatMemStreamSlot *slot;
    if (!buf || !len) return NULL;
    *buf = NULL;
    *len = 0;
    fp = tmpfile();
    if (!fp) return NULL;
    slot = (FlatMemStreamSlot *)malloc(sizeof *slot);
    if (!slot) { fclose(fp); return NULL; }
    slot->fp = fp;
    slot->out_buf = buf;
    slot->out_len = len;
    registry_lock();
    slot->next = g_ms;
    g_ms = slot;
    registry_unlock();
    return fp;
}

int flat_close_memstream(FILE *stream) {
    FlatMemStreamSlot **link;
    FlatMemStreamSlot *slot = NULL;
    long end;
    char *data = NULL;
    size_t n;
    int rc = 0;
    if (!stream) return -1;

    registry_lock();
    for (link = &g_ms; *link; link = &(*link)->next) {
        if ((*link)->fp == stream) {
            slot = *link;
            *link = slot->next;
            break;
        }
    }
    registry_unlock();
    if (!slot) return fclose(stream);

    if (fseek(stream, 0, SEEK_END) != 0) { rc = -1; goto done; }
    end = ftell(stream);
    if (end < 0) { rc = -1; goto done; }
    n = (size_t)end;
    if (fseek(stream, 0, SEEK_SET) != 0) { rc = -1; goto done; }
    if (n > 0) {
        data = (char *)malloc(n);
        if (!data || fread(data, 1, n, stream) != n) { rc = -1; goto done; }
    }
    *slot->out_buf = data;
    *slot->out_len = n;
    data = NULL;

done:
    free(data);
    if (fclose(stream) != 0) rc = -1;
    free(slot);
    return rc;
}

#endif
