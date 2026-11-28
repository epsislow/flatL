#include "flat_ws_event_client.h"
#include "flat_runtime.h"
#include "flat_runtime_socket_event.h"
#include "flat_value.h"

#include <emscripten.h>
#include <emscripten/emscripten.h>
#include <stdint.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct FlatWsEventClient {
    FlatRuntime *runtime;
    uint64_t socket_id;
    size_t max_frame_bytes;
    int stopped;
};

static void browser_error(char *dst, size_t cap, const char *message) {
    if (dst && cap) snprintf(dst, cap, "%s", message ? message : "WebSocket browser backend error");
}

EM_JS(int, flat_wasm_ws_supported_js, (), {
    return (typeof WebSocket !== 'undefined' && typeof TextEncoder !== 'undefined' &&
            typeof TextDecoder !== 'undefined') ? 1 : 0;
});

EM_JS(int, flat_wasm_ws_start_js,
      (uintptr_t runtime_ptr, double socket_id, const char *url_ptr,
       int connect_timeout_ms, double max_frame_bytes), {
    if (typeof WebSocket === 'undefined') return -1;
    var url = UTF8ToString(url_ptr);
    var key = String(runtime_ptr) + ':' + String(socket_id);
    var ws;
    try {
        ws = new WebSocket(url);
        ws.binaryType = 'arraybuffer';
    } catch (e) {
        return -1;
    }
    var entry = { ws: ws, runtime: runtime_ptr, id: socket_id,
                  maxFrame: max_frame_bytes, terminal: false, timer: 0 };
    if (!Module.__flatWsSockets) Module.__flatWsSockets = Object.create(null);
    Module.__flatWsSockets[key] = entry;
    function emit(kind, opcode, bytes) {
        if (entry.terminal && kind !== 3) return;
        var ptr = 0;
        var len = bytes ? bytes.length : 0;
        if (len) {
            ptr = _malloc(len);
            if (!ptr) {
                _flat_wasm_ws_event(runtime_ptr, socket_id, 4, 0, 0, 0);
                try { ws.close(1011, 'out of memory'); } catch (_) {}
                return;
            }
            HEAPU8.set(bytes, ptr);
        }
        var publishRc = _flat_wasm_ws_event(runtime_ptr, socket_id, kind, opcode, ptr, len);
        if (ptr) _free(ptr);
        if (kind === 2 && publishRc < 0 && !entry.terminal) {
            entry.terminal = true;
            try { ws.close(1013, 'runtime event queue full'); } catch (_) {}
        }
    }
    entry.emit = emit;
    function clearTimer() {
        if (entry.timer) { clearTimeout(entry.timer); entry.timer = 0; }
    }
    entry.cleanup = function () {
        clearTimer();
        ws.onopen = ws.onmessage = ws.onerror = ws.onclose = null;
        delete Module.__flatWsSockets[key];
    };
    ws.onopen = function () {
        clearTimer();
        emit(1, 0, null);
    };
    ws.onmessage = function (event) {
        var bytes;
        var opcode;
        if (typeof event.data === 'string') {
            bytes = new TextEncoder().encode(event.data);
            opcode = 1;
        } else if (event.data instanceof ArrayBuffer) {
            bytes = new Uint8Array(event.data);
            opcode = 2;
        } else if (ArrayBuffer.isView(event.data)) {
            bytes = new Uint8Array(event.data.buffer, event.data.byteOffset, event.data.byteLength);
            opcode = 2;
        } else {
            emit(4, 0, new TextEncoder().encode('unsupported WebSocket message type'));
            return;
        }
        if (bytes.length > entry.maxFrame) {
            emit(4, 0, new TextEncoder().encode('WebSocket frame exceeds maxFrameBytes'));
            try { ws.close(1009, 'frame too large'); } catch (_) {}
            return;
        }
        emit(2, opcode, bytes);
    };
    ws.onerror = function () {
        emit(4, 0, new TextEncoder().encode('WebSocket browser transport error'));
    };
    ws.onclose = function () {
        if (!entry.terminal) {
            entry.terminal = true;
            emit(3, 0, null);
        }
        entry.cleanup();
    };
    if (connect_timeout_ms > 0) {
        entry.timer = setTimeout(function () {
            if (ws.readyState === WebSocket.CONNECTING) {
                emit(4, 0, new TextEncoder().encode('WebSocket connection timeout'));
                try { ws.close(); } catch (_) {}
            }
        }, connect_timeout_ms);
    }
    return 0;
});

EM_JS(int, flat_wasm_ws_send_js,
      (uintptr_t runtime_ptr, double socket_id, int opcode, const unsigned char *data, size_t length), {
    var key = String(runtime_ptr) + ':' + String(socket_id);
    var entry = Module.__flatWsSockets && Module.__flatWsSockets[key];
    if (!entry || entry.terminal || entry.ws.readyState !== WebSocket.OPEN) return -1;
    try {
        var bytes = HEAPU8.slice(data, data + length);
        if (opcode === 1) {
            entry.ws.send(new TextDecoder().decode(bytes));
        } else if (opcode === 2) {
            entry.ws.send(bytes.buffer);
        } else {
            return -1;
        }
        return 0;
    } catch (e) {
        return -1;
    }
});

EM_JS(int, flat_wasm_ws_close_js, (uintptr_t runtime_ptr, double socket_id), {
    var key = String(runtime_ptr) + ':' + String(socket_id);
    var entry = Module.__flatWsSockets && Module.__flatWsSockets[key];
    if (!entry) return -1;
    if (!entry.terminal) {
        entry.terminal = true;
        entry.emit(3, 0, null);
    }
    entry.cleanup();
    try {
        if (entry.ws.readyState === WebSocket.OPEN || entry.ws.readyState === WebSocket.CONNECTING)
            entry.ws.close(1000, '');
    } catch (e) {}
    return 0;
});

int flat_ws_supported(void) {
    return flat_wasm_ws_supported_js();
}

static int publish_event(FlatRuntime *runtime, uint64_t socket_id, int event,
                         int opcode, const unsigned char *data, size_t length) {
    FlatCollectionPool *coll = flat_coll_create();
    FlatStringPool *pool = flat_pool_create();
    FlatValue payload, values[2];
    char keys[2][FLAT_OBJ_KEY_MAX + 1] = {{0}};
    uint32_t data_id = 0, object_id = 0;
    int rc = -1;
    memset(&payload, 0, sizeof payload);
    memset(values, 0, sizeof values);
    if (!coll || !pool) goto done;
    if (event == FLAT_SOCKET_EVENT_OPEN || event == FLAT_SOCKET_EVENT_CLOSE) {
        if (flat_value_set_null(&payload) != 0) goto done;
    } else if (event == FLAT_SOCKET_EVENT_ERROR) {
        if (flat_pool_alloc_copy(pool, data, length, &data_id) != 0 ||
            flat_value_set_str_ref(&payload, data_id) != 0) goto done;
        flat_value_retain(coll, pool, &payload);
    } else if (event == FLAT_SOCKET_EVENT_RECEIVE) {
        memcpy(keys[0], "opcode", sizeof "opcode");
        memcpy(keys[1], "data", sizeof "data");
        if (flat_value_set_number(&values[0], (double)opcode) != 0 ||
            flat_pool_alloc_copy(pool, data, length, &data_id) != 0 ||
            flat_value_set_str_ref(&values[1], data_id) != 0) goto done;
        flat_value_retain(coll, pool, &values[1]);
        if (flat_obj_build(coll, pool, (const char (*)[FLAT_OBJ_KEY_MAX + 1])keys,
                           values, 2, &object_id) != 0 ||
            flat_value_set_object(&payload, object_id) != 0) goto done;
        flat_value_retain(coll, pool, &payload);
    } else goto done;
    rc = flat_runtime_socket_event_publish(runtime, socket_id,
            (FlatSocketEvent)event, &payload, coll, pool, NULL);
done:
    flat_value_release(coll, pool, &payload);
    flat_value_release(coll, pool, &values[1]);
    if (coll) flat_coll_destroy(coll);
    if (pool) flat_pool_destroy(pool);
    return rc;
}

EMSCRIPTEN_KEEPALIVE
int flat_wasm_ws_event(uintptr_t runtime_ptr, uint64_t socket_id, int event,
                       int opcode, const unsigned char *data, size_t length) {
    FlatRuntime *runtime = (FlatRuntime *)runtime_ptr;
    int rc;
    if (!runtime || !socket_id || (length && !data)) return -1;
    rc = publish_event(runtime, socket_id, event, opcode, data, length);
    if (event == FLAT_SOCKET_EVENT_RECEIVE && rc < 0) {
        static const char message[] = "WebSocket receive event publication failed or runtime queue is full";
        (void)publish_event(runtime, socket_id, FLAT_SOCKET_EVENT_ERROR, 0,
                            (const unsigned char *)message, sizeof message - 1);
    }
    return rc;
}

int flat_ws_event_client_start(FlatRuntime *runtime, uint64_t socket_id,
                               const char *url, long connect_timeout_ms,
                               size_t max_frame_bytes,
                               FlatWsEventClient **out_client,
                               char *error, size_t error_capacity) {
    FlatWsEventClient *client;
    int rc;
    if (out_client) *out_client = NULL;
    if (!runtime || !socket_id || !url || !out_client || connect_timeout_ms <= 0 ||
        max_frame_bytes == 0 || !flat_ws_supported()) {
        browser_error(error, error_capacity, "WebSocket browser API unavailable or invalid arguments");
        return -1;
    }
    if (strncmp(url, "ws://", 5) != 0 && strncmp(url, "wss://", 6) != 0) {
        browser_error(error, error_capacity, "WebSocket URL must use ws:// or wss://");
        return -1;
    }
    client = (FlatWsEventClient *)calloc(1, sizeof *client);
    if (!client) { browser_error(error, error_capacity, "out of memory"); return -1; }
    client->runtime = runtime;
    client->socket_id = socket_id;
    client->max_frame_bytes = max_frame_bytes;
    rc = flat_wasm_ws_start_js((uintptr_t)runtime, (double)socket_id, url,
                               (int)(connect_timeout_ms > INT32_MAX ? INT32_MAX : connect_timeout_ms),
                               (double)max_frame_bytes);
    if (rc != 0) {
        free(client);
        browser_error(error, error_capacity, "unable to create browser WebSocket");
        return -1;
    }
    *out_client = client;
    if (error && error_capacity) error[0] = '\0';
    return 0;
}

int flat_ws_event_client_send(FlatWsEventClient *client, FlatWsOpcode opcode,
                              const void *data, size_t length, long timeout_ms,
                              char *error, size_t error_capacity) {
    int rc;
    (void)timeout_ms;
    if (!client || client->stopped || (!data && length) ||
        (opcode != FLAT_WS_TEXT && opcode != FLAT_WS_BINARY)) {
        browser_error(error, error_capacity, "invalid browser WebSocket send arguments");
        return -1;
    }
    rc = flat_wasm_ws_send_js((uintptr_t)client->runtime, (double)client->socket_id,
                              (int)opcode, (const unsigned char *)data, length);
    if (rc != 0) browser_error(error, error_capacity, "browser WebSocket is not open or send failed");
    else if (error && error_capacity) error[0] = '\0';
    return rc;
}

void flat_ws_event_client_stop(FlatWsEventClient *client) {
    if (!client) return;
    if (!client->stopped) {
        client->stopped = 1;
        (void)flat_wasm_ws_close_js((uintptr_t)client->runtime, (double)client->socket_id);
    }
    free(client);
}
