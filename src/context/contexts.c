#include "contexts.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define END_OF(type, field) (offsetof(type, field) + sizeof(((type *)0)->field))
#define FIRST_OPTIONS_SIZE END_OF(weivel_c_contexts_options, log)

void weivel_c_contexts_report(weivel_c_contexts *contexts, uint32_t kind, weivel_c_status status,
                              const weivel_c_event *event, const char *text) {
    weivel_c_failure failure = WEIVEL_INIT(weivel_c_failure);
    failure.kind = kind;
    failure.status = (uint32_t)status;
    failure.event_kind = event == NULL ? (uint32_t)WEIVEL_EVENT_NONE : event->kind;
    failure.sequence = event == NULL ? 0 : event->sequence;
    failure.text = text;
    weivel_c_core_report(contexts->core, &failure);
}

void weivel_c_contexts_called_back(weivel_c_contexts *contexts, weivel_c_status answered,
                                   const weivel_c_event *event) {
    if (answered != WEIVEL_C_STATUS_OK) {
        weivel_c_contexts_report(contexts, WEIVEL_C_FAILURE_LISTENER, answered, event,
                                 "a callback a plugin gave its context answered a failure for an event");
    }
}

void weivel_c_contexts_write_log(weivel_c_contexts *contexts, const weivel_c_log_record *record,
                                 const weivel_c_event *event) {
    weivel_c_status written;
    if (contexts->log.write == NULL) {
        return;
    }
    written = contexts->log.write(contexts->log.data, record);
    if (written != WEIVEL_C_STATUS_OK) {
        weivel_c_contexts_report(contexts, WEIVEL_C_FAILURE_LOG, written, event,
                                 "the host's log answered a failure for a line");
    }
}

weivel_c_refusal weivel_c_contexts_refusal_of(const weivel_refusal *refusal, const char *subject, uint32_t has_fix) {
    weivel_c_refusal carried = WEIVEL_INIT(weivel_c_refusal);
    carried.code = refusal->code;
    carried.detail = refusal->detail;
    carried.fix = refusal->fix;
    carried.subject = subject;
    carried.has_fix = has_fix;
    return carried;
}

weivel_c_context *weivel_c_contexts_context_of(weivel_c_contexts *contexts, weivel_handle plugin,
                                               const weivel_c_event *event) {
    weivel_c_handle_entry entry = WEIVEL_INIT(weivel_c_handle_entry);
    weivel_c_context *context;
    if (plugin == 0 || weivel_c_handles_find(contexts->handles, plugin, &entry) != WEIVEL_C_STATUS_OK ||
        entry.kind != WEIVEL_C_HANDLE_PLUGIN) {
        return NULL;
    }
    context = entry.object;
    return event->sequence > context->first_sequence ? context : NULL;
}

static void *allocate_plainly(void *data, size_t size) {
    (void)data;
    return malloc(size);
}

static void release_plainly(void *data, void *memory) {
    (void)data;
    free(memory);
}

void weivel_c_contexts_replace_allocator(weivel_c_contexts *contexts, const weivel_c_allocator *allocator) {
    weivel_c_mutex_lock(&contexts->lock);
    contexts->allocator = *allocator;
    weivel_c_mutex_unlock(&contexts->lock);
}

static int options_are_usable(const weivel_c_contexts_options *options) {
    return options != NULL && options->size >= FIRST_OPTIONS_SIZE && options->core != NULL &&
           options->handles != NULL;
}

static weivel_c_status start_hearing(weivel_c_contexts *contexts) {
    weivel_c_listener listener = {weivel_c_contexts_hear, contexts};
    uint64_t registration = 0;
    return weivel_c_core_add_listener(contexts->core, &listener, &registration);
}

weivel_c_status weivel_c_contexts_create(const weivel_c_contexts_options *options, weivel_c_contexts **contexts) {
    weivel_c_contexts *created;
    weivel_c_status status;
    if (contexts == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    *contexts = NULL;
    if (!options_are_usable(options)) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    created = calloc(1, sizeof *created);
    if (created == NULL || !weivel_c_mutex_start(&created->lock)) {
        free(created);
        return WEIVEL_C_STATUS_OUT_OF_MEMORY;
    }
    created->core = options->core;
    created->handles = options->handles;
    created->timer = options->timer;
    created->log = options->log;
    created->allocator = (weivel_c_allocator){allocate_plainly, release_plainly, NULL};
    status = start_hearing(created);
    if (status != WEIVEL_C_STATUS_OK) {
        weivel_c_mutex_end(&created->lock);
        free(created);
        return status;
    }
    *contexts = created;
    return WEIVEL_C_STATUS_OK;
}

static void unlink_context(weivel_c_context *context) {
    weivel_c_contexts *contexts = context->contexts;
    if (context->previous != NULL) {
        context->previous->next = context->next;
    } else {
        contexts->first = context->next;
    }
    if (context->next != NULL) {
        context->next->previous = context->previous;
    }
    contexts->context_count--;
}

/**
 * Ends the plugin's entry while the table still keeps this very context under its handle, and drops what the plugin
 * holds, which points at what it was handed.
 *
 * @implNote The core may issue a forgotten plugin's handle again, so an entry under the same handle can be another's.
 */
static void end_the_plugins_entry(weivel_c_context *context) {
    weivel_c_handles *handles = context->contexts->handles;
    weivel_c_handle_entry entry = WEIVEL_INIT(weivel_c_handle_entry);
    if (weivel_c_handles_find(handles, context->plugin, &entry) == WEIVEL_C_STATUS_OK && entry.object == context) {
        weivel_c_handles_end(handles, context->plugin, NULL);
    }
    weivel_c_handles_drop_holdings(handles, context->plugin);
}

/** Frees a context whose records' steps can no longer run, because the core is destroyed. */
static void free_context_for_good(weivel_c_context *context) {
    end_the_plugins_entry(context);
    weivel_c_records_discard(context->records);
    free(context->slots);
    free(context);
}

weivel_c_status weivel_c_contexts_free(weivel_c_contexts *contexts) {
    if (contexts == NULL) {
        return WEIVEL_C_STATUS_OK;
    }
    if (!weivel_c_core_is_destroyed(contexts->core)) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    while (contexts->first != NULL) {
        weivel_c_context *context = contexts->first;
        contexts->first = context->next;
        free_context_for_good(context);
    }
    weivel_c_records_discard(contexts->orphans);
    weivel_c_mutex_end(&contexts->lock);
    free(contexts);
    return WEIVEL_C_STATUS_OK;
}

weivel_c_status weivel_c_contexts_count(weivel_c_contexts *contexts, uint32_t *contexts_made, uint32_t *records) {
    if (contexts == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    weivel_c_mutex_lock(&contexts->lock);
    if (contexts_made != NULL) {
        *contexts_made = contexts->context_count;
    }
    if (records != NULL) {
        *records = contexts->record_count;
    }
    weivel_c_mutex_unlock(&contexts->lock);
    return WEIVEL_C_STATUS_OK;
}

static weivel_c_status keep_context(weivel_c_context *context) {
    weivel_c_contexts *contexts = context->contexts;
    weivel_c_handle_entry entry = WEIVEL_INIT(weivel_c_handle_entry);
    weivel_c_status status;
    entry.kind = WEIVEL_C_HANDLE_PLUGIN;
    entry.handle = context->plugin;
    entry.object = context;
    weivel_c_mutex_lock(&contexts->lock);
    status = weivel_c_handles_add(contexts->handles, &entry, &context->token);
    if (status == WEIVEL_C_STATUS_OK) {
        context->next = contexts->first;
        if (contexts->first != NULL) {
            contexts->first->previous = context;
        }
        contexts->first = context;
        contexts->context_count++;
    }
    weivel_c_mutex_unlock(&contexts->lock);
    return status;
}

weivel_c_status weivel_c_context_create(weivel_c_contexts *contexts, weivel_handle plugin,
                                        const weivel_manifest *manifest, weivel_c_context **context) {
    weivel_c_context *created;
    weivel_c_status status;
    if (context == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    *context = NULL;
    if (contexts == NULL || plugin == 0) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    created = calloc(1, sizeof *created);
    if (created == NULL) {
        return WEIVEL_C_STATUS_OUT_OF_MEMORY;
    }
    created->contexts = contexts;
    created->plugin = plugin;
    created->manifest = manifest;
    created->first_sequence = weivel_c_core_last_sequence(contexts->core);
    status = keep_context(created);
    if (status != WEIVEL_C_STATUS_OK) {
        free(created);
        return status;
    }
    *context = created;
    return WEIVEL_C_STATUS_OK;
}

weivel_c_status weivel_c_context_token(weivel_c_context *context, uint64_t *token) {
    if (context == NULL || token == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    *token = context->token;
    return WEIVEL_C_STATUS_OK;
}

weivel_c_status weivel_c_context_end_entries(weivel_c_context *context) {
    weivel_c_contexts *contexts;
    if (context == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    contexts = context->contexts;
    weivel_c_records_end(context, 0);
    weivel_c_mutex_lock(&contexts->lock);
    context->slot_count = 0;
    weivel_c_mutex_unlock(&contexts->lock);
    return WEIVEL_C_STATUS_OK;
}

weivel_c_status weivel_c_context_free(weivel_c_context *context) {
    weivel_c_contexts *contexts;
    if (context == NULL) {
        return WEIVEL_C_STATUS_OK;
    }
    contexts = context->contexts;
    weivel_c_records_end(context, 1);
    weivel_c_mutex_lock(&contexts->lock);
    end_the_plugins_entry(context);
    unlink_context(context);
    weivel_c_mutex_unlock(&contexts->lock);
    free(context->slots);
    free(context);
    return WEIVEL_C_STATUS_OK;
}

weivel_c_status weivel_c_context_detach(weivel_c_context *context) {
    weivel_c_contexts *contexts;
    if (context == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    contexts = context->contexts;
    weivel_c_mutex_lock(&contexts->lock);
    context->detached = 1;
    end_the_plugins_entry(context);
    weivel_c_mutex_unlock(&contexts->lock);
    return WEIVEL_C_STATUS_OK;
}

weivel_c_status weivel_c_context_guard(void *data) {
    return weivel_c_context_is_detached(data) ? WEIVEL_C_STATUS_DETACHED : WEIVEL_C_STATUS_OK;
}

int weivel_c_context_is_detached(weivel_c_context *context) {
    int detached;
    weivel_c_mutex_lock(&context->contexts->lock);
    detached = context->detached != 0;
    weivel_c_mutex_unlock(&context->contexts->lock);
    return detached;
}

weivel_c_status weivel_c_context_manifest(weivel_c_context *context, const weivel_manifest **manifest) {
    if (context == NULL || manifest == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    *manifest = context->manifest;
    return context->manifest == NULL ? WEIVEL_C_STATUS_NONE : WEIVEL_C_STATUS_OK;
}

weivel_c_status weivel_c_context_log(weivel_c_context *context, uint32_t level, const char *text) {
    weivel_c_log_record record = WEIVEL_INIT(weivel_c_log_record);
    if (context == NULL || text == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    record.level = level;
    record.plugin = context->plugin;
    record.plugin_name = context->manifest == NULL ? NULL : context->manifest->import_name;
    record.text = text;
    weivel_c_contexts_write_log(context->contexts, &record, NULL);
    return WEIVEL_C_STATUS_OK;
}

static int has_room_for_a_listener(weivel_c_context *context) {
    uint32_t capacity = context->slot_capacity == 0 ? 4 : context->slot_capacity * 2;
    weivel_c_listener_slot *grown;
    if (context->slot_count < context->slot_capacity) {
        return 1;
    }
    grown = realloc(context->slots, capacity * sizeof *grown);
    if (grown == NULL) {
        return 0;
    }
    context->slots = grown;
    context->slot_capacity = capacity;
    return 1;
}

weivel_c_status weivel_c_context_add_listener(weivel_c_context *context, const weivel_c_plugin_listener *listener,
                                              uint64_t *registration) {
    weivel_c_status status = WEIVEL_C_STATUS_OUT_OF_MEMORY;
    if (context == NULL || listener == NULL || listener->hear == NULL || registration == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    weivel_c_mutex_lock(&context->contexts->lock);
    if (has_room_for_a_listener(context)) {
        weivel_c_listener_slot *slot = &context->slots[context->slot_count++];
        slot->registration = ++context->last_registration;
        slot->listener = *listener;
        *registration = slot->registration;
        status = WEIVEL_C_STATUS_OK;
    }
    weivel_c_mutex_unlock(&context->contexts->lock);
    return status;
}

static weivel_c_status remove_slot(weivel_c_context *context, uint64_t registration) {
    uint32_t index;
    for (index = 0; index < context->slot_count; index++) {
        if (context->slots[index].registration == registration) {
            memmove(&context->slots[index], &context->slots[index + 1],
                    (context->slot_count - index - 1) * sizeof context->slots[index]);
            context->slot_count--;
            return WEIVEL_C_STATUS_OK;
        }
    }
    return WEIVEL_C_STATUS_MISUSE;
}

weivel_c_status weivel_c_context_remove_listener(weivel_c_context *context, uint64_t registration) {
    weivel_c_status status;
    if (context == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    weivel_c_mutex_lock(&context->contexts->lock);
    status = remove_slot(context, registration);
    weivel_c_mutex_unlock(&context->contexts->lock);
    return status;
}

static int next_slot(weivel_c_context *context, uint64_t after, weivel_c_listener_slot *found) {
    uint32_t index;
    int has = 0;
    weivel_c_mutex_lock(&context->contexts->lock);
    for (index = 0; index < context->slot_count && !has; index++) {
        if (context->slots[index].registration > after) {
            *found = context->slots[index];
            has = 1;
        }
    }
    weivel_c_mutex_unlock(&context->contexts->lock);
    return has;
}

void weivel_c_context_emit(weivel_c_context *context, const weivel_c_plugin_event *told, const weivel_c_event *event) {
    weivel_c_listener_slot slot;
    uint64_t heard = 0;
    while (context != NULL && next_slot(context, heard, &slot)) {
        weivel_c_status answered = slot.listener.hear(slot.listener.data, told);
        heard = slot.registration;
        weivel_c_contexts_called_back(context->contexts, answered, event);
    }
}
