#include "wrapper.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "events.h"
#include "mutex.h"

#define END_OF(type, field) (offsetof(type, field) + sizeof(((type *)0)->field))
#define FIRST_TABLE_SIZE END_OF(weivel_c_core_table, ledger_row_release)
#define FIRST_OPTIONS_SIZE END_OF(weivel_c_core_options, listener)
#define FIRST_HOOKS_SIZE END_OF(weivel_c_deadline_hooks, data)
#define FIRST_STEP_SIZE END_OF(weivel_c_step, place)
#define KEPT_FAILURES 8
#define DEFECT_TEXT_SIZE 320

/** What a weivel_c_queue_place holds while it waits. */
enum { PLACE_IDLE = 0, PLACE_OF_A_STEP = 1, PLACE_OF_AN_EVENT = 2 };

typedef struct listener_slot {
    uint64_t registration;
    weivel_c_listener listener;
} listener_slot;

/**
 * @implNote `lock` is the layer lock. It guards every field below it, is held across a call and its drain, and is
 * never held while a listener, a step or the sink runs. `dispatching` is 1 exactly while one thread delivers the
 * queue, and the queue is empty whenever it is 0, since whoever adds to it either finds a dispatch running or
 * becomes it under the same hold. `core` is NULL once destroyed.
 */
struct weivel_c_core {
    weivel_c_core_table table;
    weivel_c_allocator allocator;
    weivel_c_error_sink sink;
    weivel_c_mutex lock;
    weivel_core *core;
    weivel_event buffer;
    weivel_c_deadline_hooks hooks;
    listener_slot *listeners;
    uint32_t listener_count;
    uint32_t listener_capacity;
    uint64_t last_registration;
    weivel_c_queue_place *first;
    weivel_c_queue_place *last;
    uint32_t dispatching;
    uint64_t sequence;
};

/**
 * The failures one call met under the layer lock, told to the sink once the lock is let go, because the sink may
 * call into the core.
 */
typedef struct call_notes {
    weivel_c_failure failures[KEPT_FAILURES];
    uint32_t count;
    uint32_t untold;
    char defect_text[DEFECT_TEXT_SIZE];
} call_notes;

static void *allocate_plainly(void *data, size_t size) {
    (void)data;
    return malloc(size);
}

static void release_plainly(void *data, void *memory) {
    (void)data;
    free(memory);
}

static weivel_c_status write_to_standard_error(void *data, const weivel_c_failure *failure) {
    (void)data;
    fprintf(stderr, "weivel: %s (failure kind %u, status %u)\n", failure->text, failure->kind, failure->status);
    return WEIVEL_C_STATUS_OK;
}

void weivel_c_core_report(weivel_c_core *core, const weivel_c_failure *failure) {
    weivel_c_status told = core->sink.report(core->sink.data, failure);
    if (told != WEIVEL_C_STATUS_OK) {
        fprintf(stderr, "weivel: the error sink answered status %u and so was not told: %s\n", (unsigned)told,
                failure->text);
    }
}

static weivel_c_failure failure_of(uint32_t kind, weivel_c_status status, const weivel_c_event *event,
                                   const char *text) {
    weivel_c_failure failure = WEIVEL_INIT(weivel_c_failure);
    failure.kind = kind;
    failure.status = (uint32_t)status;
    failure.event_kind = event == NULL ? (uint32_t)WEIVEL_EVENT_NONE : event->kind;
    failure.sequence = event == NULL ? 0 : event->sequence;
    failure.text = text;
    return failure;
}

static void begin_notes(call_notes *notes) {
    notes->count = 0;
    notes->untold = 0;
}

static void note(call_notes *notes, weivel_c_failure failure) {
    if (notes->count == KEPT_FAILURES) {
        notes->untold++;
        return;
    }
    notes->failures[notes->count++] = failure;
}

static void tell_notes(weivel_c_core *core, const call_notes *notes) {
    uint32_t index;
    for (index = 0; index < notes->count; index++) {
        weivel_c_core_report(core, &notes->failures[index]);
    }
    if (notes->untold > 0) {
        weivel_c_failure failure = failure_of(WEIVEL_C_FAILURE_NOT_TOLD, WEIVEL_C_STATUS_FAILED, NULL,
                                              "one call met more failures than the layer keeps for it");
        failure.sequence = notes->untold;
        weivel_c_core_report(core, &failure);
    }
}

static void note_defect(call_notes *notes, const char *what, const char *detail) {
    snprintf(notes->defect_text, sizeof notes->defect_text, "%s: %s", what,
             detail == NULL ? "the core queued no refusal saying why" : detail);
    note(notes, failure_of(WEIVEL_C_FAILURE_LAYER_DEFECT, WEIVEL_C_STATUS_LAYER_DEFECT, NULL, notes->defect_text));
}

static void append(weivel_c_core *core, weivel_c_queue_place *place, uint32_t holding) {
    place->next = NULL;
    place->waiting = holding;
    if (core->last == NULL) {
        core->first = place;
    } else {
        core->last->next = place;
    }
    core->last = place;
}

static weivel_c_queue_place *detach_first(weivel_c_core *core) {
    weivel_c_queue_place *place = core->first;
    if (place != NULL) {
        core->first = place->next;
        core->last = core->first == NULL ? NULL : core->last;
        place->next = NULL;
    }
    return place;
}

static weivel_c_step *step_at(weivel_c_queue_place *place) {
    return (weivel_c_step *)(void *)((char *)place - offsetof(weivel_c_step, place));
}

static weivel_c_queued_event *event_at(weivel_c_queue_place *place) {
    return (weivel_c_queued_event *)(void *)((char *)place - offsetof(weivel_c_queued_event, place));
}

static void release_memory(weivel_c_core *core, void *memory) {
    core->allocator.release(core->allocator.data, memory);
}

/** Takes the next entry for the running dispatch, or ends that dispatch when the queue is empty. */
static weivel_c_queue_place *take_next(weivel_c_core *core, uint32_t *holding) {
    weivel_c_queue_place *place;
    weivel_c_mutex_lock(&core->lock);
    place = detach_first(core);
    if (place == NULL) {
        core->dispatching = 0;
    } else {
        *holding = place->waiting;
        place->waiting = PLACE_IDLE;
    }
    weivel_c_mutex_unlock(&core->lock);
    return place;
}

static int next_listener(weivel_c_core *core, uint64_t after, listener_slot *found) {
    uint32_t index;
    int has = 0;
    weivel_c_mutex_lock(&core->lock);
    for (index = 0; core->core != NULL && index < core->listener_count && !has; index++) {
        if (core->listeners[index].registration > after) {
            *found = core->listeners[index];
            has = 1;
        }
    }
    weivel_c_mutex_unlock(&core->lock);
    return has;
}

static void hear_event(weivel_c_core *core, const weivel_c_event *event) {
    listener_slot slot;
    uint64_t heard = 0;
    while (next_listener(core, heard, &slot)) {
        weivel_c_status status = slot.listener.hear(slot.listener.data, core, event);
        heard = slot.registration;
        if (status != WEIVEL_C_STATUS_OK) {
            weivel_c_failure failure = failure_of(WEIVEL_C_FAILURE_LISTENER, status, event,
                                                  "a listener answered a failure for an event");
            weivel_c_core_report(core, &failure);
        }
    }
}

/** @implNote Nothing reads `step` once `run` is called: it may free the step or queue it again. */
static void run_step(weivel_c_core *core, weivel_c_step *step) {
    weivel_c_status status = step->run(step->data, core);
    if (status != WEIVEL_C_STATUS_OK) {
        weivel_c_failure failure =
            failure_of(WEIVEL_C_FAILURE_STEP, status, NULL, "a deferred step answered a failure");
        weivel_c_core_report(core, &failure);
    }
}

/** Delivers the queue until it is empty. Only the thread that set `dispatching` calls it, without the lock. */
static void dispatch(weivel_c_core *core) {
    uint32_t holding = PLACE_IDLE;
    weivel_c_queue_place *place = take_next(core, &holding);
    while (place != NULL) {
        if (holding == PLACE_OF_A_STEP) {
            run_step(core, step_at(place));
        } else {
            hear_event(core, &event_at(place)->event);
            release_memory(core, event_at(place));
        }
        place = take_next(core, &holding);
    }
}

static weivel_c_status enter(weivel_c_core *core) {
    weivel_c_mutex_lock(&core->lock);
    if (core->core != NULL) {
        return WEIVEL_C_STATUS_OK;
    }
    weivel_c_mutex_unlock(&core->lock);
    return WEIVEL_C_STATUS_DESTROYED;
}

/** Lets the lock go, tells the sink what the call met, and dispatches unless a dispatch is already running. */
static void leave(weivel_c_core *core, const call_notes *notes) {
    int claimed = core->first != NULL && !core->dispatching;
    if (claimed) {
        core->dispatching = 1;
    }
    weivel_c_mutex_unlock(&core->lock);
    tell_notes(core, notes);
    if (claimed) {
        dispatch(core);
    }
}

static void empty_buffer(weivel_c_core *core) {
    memset(&core->buffer, 0, sizeof core->buffer);
    core->buffer.size = (uint32_t)sizeof core->buffer;
}

static void announce_deadline(weivel_c_core *core, call_notes *notes) {
    const weivel_deadline_event *deadline = &core->buffer.data.deadline;
    weivel_c_status status;
    if (core->buffer.kind != WEIVEL_EVENT_DEADLINE || core->hooks.announced == NULL) {
        return;
    }
    status = core->hooks.announced(core->hooks.data, deadline->at, deadline->set);
    if (status != WEIVEL_C_STATUS_OK) {
        note(notes, failure_of(WEIVEL_C_FAILURE_DEADLINE_HOOK, status, NULL,
                               "the deadline hook answered a failure for an announced deadline"));
    }
}

static void keep_copy(weivel_c_core *core, weivel_c_queued_event *copy, const weivel_c_event **own_refusal) {
    append(core, &copy->place, PLACE_OF_AN_EVENT);
    if (*own_refusal == NULL && weivel_c_event_is_refusal(&copy->event)) {
        *own_refusal = &copy->event;
    }
}

static void note_uncopied(weivel_c_core *core, call_notes *notes) {
    weivel_c_event dropped;
    dropped.kind = core->buffer.kind;
    dropped.sequence = core->sequence;
    note(notes, failure_of(WEIVEL_C_FAILURE_EVENT_NOT_COPIED, WEIVEL_C_STATUS_OUT_OF_MEMORY, &dropped,
                           "an event could not be copied and was dropped"));
}

/** Drains one event: copied, announced when it is a deadline, and released whatever the copy did. */
static int drain_one(weivel_c_core *core, uint32_t *waiting, call_notes *notes, const weivel_c_event **own_refusal) {
    weivel_c_queued_event *copy = NULL;
    weivel_c_copied copied;
    weivel_status answered;
    empty_buffer(core);
    answered = core->table.next_event(core->core, &core->buffer, waiting);
    if (answered == WEIVEL_STATUS_NONE) {
        return 0;
    }
    if (answered != WEIVEL_STATUS_OK) {
        note_defect(notes, "the core refused the layer's event struct, so the rest of its queue was not drained",
                    "weivel_next_event answered no event");
        return 0;
    }
    core->sequence++;
    copied = weivel_c_event_copy(&core->allocator, &core->buffer, core->sequence, &copy);
    announce_deadline(core, notes);
    if (copied == WEIVEL_C_COPY_FAILED) {
        note_uncopied(core, notes);
    }
    if (core->table.event_release(&core->buffer) != WEIVEL_STATUS_OK) {
        note_defect(notes, "the core refused to release an event the layer drained", "weivel_event_release failed");
    }
    if (copied == WEIVEL_C_COPIED) {
        keep_copy(core, copy, own_refusal);
    }
    return 1;
}

static void drain(weivel_c_core *core, uint32_t waiting, call_notes *notes, const weivel_c_event **own_refusal) {
    while (waiting > 0 && drain_one(core, &waiting, notes, own_refusal)) {
    }
}

/** Hands the core a deadline the hook says fell due, and drains what that queues. Answers 1 when it advanced. */
static int advance_when_due(weivel_c_core *core, call_notes *notes) {
    const weivel_c_event *unused = NULL;
    int64_t now = 0;
    uint32_t due = 0;
    uint32_t waiting = 0;
    weivel_c_status asked;
    if (core->hooks.is_due == NULL) {
        return 0;
    }
    asked = core->hooks.is_due(core->hooks.data, &now, &due);
    if (asked != WEIVEL_C_STATUS_OK) {
        note(notes, failure_of(WEIVEL_C_FAILURE_DEADLINE_HOOK, asked, NULL,
                               "the deadline hook answered a failure when asked whether a deadline is due"));
        return 0;
    }
    if (!due) {
        return 0;
    }
    if (core->table.advance(core->core, now, &waiting) == WEIVEL_STATUS_MALFORMED) {
        note_defect(notes, "the core called the advance made for a due deadline malformed", NULL);
    }
    drain(core, waiting, notes, &unused);
    return 1;
}

static void hand_refusal(weivel_c_core *core, const weivel_c_event *own_refusal, weivel_c_refusal *refusal,
                         call_notes *notes) {
    if (own_refusal == NULL || refusal == NULL) {
        return;
    }
    if (!weivel_c_refusal_copy(&core->allocator, own_refusal, refusal)) {
        note(notes, failure_of(WEIVEL_C_FAILURE_REFUSAL_NOT_COPIED, WEIVEL_C_STATUS_OUT_OF_MEMORY, own_refusal,
                               "a refusal could not be copied for the caller of the call it refused"));
    }
}

static weivel_c_status answer_of(weivel_c_core *core, weivel_status answered, weivel_c_call_parts parts,
                                 const weivel_c_event *own_refusal, weivel_c_refusal *refusal, call_notes *notes) {
    switch (answered) {
    case WEIVEL_STATUS_OK:
        return WEIVEL_C_STATUS_OK;
    case WEIVEL_STATUS_NONE:
        return WEIVEL_C_STATUS_NONE;
    case WEIVEL_STATUS_OUT_OF_MEMORY:
        return WEIVEL_C_STATUS_OUT_OF_MEMORY;
    case WEIVEL_STATUS_REFUSED:
        hand_refusal(core, own_refusal, refusal, notes);
        return WEIVEL_C_STATUS_REFUSED;
    case WEIVEL_STATUS_MALFORMED:
        if (parts == WEIVEL_C_CALL_CARRIES_A_CALLERS_STRUCT) {
            hand_refusal(core, own_refusal, refusal, notes);
            return WEIVEL_C_STATUS_MISUSE;
        }
        note_defect(notes, "the core called a call the layer built malformed",
                    own_refusal == NULL ? NULL : weivel_c_event_refusal_detail(own_refusal));
        return WEIVEL_C_STATUS_LAYER_DEFECT;
    default:
        return WEIVEL_C_STATUS_FAILED;
    }
}

weivel_c_status weivel_c_core_invoke(weivel_c_core *core, weivel_c_core_call call, void *arguments,
                                     weivel_c_call_parts parts, weivel_c_refusal *refusal) {
    const weivel_c_event *own_refusal = NULL;
    call_notes notes;
    uint32_t waiting = 0;
    weivel_status answered;
    weivel_c_status status;
    if (core == NULL || !weivel_c_refusal_is_ready(refusal)) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    begin_notes(&notes);
    status = enter(core);
    if (status == WEIVEL_C_STATUS_OK && advance_when_due(core, &notes)) {
        leave(core, &notes);
        begin_notes(&notes);
        status = enter(core);
    }
    if (status != WEIVEL_C_STATUS_OK) {
        return status;
    }
    answered = call(&core->table, core->core, arguments, &waiting);
    drain(core, waiting, &notes, &own_refusal);
    status = answer_of(core, answered, parts, own_refusal, refusal, &notes);
    leave(core, &notes);
    return status;
}

weivel_c_status weivel_c_core_invoke_release(weivel_c_core *core, weivel_c_core_release release, void *answer) {
    if (core == NULL || answer == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    return release(&core->table, answer) == WEIVEL_STATUS_OK ? WEIVEL_C_STATUS_OK : WEIVEL_C_STATUS_MISUSE;
}

void weivel_c_core_replace_allocator(weivel_c_core *core, const weivel_c_allocator *allocator) {
    weivel_c_mutex_lock(&core->lock);
    core->allocator = *allocator;
    weivel_c_mutex_unlock(&core->lock);
}

static int has_room_for_a_listener(weivel_c_core *core) {
    uint32_t capacity = core->listener_capacity == 0 ? 4 : core->listener_capacity * 2;
    listener_slot *grown;
    if (core->listener_count < core->listener_capacity) {
        return 1;
    }
    grown = core->allocator.allocate(core->allocator.data, capacity * sizeof *grown);
    if (grown == NULL) {
        return 0;
    }
    if (core->listener_count > 0) {
        memcpy(grown, core->listeners, core->listener_count * sizeof *grown);
    }
    if (core->listeners != NULL) {
        release_memory(core, core->listeners);
    }
    core->listeners = grown;
    core->listener_capacity = capacity;
    return 1;
}

static weivel_c_status add_listener(weivel_c_core *core, const weivel_c_listener *listener, uint64_t *registration) {
    listener_slot *slot;
    if (!has_room_for_a_listener(core)) {
        return WEIVEL_C_STATUS_OUT_OF_MEMORY;
    }
    slot = &core->listeners[core->listener_count++];
    slot->registration = ++core->last_registration;
    slot->listener = *listener;
    *registration = slot->registration;
    return WEIVEL_C_STATUS_OK;
}

weivel_c_status weivel_c_core_add_listener(weivel_c_core *core, const weivel_c_listener *listener,
                                           uint64_t *registration) {
    weivel_c_status status;
    if (core == NULL || listener == NULL || listener->hear == NULL || registration == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    status = enter(core);
    if (status != WEIVEL_C_STATUS_OK) {
        return status;
    }
    status = add_listener(core, listener, registration);
    weivel_c_mutex_unlock(&core->lock);
    return status;
}

static weivel_c_status remove_listener(weivel_c_core *core, uint64_t registration) {
    uint32_t index;
    for (index = 0; index < core->listener_count; index++) {
        if (core->listeners[index].registration == registration) {
            memmove(&core->listeners[index], &core->listeners[index + 1],
                    (core->listener_count - index - 1) * sizeof core->listeners[index]);
            core->listener_count--;
            return WEIVEL_C_STATUS_OK;
        }
    }
    return WEIVEL_C_STATUS_MISUSE;
}

weivel_c_status weivel_c_core_remove_listener(weivel_c_core *core, uint64_t registration) {
    weivel_c_status status;
    if (core == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    status = enter(core);
    if (status != WEIVEL_C_STATUS_OK) {
        return status;
    }
    status = remove_listener(core, registration);
    weivel_c_mutex_unlock(&core->lock);
    return status;
}

weivel_c_status weivel_c_core_set_deadline_hooks(weivel_c_core *core, const weivel_c_deadline_hooks *hooks) {
    weivel_c_status status;
    if (core == NULL || (hooks != NULL && hooks->size < FIRST_HOOKS_SIZE)) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    status = enter(core);
    if (status != WEIVEL_C_STATUS_OK) {
        return status;
    }
    memset(&core->hooks, 0, sizeof core->hooks);
    if (hooks != NULL) {
        core->hooks = *hooks;
    }
    weivel_c_mutex_unlock(&core->lock);
    return WEIVEL_C_STATUS_OK;
}

/**
 * @implNote A step is a queue entry like an event, whoever defers it, so a step and a listener never run at once:
 * with no dispatch running the queue is empty, and `leave` makes the caller the dispatcher of this one entry.
 */
weivel_c_status weivel_c_core_defer(weivel_c_core *core, weivel_c_step *step) {
    call_notes notes;
    weivel_c_status status;
    if (core == NULL || step == NULL || step->size < FIRST_STEP_SIZE || step->run == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    status = enter(core);
    if (status != WEIVEL_C_STATUS_OK) {
        return status;
    }
    if (step->place.waiting != PLACE_IDLE) {
        weivel_c_mutex_unlock(&core->lock);
        return WEIVEL_C_STATUS_MISUSE;
    }
    begin_notes(&notes);
    append(core, &step->place, PLACE_OF_A_STEP);
    leave(core, &notes);
    return WEIVEL_C_STATUS_OK;
}

static int options_are_usable(const weivel_c_core_options *options) {
    return options != NULL && options->size >= FIRST_OPTIONS_SIZE && options->table != NULL &&
           options->table->size >= FIRST_TABLE_SIZE && options->table->library != NULL;
}

static void adopt_options(weivel_c_core *core, const weivel_c_core_options *options) {
    core->table = *options->table;
    core->table.size = (uint32_t)sizeof core->table;
    core->allocator = (weivel_c_allocator){allocate_plainly, release_plainly, NULL};
    core->sink = options->error_sink;
    if (core->sink.report == NULL) {
        core->sink = (weivel_c_error_sink){write_to_standard_error, NULL};
    }
}

static void discard(weivel_c_core *core) {
    if (core->listeners != NULL) {
        release_memory(core, core->listeners);
    }
    weivel_c_mutex_end(&core->lock);
    free(core);
}

static weivel_c_status status_of_creation(weivel_status answered) {
    if (answered == WEIVEL_STATUS_OK) {
        return WEIVEL_C_STATUS_OK;
    }
    return answered == WEIVEL_STATUS_OUT_OF_MEMORY ? WEIVEL_C_STATUS_OUT_OF_MEMORY : WEIVEL_C_STATUS_MISUSE;
}

/** Creates the core, then drains and dispatches what its creation queued, as a call does. */
static weivel_c_status start(weivel_c_core *core, const weivel_c_core_options *options) {
    const weivel_c_event *unused = NULL;
    call_notes notes;
    uint32_t waiting = 0;
    uint64_t registration = 0;
    weivel_c_status status = WEIVEL_C_STATUS_OK;
    if (options->listener.hear != NULL) {
        status = add_listener(core, &options->listener, &registration);
    }
    if (status == WEIVEL_C_STATUS_OK) {
        status = status_of_creation(core->table.core_create(options->core_options, &core->core, &waiting));
    }
    if (status != WEIVEL_C_STATUS_OK) {
        return status;
    }
    begin_notes(&notes);
    weivel_c_mutex_lock(&core->lock);
    drain(core, waiting, &notes, &unused);
    leave(core, &notes);
    return WEIVEL_C_STATUS_OK;
}

weivel_c_status weivel_c_core_create(const weivel_c_core_options *options, weivel_c_core **core) {
    weivel_c_core *created;
    weivel_c_status status;
    if (core == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    *core = NULL;
    if (!options_are_usable(options)) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    created = calloc(1, sizeof *created);
    if (created == NULL) {
        return WEIVEL_C_STATUS_OUT_OF_MEMORY;
    }
    adopt_options(created, options);
    if (!weivel_c_mutex_start(&created->lock)) {
        free(created);
        return WEIVEL_C_STATUS_OUT_OF_MEMORY;
    }
    status = start(created, options);
    if (status != WEIVEL_C_STATUS_OK) {
        discard(created);
        return status;
    }
    *core = created;
    return WEIVEL_C_STATUS_OK;
}

static void drop_what_waits(weivel_c_core *core) {
    weivel_c_queue_place *place = detach_first(core);
    while (place != NULL) {
        if (place->waiting == PLACE_OF_AN_EVENT) {
            release_memory(core, event_at(place));
        } else {
            place->waiting = PLACE_IDLE;
        }
        place = detach_first(core);
    }
}

weivel_c_status weivel_c_core_destroy(weivel_c_core *core) {
    if (core == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    weivel_c_mutex_lock(&core->lock);
    if (core->core != NULL) {
        drop_what_waits(core);
        core->table.core_destroy(core->core);
        core->core = NULL;
        core->listener_count = 0;
        memset(&core->hooks, 0, sizeof core->hooks);
    }
    weivel_c_mutex_unlock(&core->lock);
    return WEIVEL_C_STATUS_OK;
}

weivel_c_status weivel_c_core_free(weivel_c_core *core) {
    int idle;
    if (core == NULL) {
        return WEIVEL_C_STATUS_OK;
    }
    weivel_c_mutex_lock(&core->lock);
    idle = core->core == NULL && !core->dispatching;
    weivel_c_mutex_unlock(&core->lock);
    if (!idle) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    discard(core);
    return WEIVEL_C_STATUS_OK;
}
