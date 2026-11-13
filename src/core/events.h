#ifndef WEIVEL_C_CORE_EVENTS_H
#define WEIVEL_C_CORE_EVENTS_H

#include "wrapper.h"

/** A drained event waiting to be dispatched: its place in the queue, then the layer's copy of it. */
typedef struct weivel_c_queued_event {
    weivel_c_queue_place place;
    weivel_c_event event;
} weivel_c_queued_event;

/** What became of an event the layer tried to copy. */
typedef enum weivel_c_copied {
    WEIVEL_C_COPIED = 0,
    /** A kind this layer does not know: ignored, as the core's header asks, and still released. */
    WEIVEL_C_COPY_SKIPPED = 1,
    /** No memory for the copy, or an array whose stride cannot be read. */
    WEIVEL_C_COPY_FAILED = 2
} weivel_c_copied;

/**
 * Copies `source`, an event the core just filled, into one block of the layer's: the event, then every text and
 * array its payload carries. `*copy` is NULL on every answer but WEIVEL_C_COPIED.
 */
weivel_c_copied weivel_c_event_copy(const weivel_c_allocator *allocator, const weivel_event *source,
                                    uint64_t sequence, weivel_c_queued_event **copy);

/** 1 when `event` is one the core queues as a call's refusal. */
int weivel_c_event_is_refusal(const weivel_c_event *event);

/** The `detail` of an event weivel_c_event_is_refusal accepts; NULL when it carries none. */
const char *weivel_c_event_refusal_detail(const weivel_c_event *event);

/** Copies the refusal `event` carries into an empty `refusal`. Answers 0 when memory ran out, `refusal` left empty. */
int weivel_c_refusal_copy(const weivel_c_allocator *allocator, const weivel_c_event *event, weivel_c_refusal *refusal);

/** 1 when `refusal` is NULL, or large enough and empty: what a call may be handed. */
int weivel_c_refusal_is_ready(const weivel_c_refusal *refusal);

#endif
