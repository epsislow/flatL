#include "flat_ws_transport.h"

#include <curl/curl.h>
#include <curl/websockets.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <poll.h>
#include <errno.h>
#endif

#if LIBCURL_VERSION_NUM < 0x075600
#error "FlatL WebSocket transport requires libcurl 7.86.0 or newer"
#endif

struct FlatWsConnection {
    CURL *easy;
    curl_socket_t socket_fd;
    pthread_mutex_t mutex;
    int closed;
};

static atomic_flag g_ws_init_lock = ATOMIC_FLAG_INIT;
static atomic_int g_ws_init_state = ATOMIC_VAR_INIT(0);

static void set_error(char *dst, size_t cap, const char *message) {
    if (dst && cap) snprintf(dst, cap, "%s", message ? message : "WebSocket transport error");
}

int flat_ws_prepare(void) {
    int state = atomic_load_explicit(&g_ws_init_state, memory_order_acquire);
    if (state) return state > 0 ? 0 : -1;
    while (atomic_flag_test_and_set_explicit(&g_ws_init_lock, memory_order_acquire)) { }
    state = atomic_load_explicit(&g_ws_init_state, memory_order_relaxed);
    if (!state) {
        state = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK ? 1 : -1;
        atomic_store_explicit(&g_ws_init_state, state, memory_order_release);
    }
    atomic_flag_clear_explicit(&g_ws_init_lock, memory_order_release);
    return state > 0 ? 0 : -1;
}

int flat_ws_supported(void) {
    const curl_version_info_data *info;
    const char * const *protocol;
    int have_ws = 0, have_wss = 0;
    if (flat_ws_prepare() != 0) return 0;
    info = curl_version_info(CURLVERSION_NOW);
    if (!info || !info->protocols) return 0;
    for (protocol = info->protocols; *protocol; ++protocol) {
        if (strcmp(*protocol, "ws") == 0) have_ws = 1;
        else if (strcmp(*protocol, "wss") == 0) have_wss = 1;
    }
    return have_ws && have_wss;
}

#define FLAT_WS_WAIT_READ 1
#define FLAT_WS_WAIT_WRITE 2

static int get_socket(CURL *easy, curl_socket_t *out);

static int wait_socket(CURL *easy, short events, long timeout_ms) {
    curl_socket_t fd = CURL_SOCKET_BAD;
    if (!easy || timeout_ms < 0 || get_socket(easy, &fd) != 0) return -1;
#ifdef _WIN32
    {
        fd_set read_set, write_set, error_set;
        struct timeval tv;
        int rc;
        FD_ZERO(&read_set); FD_ZERO(&write_set); FD_ZERO(&error_set);
        if (events & FLAT_WS_WAIT_WRITE) FD_SET(fd, &write_set); else FD_SET(fd, &read_set);
        FD_SET(fd, &error_set);
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        rc = select(0, &read_set, &write_set, &error_set, &tv);
        if (rc <= 0) return rc;
        return FD_ISSET(fd, &error_set) ? -1 : 1;
    }
#else
    {
        struct pollfd pfd;
        int rc;
        pfd.fd = (int)fd;
        pfd.events = (events & FLAT_WS_WAIT_WRITE) ? POLLOUT : POLLIN;
        pfd.revents = 0;
        do { rc = poll(&pfd, 1, (int)timeout_ms); } while (rc < 0 && errno == EINTR);
        if (rc <= 0) return rc;
        return (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) ? -1 : 1;
    }
#endif
}

static int get_socket(CURL *easy, curl_socket_t *out) {
    *out = CURL_SOCKET_BAD;
    return curl_easy_getinfo(easy, CURLINFO_ACTIVESOCKET, out) == CURLE_OK &&
           *out != CURL_SOCKET_BAD ? 0 : -1;
}

int flat_ws_connect(const char *url, long timeout_ms,
                    FlatWsConnection **out_connection,
                    char *error, size_t error_capacity) {
    FlatWsConnection *connection;
    CURLcode rc;
    if (out_connection) *out_connection = NULL;
    if (!url || !out_connection || timeout_ms <= 0 ||
        !(strncmp(url, "ws://", 5) == 0 || strncmp(url, "wss://", 6) == 0)) {
        set_error(error, error_capacity, "invalid WebSocket URL or arguments");
        return -1;
    }
    if (flat_ws_prepare() != 0) {
        set_error(error, error_capacity, "libcurl initialization failed");
        return -1;
    }
    if (!flat_ws_supported()) {
        set_error(error, error_capacity, "linked libcurl build does not enable ws/wss protocols");
        return -1;
    }
    connection = (FlatWsConnection *)calloc(1, sizeof *connection);
    if (!connection) { set_error(error, error_capacity, "out of memory"); return -1; }
    connection->socket_fd = CURL_SOCKET_BAD;
    if (pthread_mutex_init(&connection->mutex, NULL) != 0) {
        free(connection); set_error(error, error_capacity, "mutex initialization failed"); return -1;
    }
    connection->easy = curl_easy_init();
    if (!connection->easy) {
        pthread_mutex_destroy(&connection->mutex); free(connection);
        set_error(error, error_capacity, "libcurl handle allocation failed"); return -1;
    }
    curl_easy_setopt(connection->easy, CURLOPT_URL, url);
    curl_easy_setopt(connection->easy, CURLOPT_CONNECT_ONLY, 2L);
    curl_easy_setopt(connection->easy, CURLOPT_CONNECTTIMEOUT_MS, timeout_ms);
    curl_easy_setopt(connection->easy, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(connection->easy, CURLOPT_NOSIGNAL, 1L);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(connection->easy, CURLOPT_PROTOCOLS_STR, "ws,wss");
#endif
    rc = curl_easy_perform(connection->easy);
    if (rc != CURLE_OK || get_socket(connection->easy, &connection->socket_fd) != 0) {
        set_error(error, error_capacity, rc != CURLE_OK ? curl_easy_strerror(rc) : "WebSocket socket unavailable");
        curl_easy_cleanup(connection->easy);
        pthread_mutex_destroy(&connection->mutex);
        free(connection);
        return -1;
    }
    *out_connection = connection;
    if (error && error_capacity) error[0] = '\0';
    return 0;
}

static unsigned int curl_opcode(FlatWsOpcode opcode) {
    switch (opcode) {
        case FLAT_WS_TEXT: return CURLWS_TEXT;
        case FLAT_WS_BINARY: return CURLWS_BINARY;
        case FLAT_WS_CLOSE: return CURLWS_CLOSE;
        case FLAT_WS_PING: return CURLWS_PING;
        case FLAT_WS_PONG: return CURLWS_PONG;
        default: return 0;
    }
}

int flat_ws_send(FlatWsConnection *connection, FlatWsOpcode opcode,
                 const void *data, size_t length,
                 long timeout_ms, char *error, size_t error_capacity) {
    CURLcode rc;
    size_t sent = 0;
    unsigned int flags = curl_opcode(opcode);
    int ready;
    if (!connection || (!data && length) || timeout_ms <= 0 || !flags) {
        set_error(error, error_capacity, "invalid WebSocket send arguments"); return -1;
    }
    pthread_mutex_lock(&connection->mutex);
    if (connection->closed) {
        pthread_mutex_unlock(&connection->mutex); set_error(error, error_capacity, "WebSocket is closed"); return -1;
    }
    {
        size_t offset = 0;
        static const unsigned char empty_payload = 0;
        const unsigned char *bytes = data ? (const unsigned char *)data : &empty_payload;
        do {
            size_t chunk_sent = 0;
            unsigned int send_flags = flags | (offset ? CURLWS_OFFSET : 0);
            rc = curl_ws_send(connection->easy, bytes + offset,
                              length - offset, &chunk_sent, (curl_off_t)length, send_flags);
            if (rc == CURLE_AGAIN) {
                ready = wait_socket(connection->easy, FLAT_WS_WAIT_WRITE, timeout_ms);
                if (ready > 0) continue;
                break;
            }
            if (rc != CURLE_OK) break;
            if (chunk_sent == 0 && length != 0) break;
            offset += chunk_sent;
            if (length == 0) break;
        } while (offset < length);
        sent = offset;
    }
    pthread_mutex_unlock(&connection->mutex);
    if (rc != CURLE_OK || sent != length) {
        set_error(error, error_capacity, rc == CURLE_OK ? "incomplete WebSocket frame send" : curl_easy_strerror(rc));
        return -1;
    }
    if (error && error_capacity) error[0] = '\0';
    return 0;
}

static FlatWsOpcode flat_opcode(int flags) {
    if (flags & CURLWS_TEXT) return FLAT_WS_TEXT;
    if (flags & CURLWS_BINARY) return FLAT_WS_BINARY;
    if (flags & CURLWS_CONT) return FLAT_WS_CONTINUATION;
    if (flags & CURLWS_CLOSE) return FLAT_WS_CLOSE;
    if (flags & CURLWS_PING) return FLAT_WS_PING;
    if (flags & CURLWS_PONG) return FLAT_WS_PONG;
    return 0;
}

int flat_ws_receive(FlatWsConnection *connection, long timeout_ms,
                    size_t max_frame_bytes, FlatWsMessage *out_message,
                    char *error, size_t error_capacity) {
    unsigned char chunk[8192];
    unsigned char *buffer = NULL;
    size_t used = 0, capacity = 0;
    int have_meta = 0, ready;
    FlatWsOpcode opcode = 0;
    CURLcode rc = CURLE_OK;
    if (out_message) memset(out_message, 0, sizeof *out_message);
    if (!connection || !out_message || timeout_ms <= 0 || max_frame_bytes == 0) {
        set_error(error, error_capacity, "invalid WebSocket receive arguments"); return -1;
    }
    pthread_mutex_lock(&connection->mutex);
    if (connection->closed) {
        pthread_mutex_unlock(&connection->mutex); set_error(error, error_capacity, "WebSocket is closed"); return -1;
    }
    for (;;) {
        size_t received = 0;
        const struct curl_ws_frame *meta = NULL;
        rc = curl_ws_recv(connection->easy, chunk, sizeof chunk, &received, &meta);
        if (rc == CURLE_AGAIN) {
            ready = wait_socket(connection->easy, FLAT_WS_WAIT_READ, timeout_ms);
            if (ready == 0) { pthread_mutex_unlock(&connection->mutex); free(buffer); return 1; }
            if (ready < 0) { rc = CURLE_RECV_ERROR; break; }
            continue;
        }
        if (rc != CURLE_OK || !meta) break;
        if (!have_meta) { opcode = flat_opcode(meta->flags); have_meta = 1; }
        if (received > max_frame_bytes - used) { rc = CURLE_FILESIZE_EXCEEDED; break; }
        if (used + received > capacity) {
            size_t next = capacity ? capacity : 8192;
            unsigned char *grown;
            while (next < used + received && next < max_frame_bytes) next = next > max_frame_bytes / 2 ? max_frame_bytes : next * 2;
            grown = (unsigned char *)realloc(buffer, next);
            if (!grown) { rc = CURLE_OUT_OF_MEMORY; break; }
            buffer = grown; capacity = next;
        }
        if (received) memcpy(buffer + used, chunk, received);
        used += received;
        if (meta->bytesleft == 0) break;
    }
    pthread_mutex_unlock(&connection->mutex);
    if (rc != CURLE_OK || !have_meta) {
        free(buffer);
        set_error(error, error_capacity, rc != CURLE_OK ? curl_easy_strerror(rc) : "missing WebSocket frame metadata");
        return -1;
    }
    out_message->data = buffer;
    out_message->length = used;
    out_message->opcode = opcode;
    if (error && error_capacity) error[0] = '\0';
    return 0;
}

void flat_ws_message_free(FlatWsMessage *message) {
    if (!message) return;
    free(message->data);
    memset(message, 0, sizeof *message);
}

void flat_ws_close(FlatWsConnection *connection, long timeout_ms) {
    if (!connection) return;
    pthread_mutex_lock(&connection->mutex);
    if (!connection->closed) {
        const unsigned char close_payload[2] = {0x03, 0xE8};
        size_t sent = 0;
        (void)curl_ws_send(connection->easy, close_payload, sizeof close_payload,
                           &sent, (curl_off_t)sizeof close_payload, CURLWS_CLOSE);
        connection->closed = 1;
    }
    pthread_mutex_unlock(&connection->mutex);
    (void)timeout_ms;
    curl_easy_cleanup(connection->easy);
    pthread_mutex_destroy(&connection->mutex);
    free(connection);
}
