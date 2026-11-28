#include "flat_io_reactor.h"
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif
#include "flat_sync.h"

#include <stdlib.h>
#include <string.h>
#include <limits.h>

#ifdef _WIN32
typedef SOCKET ReactorSocket;
#define REACTOR_INVALID_SOCKET INVALID_SOCKET
#define reactor_close closesocket
#define REACTOR_POLL_READ POLLRDNORM
#define REACTOR_POLL_WRITE POLLWRNORM
#define REACTOR_READ_RESULT POLLRDNORM
#define REACTOR_WRITE_RESULT POLLWRNORM
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
typedef int ReactorSocket;
#define REACTOR_INVALID_SOCKET (-1)
#define reactor_close close
#define REACTOR_POLL_READ POLLIN
#define REACTOR_POLL_WRITE POLLOUT
#define REACTOR_READ_RESULT POLLIN
#define REACTOR_WRITE_RESULT POLLOUT
#ifndef POLLERR
#define POLLERR 0x008
#endif
#ifndef POLLHUP
#define POLLHUP 0x010
#endif
#ifndef POLLNVAL
#define POLLNVAL 0x020
#endif
#endif

typedef struct ReactorEntry {
    intptr_t descriptor;
    unsigned events;
    uint64_t token;
} ReactorEntry;

struct FlatIoReactor {
    FlatMutex mutex;
    ReactorEntry *entries;
    size_t count;
    size_t capacity;
#ifdef _WIN32
    ReactorSocket wake_read;
    ReactorSocket wake_write;
    int winsock_started;
#else
    int wake_read;
    int wake_write;
#endif
    int closing;
};

static int reactor_make_wake_pair(FlatIoReactor *r) {
#ifdef _WIN32
    struct sockaddr_in addr;
    int addr_len = (int)sizeof addr;
    u_long nonblocking = 1;
    SOCKET rd = INVALID_SOCKET, wr = INVALID_SOCKET;
    rd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (rd == INVALID_SOCKET) return -1;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(rd, (struct sockaddr *)&addr, sizeof addr) != 0 ||
        getsockname(rd, (struct sockaddr *)&addr, &addr_len) != 0) goto win_fail;
    wr = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (wr == INVALID_SOCKET || connect(wr, (struct sockaddr *)&addr, sizeof addr) != 0)
        goto win_fail;
    if (ioctlsocket(rd, FIONBIO, &nonblocking) != 0 ||
        ioctlsocket(wr, FIONBIO, &nonblocking) != 0) goto win_fail;
    r->wake_read = rd;
    r->wake_write = wr;
    return 0;
win_fail:
    if (wr != INVALID_SOCKET) closesocket(wr);
    if (rd != INVALID_SOCKET) closesocket(rd);
    return -1;
#else
    int fds[2];
    int flags;
    if (pipe(fds) != 0) return -1;
    flags = fcntl(fds[0], F_GETFL, 0);
    if (flags < 0 || fcntl(fds[0], F_SETFL, flags | O_NONBLOCK) != 0) goto posix_fail;
    flags = fcntl(fds[1], F_GETFL, 0);
    if (flags < 0 || fcntl(fds[1], F_SETFL, flags | O_NONBLOCK) != 0) goto posix_fail;
    (void)fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    (void)fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    r->wake_read = fds[0];
    r->wake_write = fds[1];
    return 0;
posix_fail:
    close(fds[0]); close(fds[1]);
    return -1;
#endif
}

static void reactor_drain_wake(FlatIoReactor *r) {
    char buffer[128];
#ifdef _WIN32
    while (recv(r->wake_read, buffer, (int)sizeof buffer, 0) > 0) {}
#else
    while (read(r->wake_read, buffer, sizeof buffer) > 0) {}
#endif
}

FlatIoReactor *flat_io_reactor_create(void) {
    FlatIoReactor *r = (FlatIoReactor *)calloc(1, sizeof *r);
    if (!r) return NULL;
#ifdef _WIN32
    {
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) { free(r); return NULL; }
        r->winsock_started = 1;
    }
    r->wake_read = r->wake_write = INVALID_SOCKET;
#else
    r->wake_read = r->wake_write = -1;
#endif
    if (flat_mutex_init(&r->mutex) != 0) {
#ifdef _WIN32
        if (r->winsock_started) WSACleanup();
#endif
        free(r);
        return NULL;
    }
    if (reactor_make_wake_pair(r) != 0) {
        if (r->wake_read != REACTOR_INVALID_SOCKET) reactor_close(r->wake_read);
        if (r->wake_write != REACTOR_INVALID_SOCKET) reactor_close(r->wake_write);
#ifdef _WIN32
        if (r->winsock_started) WSACleanup();
#endif
        flat_mutex_destroy(&r->mutex);
        free(r);
        return NULL;
    }
    return r;
}

static int reactor_valid_events(unsigned events) {
    return events != 0 && (events & ~(FLAT_IO_EVENT_READ | FLAT_IO_EVENT_WRITE)) == 0;
}

static size_t reactor_find(FlatIoReactor *r, intptr_t fd) {
    size_t i;
    for (i = 0; i < r->count; ++i) if (r->entries[i].descriptor == fd) return i;
    return SIZE_MAX;
}

int flat_io_reactor_add(FlatIoReactor *r, intptr_t fd, unsigned events, uint64_t token) {
    ReactorEntry *next;
    size_t cap;
    if (!r || fd < 0 || !token || !reactor_valid_events(events)) return -1;
    if (flat_mutex_lock(&r->mutex) != 0) return -1;
    if (r->closing || reactor_find(r, fd) != SIZE_MAX) { flat_mutex_unlock(&r->mutex); return -1; }
    if (r->count == r->capacity) {
        cap = r->capacity ? r->capacity * 2 : 16;
        if (cap < r->capacity || cap > SIZE_MAX / sizeof *next) { flat_mutex_unlock(&r->mutex); return -1; }
        next = (ReactorEntry *)realloc(r->entries, cap * sizeof *next);
        if (!next) { flat_mutex_unlock(&r->mutex); return -1; }
        r->entries = next; r->capacity = cap;
    }
    r->entries[r->count].descriptor = fd;
    r->entries[r->count].events = events;
    r->entries[r->count].token = token;
    r->count++;
    flat_mutex_unlock(&r->mutex);
    (void)flat_io_reactor_wake(r);
    return 0;
}

int flat_io_reactor_modify(FlatIoReactor *r, intptr_t fd, unsigned events, uint64_t token) {
    size_t i;
    if (!r || fd < 0 || !token || !reactor_valid_events(events)) return -1;
    if (flat_mutex_lock(&r->mutex) != 0) return -1;
    i = reactor_find(r, fd);
    if (r->closing || i == SIZE_MAX) { flat_mutex_unlock(&r->mutex); return -1; }
    r->entries[i].events = events; r->entries[i].token = token;
    flat_mutex_unlock(&r->mutex);
    (void)flat_io_reactor_wake(r);
    return 0;
}

int flat_io_reactor_remove(FlatIoReactor *r, intptr_t fd) {
    size_t i;
    if (!r || fd < 0) return -1;
    if (flat_mutex_lock(&r->mutex) != 0) return -1;
    i = reactor_find(r, fd);
    if (i == SIZE_MAX) { flat_mutex_unlock(&r->mutex); return -1; }
    r->entries[i] = r->entries[r->count - 1];
    r->count--;
    flat_mutex_unlock(&r->mutex);
    (void)flat_io_reactor_wake(r);
    return 0;
}

int flat_io_reactor_wake(FlatIoReactor *r) {
    char byte = 1;
    if (!r) return -1;
#ifdef _WIN32
    { int rc = send(r->wake_write, &byte, 1, 0); return rc == 1 || WSAGetLastError() == WSAEWOULDBLOCK ? 0 : -1; }
#else
    { ssize_t rc = write(r->wake_write, &byte, 1); return rc == 1 || (rc < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) ? 0 : -1; }
#endif
}

int flat_io_reactor_wait(FlatIoReactor *r, FlatIoEvent *out, size_t capacity, long timeout_ms) {
    ReactorEntry *snapshot = NULL;
    size_t count, i;
    int rc, produced = 0;
#ifdef _WIN32
    WSAPOLLFD *fds = NULL;
#else
    struct pollfd *fds = NULL;
#endif
    if (!r || !out || capacity == 0 || timeout_ms < -1) return -1;
    if (flat_mutex_lock(&r->mutex) != 0) return -1;
    if (r->closing) { flat_mutex_unlock(&r->mutex); return -1; }
    count = r->count;
    if (count) {
        snapshot = (ReactorEntry *)malloc(count * sizeof *snapshot);
        if (!snapshot) { flat_mutex_unlock(&r->mutex); return -1; }
        memcpy(snapshot, r->entries, count * sizeof *snapshot);
    }
    flat_mutex_unlock(&r->mutex);
#ifdef _WIN32
    fds = (WSAPOLLFD *)calloc(count + 1, sizeof *fds);
#else
    fds = (struct pollfd *)calloc(count + 1, sizeof *fds);
#endif
    if (!fds) { free(snapshot); return -1; }
    fds[0].fd = r->wake_read; fds[0].events = REACTOR_POLL_READ;
    for (i = 0; i < count; ++i) {
        fds[i + 1].fd = (ReactorSocket)snapshot[i].descriptor;
        if (snapshot[i].events & FLAT_IO_EVENT_READ) fds[i + 1].events |= REACTOR_POLL_READ;
        if (snapshot[i].events & FLAT_IO_EVENT_WRITE) fds[i + 1].events |= REACTOR_POLL_WRITE;
    }
#ifdef _WIN32
    rc = WSAPoll(fds, (ULONG)(count + 1), timeout_ms < 0 ? -1 : (INT)timeout_ms);
#else
    do { rc = poll(fds, (nfds_t)(count + 1), timeout_ms < 0 ? -1 : (int)(timeout_ms > INT_MAX ? INT_MAX : timeout_ms)); }
    while (rc < 0 && errno == EINTR);
#endif
    if (rc < 0) { free(fds); free(snapshot); return -1; }
    if (rc > 0 && fds[0].revents) reactor_drain_wake(r);
    for (i = 0; i < count && (size_t)produced < capacity; ++i) {
        unsigned events = 0;
        short re = fds[i + 1].revents;
        if (re & REACTOR_READ_RESULT) events |= FLAT_IO_EVENT_READ;
        if (re & REACTOR_WRITE_RESULT) events |= FLAT_IO_EVENT_WRITE;
        if (re & (POLLERR | POLLNVAL)) events |= FLAT_IO_EVENT_ERROR;
        if (re & POLLHUP) events |= FLAT_IO_EVENT_HANGUP;
        if (events) { out[produced].token = snapshot[i].token; out[produced].events = events; produced++; }
    }
    free(fds); free(snapshot);
    return produced;
}

int flat_io_reactor_destroy(FlatIoReactor *r) {
    if (!r) return -1;
    if (flat_mutex_lock(&r->mutex) != 0) return -1;
    r->closing = 1;
    flat_mutex_unlock(&r->mutex);
    (void)flat_io_reactor_wake(r);
    /* The owner must join the wait thread before destroy. */
    free(r->entries);
    reactor_close(r->wake_read);
    reactor_close(r->wake_write);
#ifdef _WIN32
    if (r->winsock_started) WSACleanup();
#endif
    flat_mutex_destroy(&r->mutex);
    free(r);
    return 0;
}
