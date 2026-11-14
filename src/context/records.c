#include "contexts.h"

#include <string.h>

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

static void release_record(weivel_c_record *record) {
    weivel_c_contexts *contexts = record->contexts;
    contexts->allocator.release(contexts->allocator.data, record);
}

/** Under the lock of the contexts: takes a record out of the table and out of its list. */
static void drop_record(weivel_c_record *record) {
    weivel_c_handles_end(record->contexts->handles, record->handle, NULL);
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

weivel_c_record *weivel_c_record_prepare(weivel_c_context *context, uint32_t kind, const char *id) {
    weivel_c_record *record = make_record(context, kind, id);
    if (record != NULL && weivel_c_handles_reserve(context->contexts->handles) != WEIVEL_C_STATUS_OK) {
        release_record(record);
        return NULL;
    }
    return record;
}

void weivel_c_record_abandon(weivel_c_record *record) {
    weivel_c_handles_unreserve(record->contexts->handles);
    release_record(record);
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

weivel_c_record *weivel_c_record_claim(weivel_c_contexts *contexts, weivel_handle handle, uint32_t kind) {
    weivel_c_record *record;
    weivel_c_mutex_lock(&contexts->lock);
    record = weivel_c_record_find(contexts, handle, kind);
    if (record != NULL && record->ended) {
        record = NULL;
    }
    if (record != NULL) {
        record->ended = 1;
    }
    weivel_c_mutex_unlock(&contexts->lock);
    return record;
}

static int is_takeable(const weivel_c_record *record, const weivel_c_context *context, uint32_t kind,
                       uint32_t other_kind) {
    return record != NULL && (record->kind == kind || record->kind == other_kind) &&
           record->plugin == context->plugin && !record->ended;
}

weivel_c_record *weivel_c_record_take(weivel_c_context *context, weivel_handle handle, uint32_t kind,
                                      uint32_t other_kind) {
    weivel_c_contexts *contexts = context->contexts;
    weivel_c_record *record;
    weivel_c_mutex_lock(&contexts->lock);
    record = record_under(contexts, handle);
    if (is_takeable(record, context, kind, other_kind)) {
        record->ended = 1;
        record->retiring = 1;
    } else {
        record = NULL;
    }
    weivel_c_mutex_unlock(&contexts->lock);
    return record;
}

/** @implNote A destroyed core runs no step, so the record stays in its list for the frees that follow a destroy. */
void weivel_c_record_retire(weivel_c_record *record) {
    weivel_c_contexts *contexts = record->contexts;
    if (weivel_c_core_defer(contexts->core, &record->step, NULL) == WEIVEL_C_STATUS_OK) {
        return;
    }
    weivel_c_mutex_lock(&contexts->lock);
    record->retiring = 0;
    weivel_c_mutex_unlock(&contexts->lock);
}

void weivel_c_record_give_back(weivel_c_record *record) {
    weivel_c_contexts *contexts = record->contexts;
    int abandoned;
    weivel_c_mutex_lock(&contexts->lock);
    abandoned = record->abandoned != 0;
    if (!abandoned) {
        record->ended = 0;
        record->retiring = 0;
    }
    weivel_c_mutex_unlock(&contexts->lock);
    if (abandoned) {
        weivel_c_record_retire(record);
    }
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
    weivel_c_handles_end(record->contexts->handles, record->handle, NULL);
    record->ended = 1;
    if (!record->retiring) {
        unlink_record(record);
        record->contexts->record_count--;
        return record;
    }
    record->abandoned = 1;
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

weivel_c_status weivel_c_context_end_handle(weivel_c_context *context, weivel_handle handle, uint32_t kind,
                                            uint32_t other_kind, weivel_c_refusal *refusal) {
    weivel_c_record *record;
    weivel_c_status status;
    if (context == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    record = weivel_c_record_take(context, handle, kind, other_kind);
    if (record == NULL) {
        return WEIVEL_C_STATUS_NONE;
    }
    status = weivel_c_core_unregister(context->contexts->core, handle, refusal);
    if (status == WEIVEL_C_STATUS_OK) {
        weivel_c_record_retire(record);
    } else {
        weivel_c_record_give_back(record);
    }
    return status;
}
