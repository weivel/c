#ifndef WEIVEL_C_TEST_HOST_ENTRY_LIBRARY_H
#define WEIVEL_C_TEST_HOST_ENTRY_LIBRARY_H

/**
 * @file
 * What the test plugin library keeps, reached by the test through the entry's `data`: the library calls nothing of
 * the layer's, so it links nothing. What it calls of the layer the test hands it as a function pointer.
 */

#include <stdint.h>

#include <weivel/c/host/entry.h>

/** What the library's own code answers once the finish it made has returned into it. */
#define ENTRY_LIBRARY_SURVIVED 42u

typedef struct entry_library_state {
    uint32_t activations;
    uint32_t deactivations;
    weivel_c_context *context;
    /** 1 to have the deactivate answer pending and keep its completion. */
    uint32_t deactivate_waits;
    weivel_c_completion *deactivation;
    /** weivel_c_completion_finish, as the test hands it. */
    weivel_c_status (*finish)(weivel_c_completion *completion, uint32_t answer, const char *reason);
    /**
     * Code of the library's own: finishes the kept deactivation through `finish`, then answers
     * ENTRY_LIBRARY_SURVIVED, which it can only do when its library is still open once that finish returns.
     */
    uint32_t (*finish_from_inside)(void);
} entry_library_state;

#endif
