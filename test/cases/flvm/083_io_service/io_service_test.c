#include "flat_io_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <time.h>
#ifndef _WIN32
#include <sys/socket.h>
#include <unistd.h>
#endif

static atomic_int seen;
static atomic_uint last_token;
static void on_event(void *context, const FlatIoEvent *event) {
#ifndef _WIN32
    char byte;
    if (event->events & FLAT_IO_EVENT_READ) (void)read(*(int *)context, &byte, 1);
#endif
    atomic_store(&last_token, (unsigned)event->token);
    atomic_fetch_add(&seen, 1);
}
static void pause_ms(long ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}
int main(void) {
#ifndef _WIN32
    int pair[2];
    FlatIoService *service;
    char byte = 'x';
    int i;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) return 1;
    service = flat_io_service_create(on_event, &pair[0]);
    if (!service) return 2;
    if (flat_io_service_add(service, pair[0], FLAT_IO_EVENT_READ, 77) != 0) return 3;
    if (flat_io_service_start(service) != 0) return 4;
    if (write(pair[1], &byte, 1) != 1) return 5;
    for (i = 0; i < 100 && atomic_load(&seen) == 0; ++i) pause_ms(5);
    if (atomic_load(&seen) != 1 || atomic_load(&last_token) != 77) return 6;
    if (flat_io_service_remove(service, pair[0]) != 0) return 7;
    if (flat_io_service_stop(service) != 0) return 8;
    if (flat_io_service_stop(service) != 0) return 9;
    if (flat_io_service_add(service, pair[0], FLAT_IO_EVENT_READ, 78) == 0) return 10;
    if (flat_io_service_destroy(service) != 0) return 11;
    close(pair[0]); close(pair[1]);
#else
    /* The portable service is covered by the Windows build matrix; this
       focused socketpair test is POSIX-specific. */
#endif
    puts("io-service-ok");
    return 0;
}
