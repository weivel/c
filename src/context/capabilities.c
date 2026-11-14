#include "contexts.h"

#include <stddef.h>
#include <stdlib.h>

#include "../core/calls.h"

#define END_OF(type, field) (offsetof(type, field) + sizeof(((type *)0)->field))
#define FIRST_IMPLEMENTATIONS_SIZE END_OF(weivel_c_implementations, storage)

static int is_an_id(const char *id) {
    return id != NULL && id[0] != '\0';
}

weivel_c_status weivel_c_context_provide_capability(weivel_c_context *context, const char *id, void *implementation,
                                                    weivel_handle *provided, weivel_c_refusal *refusal) {
    weivel_handle answered = 0;
    weivel_c_keeping keeping = {NULL, &answered, 0};
    weivel_c_answered_step then = {weivel_c_record_keep_answered, &keeping};
    weivel_c_status status;
    if (provided != NULL) {
        *provided = 0;
    }
    if (context == NULL || !is_an_id(id) || implementation == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    keeping.record = weivel_c_record_prepare(context, WEIVEL_C_RECORD_IMPLEMENTATION, id);
    if (keeping.record == NULL) {
        return WEIVEL_C_STATUS_OUT_OF_MEMORY;
    }
    keeping.record->object = implementation;
    status = weivel_c_core_provide_capability_then(context->contexts->core, context->plugin, id, &answered, refusal,
                                                   &then);
    if (status != WEIVEL_C_STATUS_OK) {
        weivel_c_record_abandon(&keeping);
    } else if (provided != NULL) {
        *provided = answered;
    }
    return status;
}

/**
 * A listing's answered step and what it works on: room for `capacity` pointers and as many holdings reserved in the
 * handles table, taken before the core is asked.
 */
typedef struct listing {
    weivel_c_context *context;
    const weivel_capability_list *answered;
    void **items;
    uint32_t capacity;
    uint32_t reserved;
    uint32_t kept;
    uint32_t unknown;
    /** How many implementations the core answered when that was more than there was room for; else 0. */
    uint32_t short_of;
} listing;

static const weivel_capability_implementation *implementation_at(const weivel_capability_list *answered,
                                                                 uint32_t index) {
    const char *first = (const char *)answered->items;
    return (const weivel_capability_implementation *)(const void *)(first + (size_t)index * answered->items->size);
}

/**
 * @implNote Each holding is recorded under the layer lock, as a get's is, into room reserved before the call, so
 * nothing here can fail. A list longer than that room records nothing and says how long it is: its caller makes the
 * room and asks again, which the core answers afresh, a plugin holding an implementation once however often it is
 * handed it. The core's array is stepped through at the stride its first element declares.
 */
static weivel_c_status hold_the_implementations(void *data) {
    listing *listed = data;
    uint32_t index;
    listed->short_of = listed->answered->count > listed->capacity ? listed->answered->count : 0;
    if (listed->short_of != 0) {
        return WEIVEL_C_STATUS_OK;
    }
    for (index = 0; index < listed->answered->count; index++) {
        weivel_handle implementation = implementation_at(listed->answered, index)->implementation;
        weivel_c_status held =
            weivel_c_contexts_hold(listed->context->contexts, listed->context->plugin, implementation,
                                   WEIVEL_C_RECORD_IMPLEMENTATION, 1, &listed->items[listed->kept]);
        if (held == WEIVEL_C_STATUS_FAILED) {
            listed->unknown++;
        } else {
            listed->kept++;
            listed->reserved--;
        }
    }
    return WEIVEL_C_STATUS_OK;
}

/** Makes room for `count` pointers and as many holdings. Answers 0 when memory ran out. */
static int make_room(listing *listed, uint32_t count) {
    weivel_c_handles *handles = listed->context->contexts->handles;
    free(listed->items);
    listed->capacity = 0;
    listed->items = malloc(count * sizeof *listed->items);
    if (listed->items == NULL) {
        return 0;
    }
    while (listed->reserved < count) {
        if (weivel_c_handles_reserve(handles) != WEIVEL_C_STATUS_OK) {
            return 0;
        }
        listed->reserved++;
    }
    listed->capacity = count;
    return 1;
}

static void give_back_unused_room(listing *listed) {
    while (listed->reserved > 0) {
        weivel_c_handles_unreserve(listed->context->contexts->handles);
        listed->reserved--;
    }
}

/** Asks the core until its answer fits the room made for it. */
static weivel_c_status list_into_room(listing *listed, const char *id, weivel_capability_list *answered,
                                      weivel_c_refusal *refusal) {
    weivel_c_core *core = listed->context->contexts->core;
    weivel_c_answered_step then = {hold_the_implementations, listed};
    weivel_c_status status;
    do {
        if (listed->short_of != 0 && !make_room(listed, listed->short_of)) {
            return WEIVEL_C_STATUS_OUT_OF_MEMORY;
        }
        listed->short_of = 0;
        status = weivel_c_core_all_capabilities_then(core, listed->context->plugin, id, answered, refusal, &then);
        weivel_c_core_capability_list_release(core, answered);
    } while (status == WEIVEL_C_STATUS_OK && listed->short_of != 0);
    return status;
}

static int list_is_ready(const weivel_c_implementations *list) {
    return list != NULL && list->size >= FIRST_IMPLEMENTATIONS_SIZE && list->storage == NULL;
}

weivel_c_status weivel_c_context_all_capabilities(weivel_c_context *context, const char *id,
                                                  weivel_c_implementations *list, weivel_c_refusal *refusal) {
    weivel_capability_list answered = WEIVEL_INIT(weivel_capability_list);
    listing listed = {context, &answered, NULL, 0, 0, 0, 0, 0};
    weivel_c_status status;
    if (context == NULL || !list_is_ready(list)) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    list->items = NULL;
    list->count = 0;
    status = list_into_room(&listed, id, &answered, refusal);
    give_back_unused_room(&listed);
    if (status == WEIVEL_C_STATUS_OK && listed.kept > 0) {
        list->items = listed.items;
        list->count = listed.kept;
        list->storage = listed.items;
    } else {
        free(listed.items);
    }
    if (listed.unknown > 0) {
        weivel_c_contexts_report(context->contexts, WEIVEL_C_FAILURE_OBJECT_UNKNOWN, WEIVEL_C_STATUS_FAILED, NULL,
                                 "the core listed an implementation the layer holds no object for, which was left out");
    }
    return status;
}

weivel_c_status weivel_c_implementations_release(weivel_c_implementations *list) {
    if (list == NULL || list->size < FIRST_IMPLEMENTATIONS_SIZE) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    free(list->storage);
    list->items = NULL;
    list->count = 0;
    list->storage = NULL;
    return WEIVEL_C_STATUS_OK;
}

weivel_c_status weivel_c_context_watch_capability(weivel_c_context *context, const char *id,
                                                  const weivel_c_capability_watcher *watcher, weivel_handle *watch,
                                                  weivel_c_refusal *refusal) {
    weivel_handle answered = 0;
    weivel_c_keeping keeping = {NULL, &answered, 0};
    weivel_c_answered_step then = {weivel_c_record_keep_answered, &keeping};
    weivel_c_status status;
    if (watch != NULL) {
        *watch = 0;
    }
    if (context == NULL || !is_an_id(id) || watcher == NULL || watcher->hear == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    keeping.record = weivel_c_record_prepare(context, WEIVEL_C_RECORD_CAPABILITY_WATCH, id);
    if (keeping.record == NULL) {
        return WEIVEL_C_STATUS_OUT_OF_MEMORY;
    }
    keeping.record->capability_watcher = *watcher;
    status = weivel_c_core_watch_capability_then(context->contexts->core, context->plugin, id, &answered, refusal,
                                                 &then);
    if (status != WEIVEL_C_STATUS_OK) {
        weivel_c_record_abandon(&keeping);
    } else if (watch != NULL) {
        *watch = answered;
    }
    return status;
}
