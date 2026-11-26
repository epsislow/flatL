#ifndef FLAT_SYNC_H
#define FLAT_SYNC_H

/* Small process-local mutex abstraction used by native invocation gates.
 * POSIX uses pthread mutexes, Windows uses SRWLOCK, and single-threaded
 * Emscripten builds use a no-op implementation. */

#if defined(_WIN32)
#include <windows.h>
typedef struct { SRWLOCK native; } FlatMutex;
#define FLAT_MUTEX_INITIALIZER { SRWLOCK_INIT }
static inline int flat_mutex_lock(FlatMutex *mutex) {
    if (!mutex) return -1;
    AcquireSRWLockExclusive(&mutex->native);
    return 0;
}
static inline int flat_mutex_unlock(FlatMutex *mutex) {
    if (!mutex) return -1;
    ReleaseSRWLockExclusive(&mutex->native);
    return 0;
}
#elif defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
typedef struct { int unused; } FlatMutex;
#define FLAT_MUTEX_INITIALIZER { 0 }
static inline int flat_mutex_lock(FlatMutex *mutex) { return mutex ? 0 : -1; }
static inline int flat_mutex_unlock(FlatMutex *mutex) { return mutex ? 0 : -1; }
#else
#include <pthread.h>
typedef struct { pthread_mutex_t native; } FlatMutex;
#define FLAT_MUTEX_INITIALIZER { PTHREAD_MUTEX_INITIALIZER }
static inline int flat_mutex_lock(FlatMutex *mutex) {
    return mutex ? pthread_mutex_lock(&mutex->native) : -1;
}
static inline int flat_mutex_unlock(FlatMutex *mutex) {
    return mutex ? pthread_mutex_unlock(&mutex->native) : -1;
}
#endif

#endif
