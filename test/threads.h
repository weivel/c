#ifndef WEIVEL_C_TEST_THREADS_H
#define WEIVEL_C_TEST_THREADS_H

/**
 * @file
 * A thread a test starts and joins and a mutex a test's own fixture guards itself with, on the platform's own.
 */

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#endif

typedef struct test_thread {
    void (*body)(void *argument);
    void *argument;
#if defined(_WIN32)
    HANDLE platform_thread;
#else
    pthread_t platform_thread;
#endif
} test_thread;

#if defined(_WIN32)

typedef SRWLOCK test_mutex;

static inline void start_test_mutex(test_mutex *mutex) {
    InitializeSRWLock(mutex);
}

static inline void lock_test_mutex(test_mutex *mutex) {
    AcquireSRWLockExclusive(mutex);
}

static inline void unlock_test_mutex(test_mutex *mutex) {
    ReleaseSRWLockExclusive(mutex);
}

static DWORD WINAPI run_test_thread(LPVOID argument) {
    test_thread *thread = argument;
    thread->body(thread->argument);
    return 0;
}

static inline int start_test_thread(test_thread *thread, void (*body)(void *argument), void *argument) {
    thread->body = body;
    thread->argument = argument;
    thread->platform_thread = CreateThread(NULL, 0, run_test_thread, thread, 0, NULL);
    return thread->platform_thread != NULL;
}

static inline void join_test_thread(test_thread *thread) {
    WaitForSingleObject(thread->platform_thread, INFINITE);
    CloseHandle(thread->platform_thread);
}

static inline void let_other_threads_run(void) {
    SwitchToThread();
}

#else

typedef pthread_mutex_t test_mutex;

static inline void start_test_mutex(test_mutex *mutex) {
    pthread_mutex_init(mutex, NULL);
}

static inline void lock_test_mutex(test_mutex *mutex) {
    pthread_mutex_lock(mutex);
}

static inline void unlock_test_mutex(test_mutex *mutex) {
    pthread_mutex_unlock(mutex);
}

static void *run_test_thread(void *argument) {
    test_thread *thread = argument;
    thread->body(thread->argument);
    return NULL;
}

static inline int start_test_thread(test_thread *thread, void (*body)(void *argument), void *argument) {
    thread->body = body;
    thread->argument = argument;
    return pthread_create(&thread->platform_thread, NULL, run_test_thread, thread) == 0;
}

static inline void join_test_thread(test_thread *thread) {
    pthread_join(thread->platform_thread, NULL);
}

static inline void let_other_threads_run(void) {
    sched_yield();
}

#endif

#endif
