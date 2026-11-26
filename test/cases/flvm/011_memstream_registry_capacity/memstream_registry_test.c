#include "flat_memstream.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STREAM_COUNT 40

int main(void) {
    FILE *streams[STREAM_COUNT] = {0};
    char *buffers[STREAM_COUNT] = {0};
    size_t lengths[STREAM_COUNT] = {0};
    int i;
    char text[32];

    for (i = 0; i < STREAM_COUNT; ++i) {
        streams[i] = flat_open_memstream(&buffers[i], &lengths[i]);
        if (!streams[i]) return 1;
        snprintf(text, sizeof text, "stream-%02d", i);
        if (fwrite(text, 1, strlen(text), streams[i]) != strlen(text)) return 2;
    }
    for (i = 0; i < STREAM_COUNT; ++i) {
        snprintf(text, sizeof text, "stream-%02d", i);
        if (flat_close_memstream(streams[i]) != 0) return 3;
        if (!buffers[i] || lengths[i] != strlen(text) || memcmp(buffers[i], text, lengths[i]) != 0)
            return 4;
        free(buffers[i]);
    }
    puts("memstream-registry-capacity-ok");
    return 0;
}
