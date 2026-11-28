#include "flat_io_service.h"
#include "flat_sync.h"

#include <stdlib.h>

#define FLAT_IO_SERVICE_BATCH 64

struct FlatIoService {
    FlatMutex mutex;
    FlatIoReactor *reactor;
    FlatThread thread;
    FlatIoServiceCallback callback;
    void *context;
    int started;
    int stopping;
    int joined;
};

static void *flat_io_service_main(void *opaque) {
    FlatIoService *service = (FlatIoService *)opaque;
    FlatIoEvent events[FLAT_IO_SERVICE_BATCH];
    for (;;) {
        int stopping;
        int count;
        size_t i;
        if (flat_mutex_lock(&service->mutex) != 0) break;
        stopping = service->stopping;
        flat_mutex_unlock(&service->mutex);
        if (stopping) break;
        count = flat_io_reactor_wait(service->reactor, events,
                                     FLAT_IO_SERVICE_BATCH, -1);
        if (count < 0) {
            if (flat_mutex_lock(&service->mutex) == 0) {
                service->stopping = 1;
                flat_mutex_unlock(&service->mutex);
            }
            break;
        }
        for (i = 0; i < (size_t)count; ++i) {
            if (flat_mutex_lock(&service->mutex) != 0) return NULL;
            stopping = service->stopping;
            flat_mutex_unlock(&service->mutex);
            if (stopping) break;
            /* Callback executes outside the service lock. */
            service->callback(service->context, &events[i]);
        }
    }
    return NULL;
}

FlatIoService *flat_io_service_create(FlatIoServiceCallback callback, void *context) {
    FlatIoService *service;
    if (!callback) return NULL;
    service = (FlatIoService *)calloc(1, sizeof *service);
    if (!service) return NULL;
    if (flat_mutex_init(&service->mutex) != 0) { free(service); return NULL; }
    service->reactor = flat_io_reactor_create();
    if (!service->reactor) {
        flat_mutex_destroy(&service->mutex);
        free(service);
        return NULL;
    }
    service->callback = callback;
    service->context = context;
    return service;
}

int flat_io_service_start(FlatIoService *service) {
    int rc;
    if (!service || !flat_threads_supported()) return -1;
    if (flat_mutex_lock(&service->mutex) != 0) return -1;
    if (service->started || service->stopping || service->joined) {
        flat_mutex_unlock(&service->mutex);
        return -1;
    }
    service->started = 1;
    rc = flat_thread_create(&service->thread, flat_io_service_main, service);
    if (rc != 0) service->started = 0;
    flat_mutex_unlock(&service->mutex);
    return rc == 0 ? 0 : -1;
}

int flat_io_service_add(FlatIoService *service, intptr_t descriptor,
                        unsigned events, uint64_t token) {
    int rc;
    if (!service) return -1;
    if (flat_mutex_lock(&service->mutex) != 0) return -1;
    if (service->stopping || service->joined) {
        flat_mutex_unlock(&service->mutex);
        return -1;
    }
    rc = flat_io_reactor_add(service->reactor, descriptor, events, token);
    flat_mutex_unlock(&service->mutex);
    return rc;
}

int flat_io_service_modify(FlatIoService *service, intptr_t descriptor,
                           unsigned events, uint64_t token) {
    int rc;
    if (!service) return -1;
    if (flat_mutex_lock(&service->mutex) != 0) return -1;
    if (service->stopping || service->joined) {
        flat_mutex_unlock(&service->mutex);
        return -1;
    }
    rc = flat_io_reactor_modify(service->reactor, descriptor, events, token);
    flat_mutex_unlock(&service->mutex);
    return rc;
}

int flat_io_service_remove(FlatIoService *service, intptr_t descriptor) {
    int rc;
    if (!service) return -1;
    if (flat_mutex_lock(&service->mutex) != 0) return -1;
    rc = flat_io_reactor_remove(service->reactor, descriptor);
    flat_mutex_unlock(&service->mutex);
    return rc;
}

int flat_io_service_wake(FlatIoService *service) {
    return service ? flat_io_reactor_wake(service->reactor) : -1;
}

int flat_io_service_stop(FlatIoService *service) {
    int started;
    if (!service) return -1;
    if (flat_mutex_lock(&service->mutex) != 0) return -1;
    if (service->joined) { flat_mutex_unlock(&service->mutex); return 0; }
    service->stopping = 1;
    started = service->started;
    flat_mutex_unlock(&service->mutex);
    (void)flat_io_reactor_wake(service->reactor);
    if (started && flat_thread_join(&service->thread) != 0) return -1;
    if (flat_mutex_lock(&service->mutex) != 0) return -1;
    service->joined = 1;
    service->started = 0;
    flat_mutex_unlock(&service->mutex);
    return 0;
}

int flat_io_service_destroy(FlatIoService *service) {
    if (!service) return -1;
    if (flat_io_service_stop(service) != 0) return -1;
    if (flat_io_reactor_destroy(service->reactor) != 0) return -1;
    if (flat_mutex_destroy(&service->mutex) != 0) return -1;
    free(service);
    return 0;
}
