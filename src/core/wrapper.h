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
 * answers its refusal into `refusal`, lets the lock go, tells the deadline hook and dispatches.
 */
weivel_c_status weivel_c_core_invoke(weivel_c_core *core, weivel_c_core_call call, void *arguments,
                                     weivel_c_call_parts parts, weivel_c_refusal *refusal);

/**
 * What a call does with its answer before any event that names it can be heard: `record` and the `data` it is called
 * with. It runs under the layer lock, after the core answered WEIVEL_STATUS_OK and the call's events were drained and
 * before any of them is dispatched, on this thread or on another, so an entry it makes under the handle the core
 * just answered is there when a listener hears of that handle.
 *
 * It is the layer's own code and never a caller's: it may not call into the core, which would wait on the lock it
 * runs under, and it calls nothing it was handed. A status other than WEIVEL_C_STATUS_OK becomes the call's answer,
 * with the core's answer already written where the call writes it, so the caller can still end what the core made.
 */
typedef struct weivel_c_answered_step {
    weivel_c_status (*record)(void *data);
    void *data;
} weivel_c_answered_step;

/** weivel_c_core_invoke with a step run once the core has answered OK; `then` may be NULL. */
weivel_c_status weivel_c_core_invoke_then(weivel_c_core *core, weivel_c_core_call call, void *arguments,
                                          weivel_c_call_parts parts, weivel_c_refusal *refusal,
                                          const weivel_c_answered_step *then);

/**
 * weivel_c_core_invoke for a call that itself hands the core a time: it is not preceded by the check for a due
 * deadline, so the clock is not read for it.
 */
weivel_c_status weivel_c_core_invoke_handing_time(weivel_c_core *core, weivel_c_core_call call, void *arguments,
                                                  weivel_c_refusal *refusal);

/**
 * Has the `follow` hook told the recorded deadline once more, as leaving a call does for a new one: what a timer asks
 * when its task ran before the deadline it waits for. Answers WEIVEL_C_STATUS_OK or WEIVEL_C_STATUS_DESTROYED.
 */
weivel_c_status weivel_c_core_follow_deadline_again(weivel_c_core *core);

/** 1 once the core behind the object is destroyed, so that no listener and no step of it will be taken again. */
int weivel_c_core_is_destroyed(weivel_c_core *core);

/** One release of an answer the core filled: it makes exactly one call through `table`, which needs no core. */
typedef weivel_status (*weivel_c_core_release)(const weivel_c_core_table *table, void *answer);

/** Releases an answer through a core's table. It queues nothing, so it takes no lock and drains nothing. */
weivel_c_status weivel_c_core_invoke_release(weivel_c_core *core, weivel_c_core_release release, void *answer);

/** Tells the core's error sink of a failure, outside the layer lock; a sink that fails goes to standard error. */
void weivel_c_core_report(weivel_c_core *core, const weivel_c_failure *failure);

#endif
