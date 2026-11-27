#include "flat_runtime.h"
#include "flat_sync.h"

#include <stdio.h>

int main(void) {
    FlatMutex mutex;
    FlatCond cond;
    FlatThread thread;
    FlatRuntime *runtime;

    if (flat_threads_supported() != 0) {
        fprintf(stderr, "single-thread Emscripten shim reported thread support\n");
        return 1;
    }

    if (flat_mutex_init(&mutex) != 0 || flat_mutex_lock(&mutex) != 0 ||
        flat_mutex_unlock(&mutex) != 0 || flat_mutex_destroy(&mutex) != 0) {
        fprintf(stderr, "single-thread mutex shim contract failed\n");
        return 2;
    }
    if (flat_cond_init(&cond) != 0 || flat_cond_signal(&cond) != 0 ||
        flat_cond_broadcast(&cond) != 0 || flat_cond_wait(&cond, &mutex) == 0 ||
        flat_cond_destroy(&cond) != 0) {
        fprintf(stderr, "single-thread condition shim contract failed\n");
        return 3;
    }
    if (flat_thread_create(&thread, NULL, NULL) == 0 ||
        flat_thread_join(&thread) == 0) {
        fprintf(stderr, "single-thread thread operations were not rejected\n");
        return 4;
    }

    runtime = flat_runtime_create(NULL);
    if (!runtime) {
        fprintf(stderr, "runtime creation failed under single-thread shim\n");
        return 5;
    }
    if (flat_runtime_workers_start(runtime, 2, 1) == 0 ||
        flat_runtime_lifecycle(runtime) != FLAT_RUNTIME_RUNNING ||
        flat_runtime_workers_join(runtime) == 0) {
        fprintf(stderr, "worker startup was not cleanly rejected\n");
        (void)flat_runtime_destroy(runtime);
        return 6;
    }
    if (flat_runtime_destroy(runtime) != 0) {
        fprintf(stderr, "runtime could not be destroyed after rejected worker startup\n");
        return 7;
    }

    puts("emscripten-no-pthreads-ok sync=noop blocking=rejected worker-start=rejected runtime=clean");
    return 0;
}
