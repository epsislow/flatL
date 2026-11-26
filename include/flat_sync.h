#ifndef FLAT_SYNC_H
#define FLAT_SYNC_H

/*
 * Small cross-platform synchronization abstraction.
 *
 * POSIX builds use pthread primitives, Windows uses SRWLOCK/CONDITION_VARIABLE
 * and _beginthreadex, and single-threaded Emscripten builds expose inert mutexes
 * but explicitly reject blocking/thread operations. Emscripten pthread builds use
 * the POSIX branch.
 */

#include <stddef.h>

#if defined(_WIN32)
#include <windows.h>
#include <process.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct { SRWLOCK native; } FlatMutex;
typedef struct { CONDITION_VARIABLE native; } FlatCond;
typedef struct { uintptr_t native; void *result; } FlatThread;
typedef void *(*FlatThreadFn)(void *);
#define FLAT_MUTEX_INITIALIZER { SRWLOCK_INIT }
#define FLAT_COND_INITIALIZER { CONDITION_VARIABLE_INIT }

static inline int flat_mutex_init(FlatMutex *mutex) {
    if (!mutex) return -1;
    InitializeSRWLock(&mutex->native);
    return 0;
}
static inline int flat_mutex_destroy(FlatMutex *mutex) { return mutex ? 0 : -1; }
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
static inline int flat_cond_init(FlatCond *cond) {
    if (!cond) return -1;
    InitializeConditionVariable(&cond->native);
    return 0;
}
static inline int flat_cond_destroy(FlatCond *cond) { return cond ? 0 : -1; }
static inline int flat_cond_wait(FlatCond *cond, FlatMutex *mutex) {
    if (!cond || !mutex) return -1;
    return SleepConditionVariableSRW(&cond->native, &mutex->native, INFINITE, 0) ? 0 : -1;
}
static inline int flat_cond_signal(FlatCond *cond) {
    if (!cond) return -1;
    WakeConditionVariable(&cond->native);
    return 0;
}
static inline int flat_cond_broadcast(FlatCond *cond) {
    if (!cond) return -1;
    WakeAllConditionVariable(&cond->native);
    return 0;
}
typedef struct FlatThreadStart {
    FlatThread *thread;
    FlatThreadFn fn;
    void *arg;
} FlatThreadStart;
static inline unsigned __stdcall flat_thread_trampoline(void *opaque) {
    FlatThreadStart *start = (FlatThreadStart *)opaque;
    FlatThread *thread = start->thread;
    FlatThreadFn fn = start->fn;
    void *arg = start->arg;
    free(start);
    thread->result = fn(arg);
    return 0;
}
static inline int flat_thread_create(FlatThread *thread, FlatThreadFn fn, void *arg) {
    FlatThreadStart *start;
    uintptr_t handle;
    if (!thread || !fn) return -1;
    start = (FlatThreadStart *)malloc(sizeof *start);
    if (!start) return -1;
    start->thread = thread;
    start->fn = fn;
    start->arg = arg;
    thread->result = NULL;
    handle = _beginthreadex(NULL, 0, flat_thread_trampoline, start, 0, NULL);
    if (!handle) {
        free(start);
        return -1;
    }
    thread->native = handle;
    return 0;
}
static inline int flat_threads_supported(void) { return 1; }
static inline int flat_thread_join(FlatThread *thread) {
    HANDLE handle;
    if (!thread || !thread->native) return -1;
    handle = (HANDLE)thread->native;
    if (WaitForSingleObject(handle, INFINITE) != WAIT_OBJECT_0) return -1;
    CloseHandle(handle);
    thread->native = 0;
    return 0;
}

#elif defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)

typedef struct { int unused; } FlatMutex;
typedef struct { int unused; } FlatCond;
typedef struct { int unused; } FlatThread;
typedef void *(*FlatThreadFn)(void *);
#define FLAT_MUTEX_INITIALIZER { 0 }
#define FLAT_COND_INITIALIZER { 0 }
static inline int flat_mutex_init(FlatMutex *mutex) { return mutex ? 0 : -1; }
static inline int flat_mutex_destroy(FlatMutex *mutex) { return mutex ? 0 : -1; }
static inline int flat_mutex_lock(FlatMutex *mutex) { return mutex ? 0 : -1; }
static inline int flat_mutex_unlock(FlatMutex *mutex) { return mutex ? 0 : -1; }
static inline int flat_cond_init(FlatCond *cond) { return cond ? 0 : -1; }
static inline int flat_cond_destroy(FlatCond *cond) { return cond ? 0 : -1; }
static inline int flat_cond_wait(FlatCond *cond, FlatMutex *mutex) {
    (void)cond; (void)mutex; return -1;
}
static inline int flat_cond_signal(FlatCond *cond) { return cond ? 0 : -1; }
static inline int flat_cond_broadcast(FlatCond *cond) { return cond ? 0 : -1; }
static inline int flat_thread_create(FlatThread *thread, FlatThreadFn fn, void *arg) {
    (void)thread; (void)fn; (void)arg; return -1;
}
static inline int flat_thread_join(FlatThread *thread) { (void)thread; return -1; }
static inline int flat_threads_supported(void) { return 0; }

#else
#include <pthread.h>

typedef struct { pthread_mutex_t native; } FlatMutex;
typedef struct { pthread_cond_t native; } FlatCond;
typedef struct { pthread_t native; int valid; } FlatThread;
typedef void *(*FlatThreadFn)(void *);
#define FLAT_MUTEX_INITIALIZER { PTHREAD_MUTEX_INITIALIZER }
#define FLAT_COND_INITIALIZER { PTHREAD_COND_INITIALIZER }
static inline int flat_mutex_init(FlatMutex *mutex) {
    return mutex ? pthread_mutex_init(&mutex->native, NULL) : -1;
}
static inline int flat_mutex_destroy(FlatMutex *mutex) {
    return mutex ? pthread_mutex_destroy(&mutex->native) : -1;
}
static inline int flat_mutex_lock(FlatMutex *mutex) {
    return mutex ? pthread_mutex_lock(&mutex->native) : -1;
}
static inline int flat_mutex_unlock(FlatMutex *mutex) {
    return mutex ? pthread_mutex_unlock(&mutex->native) : -1;
}
static inline int flat_cond_init(FlatCond *cond) {
    return cond ? pthread_cond_init(&cond->native, NULL) : -1;
}
static inline int flat_cond_destroy(FlatCond *cond) {
    return cond ? pthread_cond_destroy(&cond->native) : -1;
}
static inline int flat_cond_wait(FlatCond *cond, FlatMutex *mutex) {
    return (cond && mutex) ? pthread_cond_wait(&cond->native, &mutex->native) : -1;
}
static inline int flat_cond_signal(FlatCond *cond) {
    return cond ? pthread_cond_signal(&cond->native) : -1;
}
static inline int flat_cond_broadcast(FlatCond *cond) {
    return cond ? pthread_cond_broadcast(&cond->native) : -1;
}
static inline int flat_thread_create(FlatThread *thread, FlatThreadFn fn, void *arg) {
    int rc;
    if (!thread || !fn) return -1;
    thread->valid = 0;
    rc = pthread_create(&thread->native, NULL, fn, arg);
    if (rc == 0) thread->valid = 1;
    return rc;
}
static inline int flat_threads_supported(void) { return 1; }
static inline int flat_thread_join(FlatThread *thread) {
    int rc;
    if (!thread || !thread->valid) return -1;
    rc = pthread_join(thread->native, NULL);
    if (rc == 0) thread->valid = 0;
    return rc;
}
#endif

#endif
