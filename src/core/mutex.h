#ifndef WEIVEL_C_CORE_MUTEX_H
#define WEIVEL_C_CORE_MUTEX_H

#if defined(_WIN32)

/** @implNote One pointer wide, which is what a Windows SRWLOCK is, so this header needs none of the platform's. */
typedef struct weivel_c_mutex {
    void *state;
} weivel_c_mutex;

#else

#include <pthread.h>

typedef struct weivel_c_mutex {
    pthread_mutex_t state;
} weivel_c_mutex;

#endif

/** Makes a plain mutex, one that is not recursive. Answers 1, or 0 when the platform could not. */
int weivel_c_mutex_start(weivel_c_mutex *mutex);

/** Ends a mutex nothing holds. */
void weivel_c_mutex_end(weivel_c_mutex *mutex);

void weivel_c_mutex_lock(weivel_c_mutex *mutex);

void weivel_c_mutex_unlock(weivel_c_mutex *mutex);

#endif
