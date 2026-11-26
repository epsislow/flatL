#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t read_u32_le(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_u32_le(unsigned char *p, uint32_t value) {
    p[0] = (unsigned char)value;
    p[1] = (unsigned char)(value >> 8);
    p[2] = (unsigned char)(value >> 16);
    p[3] = (unsigned char)(value >> 24);
}

int main(int argc, char **argv) {
    FILE *file;
    unsigned char *bytes;
    long file_size;
    uint32_t jbmx_offset;
    size_t size;

    if (argc != 2) return 2;
    file = fopen(argv[1], "rb");
    if (!file) return 3;
    if (fseek(file, 0, SEEK_END) != 0 || (file_size = ftell(file)) < 36 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 4;
    }
    size = (size_t)file_size;
    bytes = (unsigned char *)malloc(size + 1);
    if (!bytes) {
        fclose(file);
        return 5;
    }
    if (fread(bytes, 1, size, file) != size) {
        free(bytes);
        fclose(file);
        return 6;
    }
    fclose(file);

    /* v14+ stores jbmx_off at header bytes 32..35. Insert one byte exactly
     * between SCHM and JBMX and move the offset, so the loader must reject the
     * extra byte as SCHM trailing data (rather than blaming the final BPAR). */
    if (bytes[0] != 'F' || bytes[1] != 'L' || bytes[2] != 'A' ||
        bytes[3] != 'T' || bytes[5] < 14) {
        free(bytes);
        return 7;
    }
    jbmx_offset = read_u32_le(bytes + 32);
    if (jbmx_offset < 6 || jbmx_offset >= size || jbmx_offset == UINT32_MAX) {
        free(bytes);
        return 7;
    }
    for (size_t i = size; i > (size_t)jbmx_offset; --i)
        bytes[i] = bytes[i - 1];
    bytes[jbmx_offset] = 0xff;
    write_u32_le(bytes + 32, jbmx_offset + 1);

    file = fopen(argv[1], "wb");
    if (!file) {
        free(bytes);
        return 8;
    }
    {
        size_t written = fwrite(bytes, 1, size + 1, file);
        int close_rc = fclose(file);
        if (written != size + 1 || close_rc != 0) {
            free(bytes);
            return 9;
        }
    }
    free(bytes);
    return 0;
}
