#ifndef WEIVEL_C_TEST_HOST_GATES_H
#define WEIVEL_C_TEST_HOST_GATES_H

/**
 * @file
 * A flag one thread raises and another waits on, guarded so the thread sanitizer sees each crossing: what the host's
 * threaded tests hold a thread still with at a point they choose.
 */

#include "threads.h"

typedef struct test_gate {
    test_mutex mutex;
    int open;
} test_gate;

static inline void start_gate(test_gate *self) {
    start_test_mutex(&self->mutex);
    self->open = 0;
}

static inline void open_gate(test_gate *self) {
    lock_test_mutex(&self->mutex);
    self->open = 1;
    unlock_test_mutex(&self->mutex);
}

static inline int is_open(test_gate *self) {
    int open;
    lock_test_mutex(&self->mutex);
    open = self->open;
    unlock_test_mutex(&self->mutex);
    return open;
}

static inline void pass_gate(test_gate *self) {
    while (!is_open(self)) {
        let_other_threads_run();
    }
}

#endif
