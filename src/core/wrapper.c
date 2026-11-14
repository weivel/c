#include "wrapper.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "events.h"
#include "load.h"
#include "mutex.h"

#define END_OF(type, field) (offsetof(type, field) + sizeof(((type *)0)->field))
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
 * never held while a listener, a step, a hook or the sink runs. `dispatching` is 1 exactly while one thread delivers
 * the queue. Whoever adds to the queue either finds a dispatch running or becomes it before it returns, so with no
 * dispatch running the queue holds only what a thread still on its way out drained: one telling the `follow` hook
 * first, whose entries the next thread to leave delivers if it gets there sooner. `core` is NULL once destroyed.
 *
 * `deadline_at` and `deadline_set` are the latest announcement drained, and `deadline_serial` counts every reason to
 * tell the `follow` hook again: an announcement, new hooks, a request to follow once more. `following` is 1 exactly
 * while one thread calls `follow`, and that thread does not stop until `followed_serial` has caught up, so the last
 * call `follow` receives always carries the latest announcement, whichever thread drained it.
 *
 * Every event a call drains along with an announcement is fenced with the serial that announcement left, and the
 * dispatch stops at the first event whose fence `followed_serial` has not reached, ending until the thread telling
 * `follow` has returned and delivers the rest itself. So no event of a call is dispatched before the timer is armed
 * for that call's announcement, while a thread telling `follow` again with nothing drained keeps nobody waiting.
 * Fences only grow along the queue, so `highest_fence` above `followed_serial` says some fenced event still waits.
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
    int64_t deadline_at;
    uint32_t deadline_set;
    uint32_t following;
    uint64_t deadline_serial;
    uint64_t followed_serial;
    uint64_t highest_fence;
};

/** One call of the `follow` hook, taken under the layer lock and made outside it. */
typedef struct following_turn {
    weivel_c_status (*follow)(void *data, int64_t at, uint32_t set);
    void *data;
    int64_t at;
    uint32_t set;
    uint64_t serial;
} following_turn;

/**
 * The failures one call met under the layer lock, told to the sink once the lock is let go, because the sink may
 * call into the core.
 */
typedef struct call_notes {
    weivel_c_failure failures[KEPT_FAILURES];
    uint32_t count;
    uint32_t untold;
    char defect_texts[KEPT_FAILURES][DEFECT_TEXT_SIZE];
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

/** @implNote Each kept failure has a text of its own, so a second defect of one call leaves the first one's alone. */
static void note_defect(call_notes *notes, const char *what, const char *detail) {
    char *text = notes->defect_texts[notes->count < KEPT_FAILURES ? notes->count : 0];
    if (notes->count < KEPT_FAILURES) {
        snprintf(text, DEFECT_TEXT_SIZE, "%s: %s", what,
                 detail == NULL ? "the core queued no refusal saying why" : detail);
    }
    note(notes, failure_of(WEIVEL_C_FAILURE_LAYER_DEFECT, WEIVEL_C_STATUS_LAYER_DEFECT, NULL, text));
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

/** Under the lock: 1 when the first entry is an event of a call whose announcement `follow` has not returned with. */
static int first_is_fenced(const weivel_c_core *core) {
    const weivel_c_queue_place *first = core->first;
    if (first == NULL || first->waiting != PLACE_OF_AN_EVENT) {
        return 0;
    }
    return ((const weivel_c_queued_event *)(const void *)first)->fence > core->followed_serial;
}

/** Under the lock: 1 while an event fenced behind an announcement `follow` has not returned with still waits. */
static int has_fenced_events(const weivel_c_core *core) {
    return core->highest_fence > core->followed_serial;
}

/**
 * Takes the next entry for the running dispatch, or ends that dispatch when the queue is empty or its first entry is
 * fenced, `*fenced` then 1 and the thread telling `follow` delivering the rest.
 */
static weivel_c_queue_place *take_next(weivel_c_core *core, uint32_t *holding, int *fenced) {
    weivel_c_queue_place *place = NULL;
    weivel_c_mutex_lock(&core->lock);
    *fenced = first_is_fenced(core);
    if (!*fenced) {
        place = detach_first(core);
    }
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

/**
 * Delivers the queue until it is empty or reaches a fence. Only the thread that set `dispatching` calls it, without
 * the lock. Answers 1 when `watched`, queued before the call, is no longer waiting for another thread: it took it,
 * or it emptied the queue, as a destroy made inside the dispatch does by dropping it.
 */
static int dispatch(weivel_c_core *core, const weivel_c_queue_place *watched) {
    uint32_t holding = PLACE_IDLE;
    int took_watched = 0;
    int fenced = 0;
    weivel_c_queue_place *place = take_next(core, &holding, &fenced);
    while (place != NULL) {
        took_watched |= place == watched;
        if (holding == PLACE_OF_A_STEP) {
            run_step(core, step_at(place));
        } else {
            hear_event(core, &event_at(place)->event);
            release_memory(core, event_at(place));
        }
        place = take_next(core, &holding, &fenced);
    }
    return took_watched || !fenced;
}

static weivel_c_status enter(weivel_c_core *core) {
    weivel_c_mutex_lock(&core->lock);
    if (core->core != NULL) {
        return WEIVEL_C_STATUS_OK;
    }
    weivel_c_mutex_unlock(&core->lock);
    return WEIVEL_C_STATUS_DESTROYED;
}

/** Under the lock: takes the turn to call `follow` when the hook has not heard the latest and nobody is telling it. */
static int take_following_turn(weivel_c_core *core, following_turn *turn) {
    if (core->following || core->followed_serial == core->deadline_serial) {
        return 0;
    }
    if (core->hooks.follow == NULL) {
        core->followed_serial = core->deadline_serial;
        return 0;
    }
    core->following = 1;
    *turn = (following_turn){core->hooks.follow, core->hooks.data, core->deadline_at, core->deadline_set,
                             core->deadline_serial};
    return 1;
}

/**
 * Entered with the lock held and left with it held: tells the `follow` hook the latest announcement, outside the
 * lock, and again for as long as a newer one was drained meanwhile.
 *
 * @implNote Only the thread that took the turn is here, so two calls of `follow` never overlap and never arrive out
 * of order. A thread that drains a newer announcement while the turn is taken leaves it to this loop, which cannot
 * end before it has seen, under the lock, that nothing newer waits.
 */
static void follow_the_deadline(weivel_c_core *core, following_turn *turn) {
    int again = 1;
    while (again) {
        weivel_c_status status;
        weivel_c_mutex_unlock(&core->lock);
        status = turn->follow(turn->data, turn->at, turn->set);
        if (status != WEIVEL_C_STATUS_OK) {
            weivel_c_failure failure = failure_of(WEIVEL_C_FAILURE_DEADLINE_HOOK, status, NULL,
                                                  "the deadline hook answered a failure for an announced deadline");
            weivel_c_core_report(core, &failure);
        }
        weivel_c_mutex_lock(&core->lock);
        core->followed_serial = turn->serial;
        core->following = 0;
        again = take_following_turn(core, turn);
    }
}

/** Under the lock: makes this thread the dispatcher when something it may deliver waits and no dispatch runs. */
static int claim_the_dispatch(weivel_c_core *core) {
    if (core->first == NULL || core->dispatching || first_is_fenced(core)) {
        return 0;
    }
    core->dispatching = 1;
    return 1;
}

/**
 * Tells the `follow` hook of a deadline this hold drained, lets the lock go, tells the sink what the call met, and
 * dispatches unless a dispatch is already running. Answers 1 when this thread dispatched `watched`, which may be NULL.
 *
 * @implNote The dispatch is claimed only once `follow` has returned, under the hold it returns with. A thread that
 * sits in `follow` keeps waiting only the events fenced behind the announcement it tells and those queued after them:
 * any other thread to leave meanwhile delivers what lies ahead of the fence, and this thread, once `follow` has
 * returned, finds the fence passed and delivers the rest.
 */
static int leave(weivel_c_core *core, const call_notes *notes, const weivel_c_queue_place *watched) {
    following_turn turn;
    int claimed;
    if (take_following_turn(core, &turn)) {
        follow_the_deadline(core, &turn);
    }
    claimed = claim_the_dispatch(core);
    weivel_c_mutex_unlock(&core->lock);
    tell_notes(core, notes);
    return claimed && dispatch(core, watched);
}

static void empty_buffer(weivel_c_core *core) {
    memset(&core->buffer, 0, sizeof core->buffer);
    core->buffer.size = (uint32_t)sizeof core->buffer;
}

/** Records a deadline announcement as it is drained, whatever became of its copy. */
static void record_deadline(weivel_c_core *core) {
    const weivel_deadline_event *deadline = &core->buffer.data.deadline;
    if (core->buffer.kind == WEIVEL_EVENT_DEADLINE) {
        core->deadline_at = deadline->at;
        core->deadline_set = deadline->set;
        core->deadline_serial++;
    }
}

static void keep_copy(weivel_c_core *core, weivel_c_queued_event *copy, const weivel_c_event **own_refusal) {
    copy->fence = 0;
    append(core, &copy->place, PLACE_OF_AN_EVENT);
    if (*own_refusal == NULL && weivel_c_event_is_refusal(&copy->event)) {
        *own_refusal = &copy->event;
    }
}

static void note_uncopied(weivel_c_core *core, call_notes *notes, weivel_c_copied copied) {
    weivel_c_event concerned;
    concerned.kind = core->buffer.kind;
    concerned.sequence = core->sequence;
    if (copied == WEIVEL_C_COPY_FAILED) {
        note(notes, failure_of(WEIVEL_C_FAILURE_EVENT_NOT_COPIED, WEIVEL_C_STATUS_OUT_OF_MEMORY, &concerned,
                               "there was no memory for an event, which was dropped"));
    } else if (copied == WEIVEL_C_COPIED_INCOMPLETE) {
        note(notes, failure_of(WEIVEL_C_FAILURE_EVENT_INCOMPLETE, WEIVEL_C_STATUS_OUT_OF_MEMORY, &concerned,
                               "an event's texts or constraints could not be copied, so it is heard without them"));
    }
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
    record_deadline(core);
    note_uncopied(core, notes, copied);
    if (core->table.event_release(&core->buffer) != WEIVEL_STATUS_OK) {
        note_defect(notes, "the core refused to release an event the layer drained", "weivel_event_release failed");
    }
    if (copy != NULL) {
        keep_copy(core, copy, own_refusal);
    }
    return 1;
}

/** Under the lock: fences every event queued after `before`, NULL for the whole queue, with the latest serial. */
static void fence_what_was_drained(weivel_c_core *core, weivel_c_queue_place *before) {
    weivel_c_queue_place *place = before == NULL ? core->first : before->next;
    for (; place != NULL; place = place->next) {
        if (place->waiting == PLACE_OF_AN_EVENT) {
            event_at(place)->fence = core->deadline_serial;
        }
    }
    core->highest_fence = core->deadline_serial;
}

/** Drains a call's events, fencing them all when one of them is an announcement. */
static void drain(weivel_c_core *core, uint32_t waiting, call_notes *notes, const weivel_c_event **own_refusal) {
    weivel_c_queue_place *before = core->last;
    uint64_t serial = core->deadline_serial;
    while (waiting > 0 && drain_one(core, &waiting, notes, own_refusal)) {
    }
    if (core->deadline_serial != serial) {
        fence_what_was_drained(core, before);
    }
}

/**
 * @implNote An advance the core did not take leaves the deadline recorded, so the next call hands the time over
 * again: the sink is told each time, since nothing else would say why a want that is past its time still waits.
 */
static void note_an_advance_not_taken(call_notes *notes, weivel_status answered) {
    if (answered == WEIVEL_STATUS_MALFORMED) {
        note_defect(notes, "the core called the advance made for a due deadline malformed", NULL);
    } else if (answered != WEIVEL_STATUS_OK) {
        weivel_c_status status =
            answered == WEIVEL_STATUS_OUT_OF_MEMORY ? WEIVEL_C_STATUS_OUT_OF_MEMORY : WEIVEL_C_STATUS_FAILED;
        note(notes, failure_of(WEIVEL_C_FAILURE_DUE_ADVANCE, status, NULL,
                               "the core did not take the time of a deadline that fell due, which stays due"));
    }
}

/** Under the lock: hands the core the time `now` when the recorded deadline has fallen due, and drains that. */
static int advance_when_due(weivel_c_core *core, int64_t now, call_notes *notes) {
    const weivel_c_event *unused = NULL;
    uint32_t waiting = 0;
    if (!core->deadline_set || now < core->deadline_at) {
        return 0;
    }
    note_an_advance_not_taken(notes, core->table.advance(core->core, now, &waiting));
    drain(core, waiting, notes, &unused);
    return 1;
}

/**
 * Entered with the lock held and left with it held, or with WEIVEL_C_STATUS_DESTROYED and without it. When a
 * deadline is recorded it reads the clock once, outside the lock, and when the deadline has fallen due it advances
 * the core, drains, dispatches and takes the lock again.
 *
 * @implNote The clock is the caller's function, so the lock is let go around it. The time it answered is still good
 * under the second hold: time only moves on, so a deadline it had reached is due whatever was drained meanwhile.
 */
static weivel_c_status hand_over_a_due_deadline(weivel_c_core *core) {
    int64_t (*read_clock)(void *data) = core->hooks.now;
    void *data = core->hooks.data;
    call_notes notes;
    weivel_c_status status;
    int64_t now;
    if (!core->deadline_set || read_clock == NULL) {
        return WEIVEL_C_STATUS_OK;
    }
    weivel_c_mutex_unlock(&core->lock);
    now = read_clock(data);
    status = enter(core);
    if (status != WEIVEL_C_STATUS_OK) {
        return status;
    }
    begin_notes(&notes);
    if (!advance_when_due(core, now, &notes)) {
        return WEIVEL_C_STATUS_OK;
    }
    leave(core, &notes, NULL);
    return enter(core);
}

static void hand_refusal(weivel_c_core *core, const weivel_c_event *own_refusal, weivel_c_refusal *refusal,
                         call_notes *notes) {
    if (own_refusal == NULL || refusal == NULL) {
        return;
    }
    if (!weivel_c_refusal_copy(&core->allocator, own_refusal, refusal)) {
        note(notes, failure_of(WEIVEL_C_FAILURE_REFUSAL_NOT_COPIED, WEIVEL_C_STATUS_OUT_OF_MEMORY, own_refusal,
                               "a refusal's texts could not be copied for the caller of the call it refused"));
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

/** One call through the wrapper, and what is done around it. */
typedef struct invocation {
    weivel_c_core_call call;
    void *arguments;
    weivel_c_call_parts parts;
    weivel_c_refusal *refusal;
    const weivel_c_answered_step *then;
    int hands_the_time;
} invocation;

/** Under the lock, before the call: the guard of the call's step, whose failure becomes the call's answer. */
static weivel_c_status guard_the_call(const invocation *asked) {
    if (asked->then == NULL || asked->then->guard == NULL) {
        return WEIVEL_C_STATUS_OK;
    }
    return asked->then->guard(asked->then->guard_data);
}

/** Under the lock: runs the step of a call the core answered OK, whose failure becomes the call's answer. */
static weivel_c_status record_the_answer(const invocation *asked, weivel_c_status status) {
    if (status != WEIVEL_C_STATUS_OK || asked->then == NULL || asked->then->record == NULL) {
        return status;
    }
    return asked->then->record(asked->then->data);
}

static int is_usable_step(const weivel_c_step *step) {
    return step->size >= FIRST_STEP_SIZE && step->run != NULL;
}

/** Under the lock: queues the call's deferred step behind its events once everything before answered OK. */
static weivel_c_status queue_the_deferred_step(weivel_c_core *core, const invocation *asked, weivel_c_status status,
                                               int *queued) {
    *queued = 0;
    if (status != WEIVEL_C_STATUS_OK || asked->then == NULL || asked->then->deferred == NULL) {
        return status;
    }
    if (!is_usable_step(asked->then->deferred)) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    if (asked->then->deferred->place.waiting != PLACE_IDLE) {
        return status;
    }
    append(core, &asked->then->deferred->place, PLACE_OF_A_STEP);
    *queued = 1;
    return WEIVEL_C_STATUS_OK;
}

static void tell_whether_the_deferred_step_ran(const invocation *asked, int queued, int dispatched) {
    if (asked->then != NULL && asked->then->deferred_ran != NULL) {
        *asked->then->deferred_ran = (uint32_t)(queued && dispatched);
    }
}

static weivel_c_status invoke(weivel_c_core *core, const invocation *asked) {
    const weivel_c_event *own_refusal = NULL;
    call_notes notes;
    uint32_t waiting = 0;
    weivel_status answered;
    weivel_c_status status;
    int queued;
    tell_whether_the_deferred_step_ran(asked, 0, 0);
    if (core == NULL || !weivel_c_refusal_is_ready(asked->refusal)) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    status = enter(core);
    if (status == WEIVEL_C_STATUS_OK && !asked->hands_the_time) {
        status = hand_over_a_due_deadline(core);
    }
    if (status != WEIVEL_C_STATUS_OK) {
        return status;
    }
    status = guard_the_call(asked);
    if (status != WEIVEL_C_STATUS_OK) {
        weivel_c_mutex_unlock(&core->lock);
        return status;
    }
    begin_notes(&notes);
    answered = asked->call(&core->table, core->core, asked->arguments, &waiting);
    drain(core, waiting, &notes, &own_refusal);
    status = answer_of(core, answered, asked->parts, own_refusal, asked->refusal, &notes);
    status = record_the_answer(asked, status);
    status = queue_the_deferred_step(core, asked, status, &queued);
    tell_whether_the_deferred_step_ran(asked, queued,
                                       leave(core, &notes, queued ? &asked->then->deferred->place : NULL));
    return status;
}

weivel_c_status weivel_c_core_invoke(weivel_c_core *core, weivel_c_core_call call, void *arguments,
                                     weivel_c_call_parts parts, weivel_c_refusal *refusal) {
    invocation asked = {call, arguments, parts, refusal, NULL, 0};
    return invoke(core, &asked);
}

weivel_c_status weivel_c_core_invoke_then(weivel_c_core *core, weivel_c_core_call call, void *arguments,
                                          weivel_c_call_parts parts, weivel_c_refusal *refusal,
                                          const weivel_c_answered_step *then) {
    invocation asked = {call, arguments, parts, refusal, then, 0};
    return invoke(core, &asked);
}

weivel_c_status weivel_c_core_invoke_handing_time(weivel_c_core *core, weivel_c_core_call call, void *arguments,
                                                  weivel_c_refusal *refusal) {
    invocation asked = {call, arguments, WEIVEL_C_CALL_BUILT_BY_THE_LAYER, refusal, NULL, 1};
    return invoke(core, &asked);
}

weivel_c_status weivel_c_core_follow_deadline_again(weivel_c_core *core) {
    call_notes notes;
    weivel_c_status status = core == NULL ? WEIVEL_C_STATUS_MISUSE : enter(core);
    if (status != WEIVEL_C_STATUS_OK) {
        return status;
    }
    begin_notes(&notes);
    core->deadline_serial += core->deadline_serial != 0;
    leave(core, &notes, NULL);
    return WEIVEL_C_STATUS_OK;
}

int weivel_c_core_is_destroyed(weivel_c_core *core) {
    int destroyed;
    weivel_c_mutex_lock(&core->lock);
    destroyed = core->core == NULL;
    weivel_c_mutex_unlock(&core->lock);
    return destroyed;
}

uint64_t weivel_c_core_last_sequence(weivel_c_core *core) {
    uint64_t sequence;
    weivel_c_mutex_lock(&core->lock);
    sequence = core->sequence;
    weivel_c_mutex_unlock(&core->lock);
    return sequence;
}

int weivel_c_core_is_idle(weivel_c_core *core) {
    int idle;
    weivel_c_mutex_lock(&core->lock);
    idle = core->core == NULL && !core->dispatching && !core->following;
    weivel_c_mutex_unlock(&core->lock);
    return idle;
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

static int hooks_are_usable(const weivel_c_deadline_hooks *hooks) {
    return hooks == NULL || hooks->size >= FIRST_HOOKS_SIZE;
}

static int has_hooks(const weivel_c_core *core) {
    return core->hooks.now != NULL || core->hooks.follow != NULL;
}

static int has_these_hooks(const weivel_c_core *core, const weivel_c_deadline_hooks *hooks) {
    return has_hooks(core) && core->hooks.now == hooks->now && core->hooks.follow == hooks->follow &&
           core->hooks.data == hooks->data;
}

/**
 * Entered with the lock held and left without it: puts `hooks` in place, none for NULL.
 *
 * @implNote Hooks set while a deadline is recorded are told it at once: the serial moves, so this call's own `leave`
 * takes the turn, or the thread that holds it goes round once more with the new hooks.
 */
static void put_hooks(weivel_c_core *core, const weivel_c_deadline_hooks *hooks) {
    call_notes notes;
    memset(&core->hooks, 0, sizeof core->hooks);
    if (hooks != NULL) {
        core->hooks = *hooks;
    }
    core->deadline_serial += core->deadline_serial != 0;
    begin_notes(&notes);
    leave(core, &notes, NULL);
}

weivel_c_status weivel_c_core_set_deadline_hooks(weivel_c_core *core, const weivel_c_deadline_hooks *hooks) {
    weivel_c_status status = core == NULL || !hooks_are_usable(hooks) ? WEIVEL_C_STATUS_MISUSE : enter(core);
    if (status == WEIVEL_C_STATUS_OK) {
        put_hooks(core, hooks);
    }
    return status;
}

weivel_c_status weivel_c_core_install_deadline_hooks(weivel_c_core *core, const weivel_c_deadline_hooks *hooks) {
    weivel_c_status status;
    if (core == NULL || hooks == NULL || !hooks_are_usable(hooks)) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    status = enter(core);
    if (status != WEIVEL_C_STATUS_OK) {
        return status;
    }
    if (has_hooks(core)) {
        weivel_c_mutex_unlock(&core->lock);
        return WEIVEL_C_STATUS_MISUSE;
    }
    put_hooks(core, hooks);
    return WEIVEL_C_STATUS_OK;
}

weivel_c_status weivel_c_core_remove_deadline_hooks(weivel_c_core *core, const weivel_c_deadline_hooks *hooks) {
    weivel_c_status status = core == NULL || hooks == NULL ? WEIVEL_C_STATUS_MISUSE : enter(core);
    if (status != WEIVEL_C_STATUS_OK) {
        return status;
    }
    if (!has_these_hooks(core, hooks)) {
        weivel_c_mutex_unlock(&core->lock);
        return WEIVEL_C_STATUS_NONE;
    }
    put_hooks(core, NULL);
    return WEIVEL_C_STATUS_OK;
}

/**
 * @implNote A step is a queue entry like an event, whoever defers it, so a step and a listener never run at once:
 * with no dispatch running `leave` makes the caller the dispatcher, of this one entry and of whatever a thread still
 * telling the `follow` hook drained ahead of it.
 */
weivel_c_status weivel_c_core_defer(weivel_c_core *core, weivel_c_step *step, uint32_t *dispatched_here) {
    call_notes notes;
    weivel_c_status status;
    if (dispatched_here != NULL) {
        *dispatched_here = 0;
    }
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
    if (leave(core, &notes, &step->place) && dispatched_here != NULL) {
        *dispatched_here = 1;
    }
    return WEIVEL_C_STATUS_OK;
}

/**
 * @implNote The dispatch is claimed under the same hold that found none running, so no other thread can take the step
 * between. The `follow` hook is left to the next thread to leave a call, the step's own calls among them.
 */
weivel_c_status weivel_c_core_run_as_the_dispatcher(weivel_c_core *core, weivel_c_step *step, uint32_t *ran) {
    weivel_c_status status;
    if (ran != NULL) {
        *ran = 0;
    }
    if (core == NULL || ran == NULL || step == NULL || step->size < FIRST_STEP_SIZE || step->run == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    status = enter(core);
    if (status != WEIVEL_C_STATUS_OK) {
        return status;
    }
    if (core->dispatching || has_fenced_events(core) || step->place.waiting != PLACE_IDLE) {
        status = core->dispatching || has_fenced_events(core) ? WEIVEL_C_STATUS_OK : WEIVEL_C_STATUS_MISUSE;
        weivel_c_mutex_unlock(&core->lock);
        return status;
    }
    append(core, &step->place, PLACE_OF_A_STEP);
    core->dispatching = 1;
    weivel_c_mutex_unlock(&core->lock);
    dispatch(core, NULL);
    *ran = 1;
    return WEIVEL_C_STATUS_OK;
}

static int options_are_usable(const weivel_c_core_options *options) {

    return options != NULL && options->size >= FIRST_OPTIONS_SIZE && weivel_c_core_table_is_large_enough(options->table) &&
           options->table->library != NULL;
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
    leave(core, &notes, NULL);
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
        core->deadline_set = 0;
        memset(&core->hooks, 0, sizeof core->hooks);
    }
    weivel_c_mutex_unlock(&core->lock);
    return WEIVEL_C_STATUS_OK;
}

weivel_c_status weivel_c_core_free(weivel_c_core *core) {
    if (core == NULL) {
        return WEIVEL_C_STATUS_OK;
    }
    if (!weivel_c_core_is_idle(core)) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    discard(core);
    return WEIVEL_C_STATUS_OK;
}
