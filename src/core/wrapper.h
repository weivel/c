#ifndef WEIVEL_C_CORE_WRAPPER_H
#define WEIVEL_C_CORE_WRAPPER_H

/**
 * @file
 * What the layer's other sources reach of the wrapper. None of it is exported from the shared library.
 */

#include <stddef.h>

#include <weivel/c/core.h>

/** Where the layer's memory for one core comes from. `release` takes what `allocate` answered, never NULL. */
typedef struct weivel_c_allocator {
    void *(*allocate)(void *data, size_t size);
    void (*release)(void *data, void *memory);
    void *data;
} weivel_c_allocator;

/**
 * Replaces the allocator a core copies events, refusals and its listener list with: the seam a test fails an
 * allocation through. Whatever the core holds from the allocator before must be gone first.
 */
void weivel_c_core_replace_allocator(weivel_c_core *core, const weivel_c_allocator *allocator);

/** Whose a call's parts are, which decides whose mistake a WEIVEL_STATUS_MALFORMED is. */
typedef enum weivel_c_call_parts {
    /** The layer built every part: malformed is a defect of the layer. */
    WEIVEL_C_CALL_BUILT_BY_THE_LAYER = 0,
    /** The call carries a struct its caller built: malformed is the caller's mistake, answered with the refusal. */
    WEIVEL_C_CALL_CARRIES_A_CALLERS_STRUCT = 1
} weivel_c_call_parts;

/** One call into the core: it makes exactly one call through `table` with `core` and `pending_events`. */
typedef weivel_status (*weivel_c_core_call)(const weivel_c_core_table *table, weivel_core *core, void *arguments,
                                            uint32_t *pending_events);

/**
 * The one wrapper: hands the core a deadline that fell due, makes `call` under the layer lock, drains its events,
 * answers its refusal into `refusal`, lets the lock go and dispatches.
 */
weivel_c_status weivel_c_core_invoke(weivel_c_core *core, weivel_c_core_call call, void *arguments,
                                     weivel_c_call_parts parts, weivel_c_refusal *refusal);

/** One release of an answer the core filled: it makes exactly one call through `table`, which needs no core. */
typedef weivel_status (*weivel_c_core_release)(const weivel_c_core_table *table, void *answer);

/** Releases an answer through a core's table. It queues nothing, so it takes no lock and drains nothing. */
weivel_c_status weivel_c_core_invoke_release(weivel_c_core *core, weivel_c_core_release release, void *answer);

/** Tells the core's error sink of a failure, outside the layer lock; a sink that fails goes to standard error. */
void weivel_c_core_report(weivel_c_core *core, const weivel_c_failure *failure);

#endif
