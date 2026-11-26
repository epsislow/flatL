#include "fb_format.h"
#include "flat_runtime.h"
#include "flat_task.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>


static uint32_t read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_u32_le(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value & 0xffu);
    p[1] = (uint8_t)((value >> 8) & 0xffu);
    p[2] = (uint8_t)((value >> 16) & 0xffu);
    p[3] = (uint8_t)((value >> 24) & 0xffu);
}

static int verify_v13_compatibility(const FlatBytecode *source) {
    FlatBytecode legacy;
    uint32_t native_off;
    uint32_t jbmx_off;
    size_t bpar_start;
    size_t bpar_len;
    size_t legacy_size;
    uint8_t *data;
    int rc = -1;
    memset(&legacy, 0, sizeof legacy);
    if (!source || !source->data || source->format_version != 14 || source->size < 40)
        return -1;
    native_off = read_u32_le(source->data + 8);
    jbmx_off = read_u32_le(source->data + 32);
    bpar_start = (size_t)jbmx_off + flat_fb_jbmx_section_size(source->block_count);
    if (bpar_start >= source->size || (native_off && native_off <= bpar_start)) return -1;
    bpar_len = native_off ? (size_t)(native_off - bpar_start) : source->size - bpar_start;
    if (bpar_len < 7 || bpar_start + bpar_len > source->size) return -1;
    legacy_size = source->size - bpar_len;
    data = malloc(legacy_size);
    if (!data) return -1;
    memcpy(data, source->data, bpar_start);
    if (native_off) memcpy(data + bpar_start, source->data + native_off,
                           source->size - native_off);
    data[5] = 13;
    if (native_off) write_u32_le(data + 8, native_off - (uint32_t)bpar_len);
    if (flat_fb_load_buffer(data, legacy_size, &legacy) == 0 &&
        legacy.format_version == 13 && legacy.block_params == NULL &&
        legacy.block_count == source->block_count) rc = 0;
    flat_fb_free(&legacy);
    free(data);
    return rc;
}

static FILE *capture_begin(int *saved_stdout) {
    FILE *capture = tmpfile();
    if (!capture) return NULL;
    fflush(stdout);
    *saved_stdout = dup(STDOUT_FILENO);
    if (*saved_stdout < 0 || dup2(fileno(capture), STDOUT_FILENO) < 0) {
        if (*saved_stdout >= 0) close(*saved_stdout);
        fclose(capture);
        return NULL;
    }
    return capture;
}

static int capture_end(FILE *capture, int saved_stdout, char *out, size_t out_sz) {
    long n;
    int rc = 0;
    fflush(stdout);
    if (dup2(saved_stdout, STDOUT_FILENO) < 0) rc = -1;
    close(saved_stdout);
    if (fseek(capture, 0, SEEK_END) != 0 || (n = ftell(capture)) < 0 ||
        (size_t)n >= out_sz || fseek(capture, 0, SEEK_SET) != 0 ||
        fread(out, 1, (size_t)n, capture) != (size_t)n) rc = -1;
    else out[n] = '\0';
    fclose(capture);
    return rc;
}

int main(int argc, char **argv) {
    FlatBytecode bc;
    FlatBytecode mismatch_bc;
    FlatRuntime *runtime;
    FlatRuntime *mismatch_runtime;
    FlatTaskId parent = 0;
    FlatVmResult result;
    uint64_t turns = 0;
    int saved_stdout = -1;
    FILE *capture = NULL;
    char output[2048];
    int rc = 0;
    size_t block_index;
    int found_worker = 0;
    if (argc != 3) return 2;
    memset(&bc, 0, sizeof bc);
    memset(&mismatch_bc, 0, sizeof mismatch_bc);
    if (flat_fb_load_file(argv[1], &bc) != 0) return 3;
    if (flat_fb_load_file(argv[2], &mismatch_bc) != 0) { flat_fb_free(&bc); return 4; }
    if (!bc.block_params || bc.format_version != 14) {
        flat_fb_free(&mismatch_bc); flat_fb_free(&bc); return 5;
    }
    for (block_index = 0; block_index < bc.block_count; block_index++) {
        if (bc.block_labels && strcmp(bc.block_labels[block_index], "#worker") == 0) {
            const FlatFbBlockParams *params = &bc.block_params[block_index];
            found_worker = params->param_count == 4 &&
                strcmp(params->params[0], "price") == 0 &&
                strcmp(params->params[1], "label") == 0 &&
                strcmp(params->params[2], "values") == 0 &&
                strcmp(params->params[3], "metadata") == 0;
            break;
        }
    }
    if (!found_worker) { flat_fb_free(&mismatch_bc); flat_fb_free(&bc); return 19; }
    if (verify_v13_compatibility(&bc) != 0) {
        flat_fb_free(&mismatch_bc); flat_fb_free(&bc); return 20;
    }
    runtime = flat_runtime_create(NULL);
    if (!runtime) { flat_fb_free(&mismatch_bc); flat_fb_free(&bc); return 6; }
    capture = capture_begin(&saved_stdout);
    if (!capture) { flat_runtime_destroy(runtime); flat_fb_free(&mismatch_bc); flat_fb_free(&bc); return 7; }
    if (flat_runtime_task_create(runtime, &bc, NULL, &parent) != 0 ||
        flat_runtime_task_start(runtime, parent) != 0 ||
        flat_runtime_schedule(runtime, 2, 0, &turns) != 0 || turns == 0) rc = 7;
    if (!rc && flat_runtime_task_state(runtime, parent) != FLAT_TASK_COMPLETED) rc = 8;
    if (!rc && flat_runtime_task_state(runtime, parent + 1) != FLAT_TASK_COMPLETED) rc = 9;
    if (!rc && (flat_runtime_task_result(runtime, parent + 1, &result) != 0 ||
                result.termination != FLAT_TERM_OK)) rc = 10;
    if (capture_end(capture, saved_stdout, output, sizeof output) != 0) rc = 11;
    if (!rc && (!strstr(output, "12\nhello, task\n") || !strstr(output, "99,5,7") ||
                !strstr(output, "kind") || !strstr(output, "demo") ||
                !strstr(output, "count\":9") || !strstr(output, "count\":3") ||
                !strstr(output, "nested") || !strstr(output, "5\n3,5,7"))) {
        fprintf(stderr, "unexpected task argument output:\n%s\n", output);
        rc = 12;
    }
    if (!rc && (flat_runtime_task_destroy(runtime, parent + 1) != 0 ||
                flat_runtime_task_destroy(runtime, parent) != 0)) rc = 13;
    if (flat_runtime_destroy(runtime) != 0 && rc == 0) rc = 14;
    mismatch_runtime = flat_runtime_create(NULL);
    if (!mismatch_runtime) rc = 15;
    if (!rc && (flat_runtime_task_create(mismatch_runtime, &mismatch_bc, NULL, &parent) != 0 ||
                flat_runtime_task_start(mismatch_runtime, parent) != 0 ||
                flat_runtime_schedule(mismatch_runtime, 2, 0, &turns) != 0 ||
                flat_runtime_task_state(mismatch_runtime, parent) != FLAT_TASK_FAILED ||
                flat_runtime_task_result(mismatch_runtime, parent, &result) != 0 ||
                result.termination != FLAT_TERM_PANIC ||
                !strstr(result.diagnostic, "task.spawn.args"))) rc = 16;
    if (mismatch_runtime) {
        if (!rc && flat_runtime_task_destroy(mismatch_runtime, parent) != 0) rc = 17;
        if (flat_runtime_destroy(mismatch_runtime) != 0 && rc == 0) rc = 18;
    }
    flat_fb_free(&mismatch_bc);
    flat_fb_free(&bc);
    if (rc) {
        fprintf(stderr, "task spawn arguments failed: %d\n", rc);
        return rc;
    }
    puts("task-spawn-arguments-ok");
    return 0;
}
