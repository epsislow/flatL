#include "flat_io_reactor.h"
#include <stdio.h>
#include <stdint.h>
#ifdef _WIN32
int main(void) { puts("io-reactor-ok"); return 0; }
#else
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>
typedef struct WaitCtx { FlatIoReactor *reactor; int result; } WaitCtx;
static void *wait_worker(void *arg) {
    WaitCtx *ctx = (WaitCtx *)arg;
    FlatIoEvent event;
    ctx->result = flat_io_reactor_wait(ctx->reactor, &event, 1, -1);
    return NULL;
}
int main(void) {
    FlatIoReactor *r = flat_io_reactor_create();
    int pair[2]; char byte = 'x'; FlatIoEvent event;
    pthread_t thread; WaitCtx ctx;
    if (!r || socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) return 2;
    if (flat_io_reactor_add(r, pair[0], FLAT_IO_EVENT_READ, 41) != 0) return 3;
    if (write(pair[1], &byte, 1) != 1) return 4;
    if (flat_io_reactor_wait(r, &event, 1, 1000) != 1 ||
        event.token != 41 || !(event.events & FLAT_IO_EVENT_READ)) return 5;
    if (read(pair[0], &byte, 1) != 1) return 6;
    if (flat_io_reactor_modify(r, pair[0], FLAT_IO_EVENT_WRITE, 42) != 0) return 7;
    if (flat_io_reactor_wait(r, &event, 1, 1000) != 1 ||
        event.token != 42 || !(event.events & FLAT_IO_EVENT_WRITE)) return 8;
    if (flat_io_reactor_remove(r, pair[0]) != 0) return 9;
    if (flat_io_reactor_wait(r, &event, 1, 10) != 0) return 10;
    ctx.reactor = r; ctx.result = -99;
    if (pthread_create(&thread, NULL, wait_worker, &ctx) != 0) return 11;
    { struct timespec ts; ts.tv_sec = 0; ts.tv_nsec = 30000000L; nanosleep(&ts, NULL); }
    if (flat_io_reactor_wake(r) != 0) return 12;
    pthread_join(thread, NULL);
    if (ctx.result != 0) return 13;
    close(pair[0]); close(pair[1]);
    if (flat_io_reactor_destroy(r) != 0) return 14;
    puts("io-reactor-ok"); return 0;
}
#endif
