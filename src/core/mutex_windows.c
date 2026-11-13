#include "mutex.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

_Static_assert(sizeof(weivel_c_mutex) == sizeof(SRWLOCK), "the mutex must be exactly an SRWLOCK");

static PSRWLOCK lock_of(weivel_c_mutex *mutex) {
    return (PSRWLOCK)(void *)mutex;
}

int weivel_c_mutex_start(weivel_c_mutex *mutex) {
    InitializeSRWLock(lock_of(mutex));
    return 1;
}

void weivel_c_mutex_end(weivel_c_mutex *mutex) {
    (void)mutex;
}

void weivel_c_mutex_lock(weivel_c_mutex *mutex) {
    AcquireSRWLockExclusive(lock_of(mutex));
}

void weivel_c_mutex_unlock(weivel_c_mutex *mutex) {
    ReleaseSRWLockExclusive(lock_of(mutex));
}
