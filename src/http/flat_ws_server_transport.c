#include "flat_ws_server_transport.h"
#include "flat_hash.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET FlatWsPeerSocket;
struct FlatWsServerListener { SOCKET fd; int winsock_started; };
static void close_fd(SOCKET fd) { if (fd != INVALID_SOCKET) closesocket(fd); }
#else
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int FlatWsPeerSocket;
struct FlatWsServerListener { int fd; };
static void close_fd(int fd) { if (fd >= 0) close(fd); }
#endif

static void ws_server_error(char *dst, unsigned long cap, const char *message) {
    if (dst && cap) snprintf(dst, (size_t)cap, "%s", message ? message : "WebSocket listener error");
}

int flat_ws_server_listener_open(const char *host, uint16_t port, int backlog,
                                 FlatWsServerListener **out_listener,
                                 uint16_t *out_bound_port,
                                 char *error, unsigned long error_capacity) {
    struct addrinfo hints, *items = NULL, *it;
    char service[16];
    FlatWsServerListener *listener = NULL;
    int gai, one = 1;
    intptr_t fd = -1;
    if (out_listener) *out_listener = NULL;
    if (out_bound_port) *out_bound_port = 0;
    if (!host || !*host || !out_listener || !out_bound_port || backlog < 1) {
        ws_server_error(error, error_capacity, "invalid WebSocket listener arguments");
        return -1;
    }
#ifdef _WIN32
    {
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            ws_server_error(error, error_capacity, "Winsock initialization failed");
            return -1;
        }
    }
#endif
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    snprintf(service, sizeof service, "%u", (unsigned int)port);
    gai = getaddrinfo(host, service, &hints, &items);
    if (gai != 0) {
        ws_server_error(error, error_capacity, "unable to resolve listener address");
        goto fail;
    }
    for (it = items; it; it = it->ai_next) {
#ifdef _WIN32
        SOCKET candidate = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (candidate == INVALID_SOCKET) continue;
        fd = (intptr_t)candidate;
        setsockopt(candidate, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof one);
        if (bind(candidate, it->ai_addr, (int)it->ai_addrlen) == 0 && listen(candidate, backlog) == 0) break;
        closesocket(candidate); fd = -1;
#else
        int candidate = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (candidate < 0) continue;
        (void)setsockopt(candidate, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#ifdef SO_NOSIGPIPE
        (void)setsockopt(candidate, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
        if (bind(candidate, it->ai_addr, it->ai_addrlen) == 0 && listen(candidate, backlog) == 0) { fd = (intptr_t)candidate; break; }
        close(candidate);
#endif
    }
    freeaddrinfo(items); items = NULL;
    if (fd == -1) {
        ws_server_error(error, error_capacity, "unable to bind/listen on requested address");
        goto fail;
    }
    listener = (FlatWsServerListener *)calloc(1, sizeof *listener);
    if (!listener) {
#ifdef _WIN32
        close_fd((SOCKET)fd);
#else
        close_fd((int)fd);
#endif
        ws_server_error(error, error_capacity, "out of memory");
        goto fail;
    }
#ifdef _WIN32
    listener->fd = (SOCKET)fd;
    listener->winsock_started = 1;
#else
    listener->fd = (int)fd;
#endif
    {
        struct sockaddr_storage address;
#ifdef _WIN32
        int address_len = (int)sizeof address;
#else
        socklen_t address_len = (socklen_t)sizeof address;
#endif
        memset(&address, 0, sizeof address);
        if (getsockname(
#ifdef _WIN32
                listener->fd,
#else
                listener->fd,
#endif
                (struct sockaddr *)&address, &address_len) != 0) {
            flat_ws_server_listener_close(listener);
            ws_server_error(error, error_capacity, "unable to read bound listener port");
            return -1;
        }
        if (address.ss_family == AF_INET) *out_bound_port = ntohs(((struct sockaddr_in *)&address)->sin_port);
        else if (address.ss_family == AF_INET6) *out_bound_port = ntohs(((struct sockaddr_in6 *)&address)->sin6_port);
        else {
            flat_ws_server_listener_close(listener);
            ws_server_error(error, error_capacity, "unsupported listener address family");
            return -1;
        }
    }
    *out_listener = listener;
    if (error && error_capacity) error[0] = '\0';
    return 0;
fail:
    if (items) freeaddrinfo(items);
#ifdef _WIN32
    WSACleanup();
#endif
    return -1;
}

int flat_ws_server_listener_accept(FlatWsServerListener *listener,
                                   long timeout_ms, intptr_t *out_peer,
                                   char *error, unsigned long error_capacity) {
    if (out_peer) *out_peer = (intptr_t)-1;
    if (!listener || !out_peer || timeout_ms < -1) {
        ws_server_error(error, error_capacity, "invalid WebSocket accept arguments");
        return -1;
    }
#ifdef _WIN32
    {
        fd_set read_set;
        struct timeval tv, *tvp = NULL;
        int ready;
        SOCKET peer;
        FD_ZERO(&read_set); FD_SET(listener->fd, &read_set);
        if (timeout_ms >= 0) { tv.tv_sec = timeout_ms / 1000; tv.tv_usec = (timeout_ms % 1000) * 1000; tvp = &tv; }
        ready = select(0, &read_set, NULL, NULL, tvp);
        if (ready == 0) return 1;
        if (ready < 0) { ws_server_error(error, error_capacity, "listener wait failed"); return -1; }
        peer = accept(listener->fd, NULL, NULL);
        if (peer == INVALID_SOCKET) { ws_server_error(error, error_capacity, "accept failed"); return -1; }
        *out_peer = (intptr_t)peer;
    }
#else
    {
        struct pollfd pfd;
        int ready;
        int peer;
        pfd.fd = listener->fd; pfd.events = POLLIN; pfd.revents = 0;
        do { ready = poll(&pfd, 1, timeout_ms < 0 ? -1 : (int)timeout_ms); } while (ready < 0 && errno == EINTR);
        if (ready == 0) return 1;
        if (ready < 0 || (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
            ws_server_error(error, error_capacity, "listener wait failed"); return -1;
        }
        peer = accept(listener->fd, NULL, NULL);
        if (peer < 0) { ws_server_error(error, error_capacity, "accept failed"); return -1; }
        *out_peer = (intptr_t)peer;
    }
#endif
    if (error && error_capacity) error[0] = '\0';
    return 0;
}

void flat_ws_server_peer_shutdown(intptr_t peer) {
#ifdef _WIN32
    if ((SOCKET)peer != INVALID_SOCKET) (void)shutdown((SOCKET)peer, SD_BOTH);
#else
    if ((int)peer >= 0) (void)shutdown((int)peer, SHUT_RDWR);
#endif
}

void flat_ws_server_peer_close(intptr_t peer) {
#ifdef _WIN32
    if ((SOCKET)peer != INVALID_SOCKET) closesocket((SOCKET)peer);
#else
    if ((int)peer >= 0) close((int)peer);
#endif
}

void flat_ws_server_listener_shutdown(FlatWsServerListener *listener) {
    if (!listener) return;
#ifdef _WIN32
    if (listener->fd != INVALID_SOCKET) (void)shutdown(listener->fd, SD_BOTH);
#else
    if (listener->fd >= 0) (void)shutdown(listener->fd, SHUT_RDWR);
#endif
}

void flat_ws_server_listener_close(FlatWsServerListener *listener) {
    if (!listener) return;
#ifdef _WIN32
    close_fd(listener->fd);
    if (listener->winsock_started) WSACleanup();
#else
    close_fd(listener->fd);
#endif
    free(listener);
}


#define FLAT_WS_SERVER_HTTP_MAX 16384
static int ws_header_name_equal(const char *a, size_t n, const char *b) {
    size_t i;
    if (strlen(b) != n) return 0;
    for (i = 0; i < n; i++) if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return 0;
    return 1;
}
static int ws_header_has_token(const char *value, const char *wanted) {
    const char *p = value;
    size_t wn = strlen(wanted);
    while (*p) {
        const char *start;
        size_t n;
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        start = p;
        while (*p && *p != ',') p++;
        n = (size_t)(p - start);
        while (n && (start[n-1] == ' ' || start[n-1] == '\t')) n--;
        if (n == wn && ws_header_name_equal(start, n, wanted)) return 1;
        if (*p == ',') p++;
    }
    return 0;
}
static int ws_base64_key_is_16_bytes(const char *s) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned int acc = 0; int bits = 0; size_t i, n = strlen(s), out = 0;
    if (n != 24 || s[22] != '=' || s[23] != '=') return 0;
    for (i = 0; i < 22; i++) {
        const char *q = strchr(alphabet, s[i]);
        if (!q) return 0;
        acc = (acc << 6) | (unsigned int)(q - alphabet); bits += 6;
        if (bits >= 8) { bits -= 8; out++; }
    }
    return out == 16 && (acc & 0x0fu) == 0;
}
static int ws_wait_peer(FlatWsPeerSocket fd, long timeout_ms) {
#ifdef _WIN32
    fd_set set; struct timeval tv, *tvp = NULL; int rc;
    FD_ZERO(&set); FD_SET(fd, &set);
    if (timeout_ms >= 0) { tv.tv_sec = timeout_ms / 1000; tv.tv_usec = (timeout_ms % 1000) * 1000; tvp = &tv; }
    rc = select(0, &set, NULL, NULL, tvp); return rc;
#else
    struct pollfd pfd; int rc;
    pfd.fd = fd; pfd.events = POLLIN; pfd.revents = 0;
    do { rc = poll(&pfd, 1, timeout_ms < 0 ? -1 : (int)timeout_ms); } while (rc < 0 && errno == EINTR);
    if (rc > 0 && (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))) return -1;
    return rc;
#endif
}
static int ws_send_all(FlatWsPeerSocket fd, const char *data, size_t len) {
    size_t off = 0;
    while (off < len) {
#ifdef _WIN32
        int n = send(fd, data + off, (int)(len - off), 0);
#else
        ssize_t n = send(fd, data + off, len - off,
#ifdef MSG_NOSIGNAL
                         MSG_NOSIGNAL
#else
                         0
#endif
                         );
#endif
        if (n <= 0) return -1;
        off += (size_t)n;
    }
    return 0;
}
int flat_ws_server_peer_accept_upgrade(intptr_t peer, long timeout_ms,
                                       char *error, unsigned long error_capacity) {
    char request[FLAT_WS_SERVER_HTTP_MAX + 1];
    size_t used = 0, i;
    char key[128] = {0}, accept_input[256], response[512];
    int have_upgrade = 0, have_connection = 0, have_version = 0;
    unsigned char digest[FLAT_HASH_DIGEST_MAX]; size_t digest_len = 0;
    char why[64];
    char *accept_value = NULL;
    static const char guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    FlatWsPeerSocket fd = (FlatWsPeerSocket)peer;
    if (peer == (intptr_t)-1 || timeout_ms < -1) { ws_server_error(error, error_capacity, "invalid WebSocket upgrade arguments"); return -1; }
    while (used < FLAT_WS_SERVER_HTTP_MAX) {
        int ready = ws_wait_peer(fd, timeout_ms);
        if (ready == 0) { ws_server_error(error, error_capacity, "WebSocket upgrade timed out"); return -1; }
        if (ready < 0) { ws_server_error(error, error_capacity, "WebSocket upgrade read failed"); return -1; }
#ifdef _WIN32
        { char ch; int n = recv(fd, &ch, 1, 0); if (n <= 0) { ws_server_error(error, error_capacity, "incomplete WebSocket upgrade request"); return -1; } request[used++] = ch; }
#else
        { char ch; ssize_t n = recv(fd, &ch, 1, 0); if (n <= 0) { ws_server_error(error, error_capacity, "incomplete WebSocket upgrade request"); return -1; } request[used++] = ch; }
#endif
        if (used >= 4 && memcmp(request + used - 4, "\r\n\r\n", 4) == 0) break;
    }
    if (used == FLAT_WS_SERVER_HTTP_MAX) { ws_server_error(error, error_capacity, "WebSocket upgrade headers too large"); return -1; }
    request[used] = '\0';
    if (strncmp(request, "GET ", 4) != 0 || !strstr(request, " HTTP/1.1\r\n")) { ws_server_error(error, error_capacity, "WebSocket upgrade requires GET HTTP/1.1"); return -1; }
    i = 0;
    while (i + 1 < used) {
        size_t start = i, colon, end;
        while (i + 1 < used && !(request[i] == '\r' && request[i+1] == '\n')) i++;
        end = i; i += 2;
        if (end == start) break;
        if (start == 0) continue;
        colon = start; while (colon < end && request[colon] != ':') colon++;
        if (colon == end) { ws_server_error(error, error_capacity, "malformed HTTP header"); return -1; }
        { size_t v = colon + 1, vn = end - v; while (v < end && (request[v] == ' ' || request[v] == '\t')) { v++; vn--; }
          while (vn && (request[v+vn-1] == ' ' || request[v+vn-1] == '\t')) vn--;
          if (ws_header_name_equal(request + start, colon - start, "Upgrade")) have_upgrade = ws_header_name_equal(request + v, vn, "websocket");
          else if (ws_header_name_equal(request + start, colon - start, "Connection")) { char tmp[256]; if (vn >= sizeof tmp) { ws_server_error(error, error_capacity, "Connection header too long"); return -1; } memcpy(tmp, request + v, vn); tmp[vn] = 0; have_connection = ws_header_has_token(tmp, "upgrade"); }
          else if (ws_header_name_equal(request + start, colon - start, "Sec-WebSocket-Version")) have_version = vn == 2 && memcmp(request + v, "13", 2) == 0;
          else if (ws_header_name_equal(request + start, colon - start, "Sec-WebSocket-Key")) { if (vn >= sizeof key) { ws_server_error(error, error_capacity, "WebSocket key too long"); return -1; } memcpy(key, request + v, vn); key[vn] = 0; }
        }
    }
    if (!have_upgrade || !have_connection || !have_version || !ws_base64_key_is_16_bytes(key)) {
        ws_server_error(error, error_capacity, "invalid WebSocket upgrade headers"); return -1;
    }
    if (snprintf(accept_input, sizeof accept_input, "%s%s", key, guid) >= (int)sizeof accept_input ||
        flat_hash_digest("sha1", (const unsigned char *)accept_input, strlen(accept_input), digest, &digest_len, why, sizeof why) != 0 || digest_len != 20) {
        ws_server_error(error, error_capacity, "unable to calculate WebSocket accept key"); return -1;
    }
    {
        static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        size_t o = 0;
        for (i = 0; i < digest_len; i += 3) {
            unsigned int v = (unsigned int)digest[i] << 16;
            size_t rem = digest_len - i;
            if (rem > 1) v |= (unsigned int)digest[i+1] << 8;
            if (rem > 2) v |= digest[i+2];
            response[o++] = b64[(v >> 18) & 63]; response[o++] = b64[(v >> 12) & 63];
            response[o++] = rem > 1 ? b64[(v >> 6) & 63] : '='; response[o++] = rem > 2 ? b64[v & 63] : '=';
        }
        response[o] = 0;
        accept_value = (char *)malloc(o + 1); if (!accept_value) { ws_server_error(error, error_capacity, "out of memory"); return -1; }
        memcpy(accept_value, response, o + 1);
    }
    if (snprintf(response, sizeof response,
        "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", accept_value) >= (int)sizeof response) {
        free(accept_value); ws_server_error(error, error_capacity, "WebSocket response too large"); return -1;
    }
    free(accept_value);
    if (ws_send_all(fd, response, strlen(response)) != 0) { ws_server_error(error, error_capacity, "unable to write WebSocket upgrade response"); return -1; }
    if (error && error_capacity) error[0] = '\0';
    return 0;
}
