#include "contexts.h"

#include <string.h>

#include "../core/calls.h"

static uint32_t handle_kind_of(uint32_t kind) {
    switch (kind) {
    case WEIVEL_C_RECORD_SERVICE:
        return WEIVEL_C_HANDLE_SERVICE_REGISTRATION;
    case WEIVEL_C_RECORD_IMPLEMENTATION:
        return WEIVEL_C_HANDLE_CAPABILITY_IMPLEMENTATION;
    case WEIVEL_C_RECORD_WANT:
        return WEIVEL_C_HANDLE_WANT;
    default:
        return WEIVEL_C_HANDLE_WATCH;
    }
}

static weivel_c_record **list_of(weivel_c_record *record) {
    return record->context != NULL ? &record->context->records : &record->contexts->orphans;
}

static void link_record(weivel_c_record *record) {
    weivel_c_record **head = list_of(record);
    record->previous = NULL;
    record->next = *head;
    if (*head != NULL) {
        (*head)->previous = record;
    }
    *head = record;
}

static void unlink_record(weivel_c_record *record) {
    if (record->previous != NULL) {
        record->previous->next = record->next;
    } else {
        *list_of(record) = record->next;
    }
    if (record->next != NULL) {
        record->next->previous = record->previous;
    }
    record->previous = NULL;
    record->next = NULL;
}

/** Frees a record and gives back the holding reservation a want still holds. */
static void release_record(weivel_c_record *record) {
    weivel_c_contexts *contexts = record->contexts;
    if (record->holding_reserved) {
        weivel_c_handles_unreserve(contexts->handles);
    }
    contexts->allocator.release(contexts->allocator.data, record);
}

/**
 * Ends the record's entry while the table still keeps that very record under its handle.
 *
 * @implNote The core may issue a forgotten plugin's handle again, so an entry under the same handle can be another's.
 */
static void end_entry_of(weivel_c_record *record) {
    weivel_c_handles *handles = record->contexts->handles;
    weivel_c_handle_entry entry = WEIVEL_INIT(weivel_c_handle_entry);
    if (record->handle != 0 && weivel_c_handles_find(handles, record->handle, &entry) == WEIVEL_C_STATUS_OK &&
        entry.object == record) {
        weivel_c_handles_end(handles, record->handle, NULL);
    }
}

/** Under the lock of the contexts: takes a record out of the table and out of its list. */
static void drop_record(weivel_c_record *record) {
    end_entry_of(record);
    unlink_record(record);
    record->contexts->record_count--;
}

/** The step a retired record carries: it runs in the dispatch, behind every event drained before it was queued. */
static weivel_c_status end_retired_record(void *data, weivel_c_core *core) {
    weivel_c_record *record = data;
    weivel_c_contexts *contexts = record->contexts;
    (void)core;
    weivel_c_mutex_lock(&contexts->lock);
    drop_record(record);
    weivel_c_mutex_unlock(&contexts->lock);
    release_record(record);
    return WEIVEL_C_STATUS_OK;
}

static weivel_c_record *make_record(weivel_c_context *context, uint32_t kind, const char *id) {
    weivel_c_contexts *contexts = context->contexts;
    size_t id_size = strlen(id) + 1;
    weivel_c_record *record = contexts->allocator.allocate(contexts->allocator.data, sizeof *record + id_size);
    if (record == NULL) {
        return NULL;
    }
    memset(record, 0, sizeof *record);
    record->step.size = (uint32_t)sizeof record->step;
    record->step.run = end_retired_record;
    record->step.data = record;
    record->contexts = context->contexts;
    record->context = context;
    record->plugin = context->plugin;
    record->kind = kind;
    memcpy(record + 1, id, id_size);
    record->id = (const char *)(record + 1);
    return record;
}

/** Reserves the room of the one holding a want's settlement may hand over. Answers 0 when memory ran out. */
static int reserve_the_settlements_holding(weivel_c_record *record) {
    if (record->kind != WEIVEL_C_RECORD_WANT) {
        return 1;
    }
    if (weivel_c_handles_reserve(record->contexts->handles) != WEIVEL_C_STATUS_OK) {
        return 0;
    }
    record->holding_reserved = 1;
    return 1;
}

weivel_c_record *weivel_c_record_prepare(weivel_c_context *context, uint32_t kind, const char *id) {
    weivel_c_record *record = make_record(context, kind, id);
    weivel_c_handles *handles = context->contexts->handles;
    if (record == NULL) {
        return NULL;
    }
    if (weivel_c_handles_reserve(handles) != WEIVEL_C_STATUS_OK) {
        release_record(record);
        return NULL;
    }
    if (!reserve_the_settlements_holding(record)) {
        weivel_c_handles_unreserve(handles);
        release_record(record);
        return NULL;
    }
    return record;
}

void weivel_c_record_abandon(const weivel_c_keeping *keeping) {
    if (!keeping->reservation_used) {
        weivel_c_handles_unreserve(keeping->record->contexts->handles);
    }
    release_record(keeping->record);
}

weivel_c_status weivel_c_record_keep_answered(void *data) {
    weivel_c_keeping *keeping = data;
    weivel_c_record *record = keeping->record;
    weivel_c_contexts *contexts = record->contexts;
    weivel_c_handle_entry entry = WEIVEL_INIT(weivel_c_handle_entry);
    weivel_c_status status;
    entry.kind = handle_kind_of(record->kind);
    entry.handle = *keeping->answered;
    entry.owner = record->plugin;
    entry.object = record;
    weivel_c_mutex_lock(&contexts->lock);
    keeping->reservation_used = entry.handle != 0;
    status = weivel_c_handles_add_reserved(contexts->handles, &entry, NULL);
    if (status == WEIVEL_C_STATUS_OK) {
        record->handle = entry.handle;
        link_record(record);
        contexts->record_count++;
    }
    weivel_c_mutex_unlock(&contexts->lock);
    return status;
}

/** Under the lock of the contexts: the record kept under `handle`, whatever its kind; NULL for none and for a plugin. */
static weivel_c_record *record_under(weivel_c_contexts *contexts, weivel_handle handle) {
    weivel_c_handle_entry entry = WEIVEL_INIT(weivel_c_handle_entry);
    if (handle == 0 || weivel_c_handles_find(contexts->handles, handle, &entry) != WEIVEL_C_STATUS_OK ||
        entry.kind == WEIVEL_C_HANDLE_PLUGIN) {
        return NULL;
    }
    return entry.object;
}

weivel_c_record *weivel_c_record_find(weivel_c_contexts *contexts, weivel_handle handle, uint32_t kind) {
    weivel_c_record *record = record_under(contexts, handle);
    return record != NULL && record->kind == kind ? record : NULL;
}

int weivel_c_record_read(weivel_c_contexts *contexts, weivel_handle handle, uint32_t kind, weivel_c_record *copy) {
    weivel_c_record *record;
    int found;
    weivel_c_mutex_lock(&contexts->lock);
    record = weivel_c_record_find(contexts, handle, kind);
    found = record != NULL && !record->ended;
    if (found) {
        *copy = *record;
    }
    weivel_c_mutex_unlock(&contexts->lock);
    return found;
}

weivel_c_record *weivel_c_record_borrow(weivel_c_contexts *contexts, weivel_handle handle, uint32_t kind) {
    weivel_c_record *record;
    weivel_c_mutex_lock(&contexts->lock);
    record = weivel_c_record_find(contexts, handle, kind);
    if (record != NULL && record->ended) {
        record = NULL;
    }
    if (record != NULL) {
        record->calling = 1;
    }
    weivel_c_mutex_unlock(&contexts->lock);
    return record;
}

void weivel_c_record_put_back(weivel_c_record *record) {
    weivel_c_contexts *contexts = record->contexts;
    int freeing;
    weivel_c_mutex_lock(&contexts->lock);
    record->calling = 0;
    freeing = record->ended && !record->retiring;
    if (freeing) {
        drop_record(record);
    }
    weivel_c_mutex_unlock(&contexts->lock);
    if (freeing) {
        release_record(record);
    }
}

weivel_c_record *weivel_c_record_claim(weivel_c_contexts *contexts, weivel_handle handle, uint32_t kind) {
    weivel_c_record *record;
    weivel_c_mutex_lock(&contexts->lock);
    record = weivel_c_record_find(contexts, handle, kind);
    if (record != NULL && record->ended) {
        record = NULL;
    }
    if (record != NULL) {
        record->ended = 1;
        record->retiring = 1;
    }
    weivel_c_mutex_unlock(&contexts->lock);
    return record;
}

weivel_c_status weivel_c_record_hold_settled(weivel_c_record *want, weivel_handle plugin,
                                             weivel_handle implementation, void **object) {
    weivel_c_status held = weivel_c_contexts_hold(want->contexts, plugin, implementation, WEIVEL_C_RECORD_SERVICE,
                                                  (int)want->holding_reserved, object);
    if (held != WEIVEL_C_STATUS_FAILED) {
        want->holding_reserved = 0;
    }
    return held;
}

static int is_takeable(const weivel_c_record *record, const weivel_c_context *context, uint32_t kind,
                       uint32_t other_kind) {
    return record != NULL && (record->kind == kind || record->kind == other_kind) &&
           record->plugin == context->plugin && !record->ended;
}

/** @implNote A destroyed core runs no step, so the record stays in its list for the frees that follow a destroy. */
static void retire(weivel_c_record *record) {
    weivel_c_contexts *contexts = record->contexts;
    if (weivel_c_core_defer(contexts->core, &record->step, NULL) == WEIVEL_C_STATUS_OK) {
        return;
    }
    weivel_c_mutex_lock(&contexts->lock);
    record->retiring = 0;
    weivel_c_mutex_unlock(&contexts->lock);
}

void weivel_c_record_end_in_dispatch(weivel_c_record *record) {
    weivel_c_contexts *contexts = record->contexts;
    weivel_c_mutex_lock(&contexts->lock);
    drop_record(record);
    weivel_c_mutex_unlock(&contexts->lock);
    release_record(record);
}

static void orphan_record(weivel_c_record *record) {
    unlink_record(record);
    record->context = NULL;
    link_record(record);
}

/** Under the lock of the contexts: ends one record, and answers it when it is the caller's to free. */
static weivel_c_record *end_record(weivel_c_record *record, int freeing) {
    end_entry_of(record);
    record->ended = 1;
    if (!record->retiring && !record->calling) {
        unlink_record(record);
        record->contexts->record_count--;
        return record;
    }
    if (freeing) {
        orphan_record(record);
    }
    return NULL;
}

void weivel_c_records_end(weivel_c_context *context, int freeing) {
    weivel_c_contexts *contexts = context->contexts;
    weivel_c_record *freed = NULL;
    weivel_c_record *record;
    weivel_c_mutex_lock(&contexts->lock);
    record = context->records;
    while (record != NULL) {
        weivel_c_record *next = record->next;
        weivel_c_record *ended = end_record(record, freeing);
        if (ended != NULL) {
            ended->next = freed;
            freed = ended;
        }
        record = next;
    }
    weivel_c_mutex_unlock(&contexts->lock);
    weivel_c_records_free(freed);
}

void weivel_c_records_free(weivel_c_record *first) {
    while (first != NULL) {
        weivel_c_record *next = first->next;
        release_record(first);
        first = next;
    }
}

void weivel_c_records_discard(weivel_c_record *first) {
    while (first != NULL) {
        weivel_c_record *next = first->next;
        end_entry_of(first);
        release_record(first);
        first = next;
    }
}

weivel_c_status weivel_c_contexts_hold(weivel_c_contexts *contexts, weivel_handle plugin, weivel_handle handle,
                                       uint32_t kind, int reserved, void **object) {
    weivel_c_status status = WEIVEL_C_STATUS_FAILED;
    weivel_c_record *record;
    *object = NULL;
    weivel_c_mutex_lock(&contexts->lock);
    record = weivel_c_record_find(contexts, handle, kind);
    if (record != NULL) {
        *object = record->object;
        status = reserved ? weivel_c_handles_hold_reserved(contexts->handles, plugin, handle, record)
                          : weivel_c_handles_hold(contexts->handles, plugin, handle, record);
    }
    weivel_c_mutex_unlock(&contexts->lock);
    return status;
}

/** What an ending the core is asked for works on, and the record its answered step marked, if any. */
typedef struct ending {
    weivel_c_context *context;
    weivel_handle handle;
    uint32_t kind;
    uint32_t other_kind;
    weivel_c_record *marked;
} ending;

/**
 * The answered step of an ending the core made: under the layer lock, before any event is dispatched, it marks the
 * record ended and retiring.
 *
 * @implNote Marking only once the core answered OK is what lets a settlement the core queued before the ending still
 * settle: the core refuses the ending of a want that settled, so no settlement can be waiting for a marked record.
 */
static weivel_c_status mark_ended(void *data) {
    ending *ended = data;
    weivel_c_contexts *contexts = ended->context->contexts;
    weivel_c_record *record;
    weivel_c_mutex_lock(&contexts->lock);
    record = record_under(contexts, ended->handle);
    if (is_takeable(record, ended->context, ended->kind, ended->other_kind)) {
        record->ended = 1;
        record->retiring = 1;
        ended->marked = record;
    }
    weivel_c_mutex_unlock(&contexts->lock);
    return WEIVEL_C_STATUS_OK;
}

static int names_a_takeable_record(const ending *asked) {
    weivel_c_contexts *contexts = asked->context->contexts;
    int takeable;
    weivel_c_mutex_lock(&contexts->lock);
    takeable =
        is_takeable(record_under(contexts, asked->handle), asked->context, asked->kind, asked->other_kind);
    weivel_c_mutex_unlock(&contexts->lock);
    return takeable;
}

weivel_c_status weivel_c_context_end_handle(weivel_c_context *context, weivel_handle handle, uint32_t kind,
                                            uint32_t other_kind, weivel_c_refusal *refusal) {
    ending asked = {context, handle, kind, other_kind, NULL};
    weivel_c_answered_step then = {mark_ended, &asked};
    weivel_c_status status;
    if (context == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    if (!names_a_takeable_record(&asked)) {
        return WEIVEL_C_STATUS_NONE;
    }
    status = weivel_c_core_unregister_then(context->contexts->core, handle, refusal, &then);
    if (asked.marked != NULL) {
        retire(asked.marked);
    }
    return status;
}
