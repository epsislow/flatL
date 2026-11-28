#ifndef FLAT_IO_REACTOR_H
#define FLAT_IO_REACTOR_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FlatIoReactor FlatIoReactor;

typedef enum FlatIoEventMask {
    FLAT_IO_EVENT_READ  = 1u,
    FLAT_IO_EVENT_WRITE = 2u,
    FLAT_IO_EVENT_ERROR = 4u,
    FLAT_IO_EVENT_HANGUP = 8u
} FlatIoEventMask;

typedef struct FlatIoEvent {
    uint64_t token;
    unsigned events;
} FlatIoEvent;

/* Portable readiness reactor. The current backend uses poll/WSAPoll and a
 * private wake channel. Tokens are opaque, caller-owned identities and are
 * never interpreted as descriptors. A remove racing with wait may leave one
 * already-snapshotted event in that wait result; callers must validate tokens
 * against their own live-connection registry before acting on them.
 */
FlatIoReactor *flat_io_reactor_create(void);
int flat_io_reactor_add(FlatIoReactor *reactor, intptr_t descriptor,
                        unsigned events, uint64_t token);
int flat_io_reactor_modify(FlatIoReactor *reactor, intptr_t descriptor,
                           unsigned events, uint64_t token);
int flat_io_reactor_remove(FlatIoReactor *reactor, intptr_t descriptor);
/* Wait returns event count (0 on timeout), or -1 on error. timeout_ms: -1
 * waits indefinitely. A wake returns 0. out_events/capacity must be valid. */
int flat_io_reactor_wait(FlatIoReactor *reactor, FlatIoEvent *out_events,
                         size_t capacity, long timeout_ms);
/* Interrupt a thread blocked in wait. Multiple wakes may coalesce. */
int flat_io_reactor_wake(FlatIoReactor *reactor);
int flat_io_reactor_destroy(FlatIoReactor *reactor);

#ifdef __cplusplus
}
#endif
#endif
