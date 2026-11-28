#ifndef FLAT_IO_SERVICE_H
#define FLAT_IO_SERVICE_H

#include "flat_io_reactor.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FlatIoService FlatIoService;
typedef void (*FlatIoServiceCallback)(void *context, const FlatIoEvent *event);

/* A single owner thread waits on a FlatIoReactor and dispatches readiness
 * notifications. Callbacks run on the I/O thread and MUST NOT execute VM code
 * or call stop/destroy on their own service. They should perform bounded native
 * I/O and enqueue work for the runtime. The owner must serialize destroy against
 * all other API calls; stop joins any callback already in progress. */
FlatIoService *flat_io_service_create(FlatIoServiceCallback callback, void *context);
int flat_io_service_start(FlatIoService *service);
int flat_io_service_add(FlatIoService *service, intptr_t descriptor,
                        unsigned events, uint64_t token);
int flat_io_service_modify(FlatIoService *service, intptr_t descriptor,
                           unsigned events, uint64_t token);
int flat_io_service_remove(FlatIoService *service, intptr_t descriptor);
int flat_io_service_wake(FlatIoService *service);
/* Stop wakes and joins the owner thread. Safe to call once; repeated stop is OK. */
int flat_io_service_stop(FlatIoService *service);
int flat_io_service_destroy(FlatIoService *service);

#ifdef __cplusplus
}
#endif
#endif
