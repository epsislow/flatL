#include "flat_wasm_io.h"
#include "flat_value.h"

#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void wasm_fprint_to_stdout(void (*fn)(FILE *, const FlatValue *, const FlatStringPool *,
                                             const FlatCollectionPool *),
                                  const FlatValue *v, const FlatStringPool *pool,
                                  const FlatCollectionPool *coll) {
    char *ptr = NULL;
    size_t len = 0;
    FILE *f = open_memstream(&ptr, &len);
    if (!f) {
        return;
    }
    fn(f, v, pool, coll);
    fclose(f);
    if (len > 0 && ptr) {
        flat_wasm_io_write_stdout(ptr, len);
    }
    free(ptr);
}

void flat_wasm_io_fprint_io(const FlatValue *v, const FlatStringPool *pool,
                            const FlatCollectionPool *coll) {
    wasm_fprint_to_stdout(flat_value_fprint_io, v, pool, coll);
}

void flat_wasm_io_fprint_io_ln(const FlatValue *v, const FlatStringPool *pool,
                               const FlatCollectionPool *coll) {
    wasm_fprint_to_stdout(flat_value_fprint_io_ln, v, pool, coll);
}

EM_JS(void, flat_wasm_js_stdout, (const char *ptr, int len), {
  if (len <= 0) return;
  if (typeof Module === 'undefined' || !Module['flatWasmOnStdout']) return;
  var heap = Module['HEAPU8'] || HEAPU8;
  var start = ptr;
  var end = ptr + len;
  if (start < 0) return;
  if (end > heap.length) end = heap.length;
  if (start >= end) return;
  Module['flatWasmOnStdout'](heap.subarray(start, end));
});

EM_JS(void, flat_wasm_js_start_read_line, (char *buf, int buf_size, int allow_empty), {
  Module._flatWasmIoPending = { done: false, status: 0 };
  var cb = (typeof Module !== 'undefined' && Module['flatWasmOnNeedLine'])
    ? Module['flatWasmOnNeedLine']
    : null;
  if (!cb) {
    stringToUTF8("", buf, buf_size);
    Module._flatWasmIoPending.done = true;
    Module._flatWasmIoPending.status = 2;
    return;
  }
  cb(allow_empty, function(line, status) {
    stringToUTF8(line || "", buf, buf_size);
    Module._flatWasmIoPending.status = status | 0;
    Module._flatWasmIoPending.done = true;
  });
});

EM_JS(int, flat_wasm_js_io_poll_done, (), {
  return (Module._flatWasmIoPending && Module._flatWasmIoPending.done) ? 1 : 0;
});

EM_JS(int, flat_wasm_js_io_status, (), {
  return Module._flatWasmIoPending ? (Module._flatWasmIoPending.status | 0) : 2;
});

EM_JS(void, flat_wasm_js_start_read_char, (char *ch_out), {
  Module._flatWasmIoPending = { done: false, status: 0 };
  var cb = (typeof Module !== 'undefined' && Module['flatWasmOnNeedChar'])
    ? Module['flatWasmOnNeedChar']
    : null;
  if (!cb) {
    setValue(ch_out, 0, 'i8');
    Module._flatWasmIoPending.done = true;
    Module._flatWasmIoPending.status = 2;
    return;
  }
  cb(function(ch, status) {
    setValue(ch_out, ch | 0, 'i8');
    Module._flatWasmIoPending.status = status | 0;
    Module._flatWasmIoPending.done = true;
  });
});

void flat_wasm_io_write_stdout(const char *s, size_t n) {
    if (!s || n == 0) {
        return;
    }
    flat_wasm_js_stdout(s, (int)n);
}

int flat_wasm_io_read_line(char *buf, size_t buf_size, int allow_empty, FlatIoStatus *st) {
    int status;

    if (!buf || buf_size == 0 || !st) {
        return -1;
    }
    flat_wasm_js_start_read_line(buf, (int)buf_size, allow_empty);
    while (!flat_wasm_js_io_poll_done()) {
        emscripten_sleep(1);
    }
    status = flat_wasm_js_io_status();
    *st = (FlatIoStatus)status;
    return 0;
}

int flat_wasm_io_read_char(char *ch_out, FlatIoStatus *st) {
    int status;

    if (!ch_out || !st) {
        return -1;
    }
    flat_wasm_js_start_read_char(ch_out);
    while (!flat_wasm_js_io_poll_done()) {
        emscripten_sleep(1);
    }
    status = flat_wasm_js_io_status();
    *st = (FlatIoStatus)status;
    return 0;
}
