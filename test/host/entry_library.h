#ifndef WEIVEL_C_TEST_HOST_ENTRY_LIBRARY_H
#define WEIVEL_C_TEST_HOST_ENTRY_LIBRARY_H

/**
 * @file
 * What the test plugin library keeps, reached by the test through the entry's `data`: the library calls nothing of
 * the layer's, so it links nothing.
 */

#include <stdint.h>

#include <weivel/c/host/entry.h>

typedef struct entry_library_state {
    uint32_t activations;
    uint32_t deactivations;
    weivel_c_context *context;
} entry_library_state;

#endif
