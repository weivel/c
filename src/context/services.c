#include "contexts.h"

#include <stddef.h>

#include "../core/calls.h"

#define END_OF(type, field) (offsetof(type, field) + sizeof(((type *)0)->field))
#define FIRST_WANT_OPTIONS_SIZE END_OF(weivel_c_want_options, poll_millis)

static int is_an_id(const char *id) {
    return id != NULL && id[0] != '\0';
}

/** A get's answered step and what it works on. */
typedef struct resolving {
    weivel_c_context *context;
    const weivel_handle *answered;
    void *object;
    int held;
} resolving;

/**
 * @implNote The holding is recorded under the layer lock, before the get's caller is answered: the registration can
 * be revoked the moment that lock is let go, and a revocation heard before the holding was recorded would tell the
 * plugin nothing. It goes into room reserved before the core was asked, so it cannot fail for want of memory.
 */
static weivel_c_status hold_the_service(void *data) {
    resolving *resolved = data;
    weivel_c_status status = weivel_c_contexts_hold(resolved->context->contexts, resolved->context->plugin,
                                                    *resolved->answered, WEIVEL_C_RECORD_SERVICE, 1,
                                                    &resolved->object);
    resolved->held = status != WEIVEL_C_STATUS_FAILED;
    return status;
}

weivel_c_status weivel_c_context_get_service(weivel_c_context *context, const char *id, void **service,
                                             weivel_c_refusal *refusal) {
    weivel_handle registration = 0;
    resolving resolved = {context, &registration, NULL, 0};
    weivel_c_answered_step then = {hold_the_service, &resolved, weivel_c_context_guard, context};
    weivel_c_status status;
    if (service == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    *service = NULL;
    if (context == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    if (weivel_c_context_is_detached(context)) {
        return WEIVEL_C_STATUS_DETACHED;
    }
    status = weivel_c_handles_reserve(context->contexts->handles);
    if (status != WEIVEL_C_STATUS_OK) {
        return status;
    }
    status = weivel_c_core_get_service_then(context->contexts->core, context->plugin, id, &registration, refusal,
                                            &then);
    if (!resolved.held) {
        weivel_c_handles_unreserve(context->contexts->handles);
    }
    if (status == WEIVEL_C_STATUS_OK) {
        *service = resolved.object;
    }
    return status;
}

/** 1 when the options name a time, so the core reads `now`. */
static int names_a_time(const weivel_c_want_options *options) {
    return options->timed == WEIVEL_CHOICE_YES || options->budgeted == WEIVEL_CHOICE_YES || options->poll_millis > 0;
}

/** Lays the plugin's options into the core's, each unchanged, with the host's time where the core will read it. */
static weivel_c_status encode_want(weivel_c_context *context, const weivel_c_want_options *options,
                                   weivel_want_options *encoded) {
    encoded->timed = options->timed;
    encoded->timeout_millis = options->timeout_millis;
    encoded->budgeted = options->budgeted;
    encoded->budget_millis = options->budget_millis;
    encoded->poll_millis = options->poll_millis;
    if (!names_a_time(options)) {
        return WEIVEL_C_STATUS_OK;
    }
    if (context->contexts->timer == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    return weivel_c_timer_now(context->contexts->timer, &encoded->now);
}

static void answer_handle(weivel_handle *asked, weivel_handle answered, weivel_c_status status) {
    if (asked != NULL) {
        *asked = status == WEIVEL_C_STATUS_OK ? answered : 0;
    }
}

static int want_is_usable(const weivel_c_context *context, const char *id, const weivel_c_want_options *options,
                          const weivel_c_want_callback *callback) {
    return context != NULL && is_an_id(id) && callback != NULL && callback->settled != NULL &&
           (options == NULL || options->size >= FIRST_WANT_OPTIONS_SIZE);
}

weivel_c_status weivel_c_context_want_service(weivel_c_context *context, const char *id,
                                              const weivel_c_want_options *options,
                                              const weivel_c_want_callback *callback, weivel_handle *want,
                                              weivel_c_refusal *refusal) {
    weivel_want_options encoded = WEIVEL_INIT(weivel_want_options);
    weivel_handle answered = 0;
    weivel_c_keeping keeping = {NULL, &answered, 0};
    weivel_c_answered_step then = {weivel_c_record_keep_answered, &keeping, weivel_c_context_guard, context};
    weivel_c_status status = WEIVEL_C_STATUS_OK;
    answer_handle(want, 0, WEIVEL_C_STATUS_MISUSE);
    if (!want_is_usable(context, id, options, callback)) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    if (weivel_c_context_is_detached(context)) {
        return WEIVEL_C_STATUS_DETACHED;
    }
    if (options != NULL) {
        status = encode_want(context, options, &encoded);
    }
    if (status != WEIVEL_C_STATUS_OK) {
        return status;
    }
    keeping.record = weivel_c_record_prepare(context, WEIVEL_C_RECORD_WANT, id);
    if (keeping.record == NULL) {
        return WEIVEL_C_STATUS_OUT_OF_MEMORY;
    }
    keeping.record->want_callback = *callback;
    status = weivel_c_core_want_service_then(context->contexts->core, context->plugin, id,
                                             options == NULL ? NULL : &encoded, &answered, refusal, &then);
    if (status != WEIVEL_C_STATUS_OK) {
        weivel_c_record_abandon(&keeping);
    }
    answer_handle(want, answered, status);
    return status;
}

weivel_c_status weivel_c_context_withdraw_want(weivel_c_context *context, weivel_handle want,
                                               weivel_c_refusal *refusal) {
    return weivel_c_context_end_handle(context, want, WEIVEL_C_RECORD_WANT, WEIVEL_C_RECORD_WANT, refusal);
}

weivel_c_status weivel_c_context_watch_service(weivel_c_context *context, const char *id,
                                               const weivel_c_service_watcher *watcher, weivel_handle *watch,
                                               weivel_c_refusal *refusal) {
    weivel_handle answered = 0;
    weivel_c_keeping keeping = {NULL, &answered, 0};
    weivel_c_answered_step then = {weivel_c_record_keep_answered, &keeping, weivel_c_context_guard, context};
    weivel_c_status status;
    answer_handle(watch, 0, WEIVEL_C_STATUS_MISUSE);
    if (context == NULL || !is_an_id(id) || watcher == NULL || watcher->hear == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    if (weivel_c_context_is_detached(context)) {
        return WEIVEL_C_STATUS_DETACHED;
    }
    keeping.record = weivel_c_record_prepare(context, WEIVEL_C_RECORD_SERVICE_WATCH, id);
    if (keeping.record == NULL) {
        return WEIVEL_C_STATUS_OUT_OF_MEMORY;
    }
    keeping.record->service_watcher = *watcher;
    status = weivel_c_core_watch_service_then(context->contexts->core, context->plugin, id, &answered, refusal, &then);
    if (status != WEIVEL_C_STATUS_OK) {
        weivel_c_record_abandon(&keeping);
    }
    answer_handle(watch, answered, status);
    return status;
}

weivel_c_status weivel_c_context_stop_watching(weivel_c_context *context, weivel_handle watch,
                                               weivel_c_refusal *refusal) {
    return weivel_c_context_end_handle(context, watch, WEIVEL_C_RECORD_SERVICE_WATCH,
                                       WEIVEL_C_RECORD_CAPABILITY_WATCH, refusal);
}

weivel_c_status weivel_c_context_service_ids(weivel_c_context *context, weivel_service_list *list,
                                             weivel_c_refusal *refusal) {
    weivel_c_answered_step then = {NULL, NULL, weivel_c_context_guard, context};
    if (context == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    if (weivel_c_context_is_detached(context)) {
        return WEIVEL_C_STATUS_DETACHED;
    }
    return weivel_c_core_service_ids_then(context->contexts->core, context->plugin, list, refusal, &then);
}

weivel_c_status weivel_c_context_service_ids_release(weivel_c_context *context, weivel_service_list *list) {
    if (context == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    return weivel_c_core_service_list_release(context->contexts->core, list);
}

weivel_c_status weivel_c_context_register_service(weivel_c_context *context, const char *id, void *service,
                                                  const weivel_c_capacity_answerer *answerer,
                                                  weivel_handle *registration, weivel_c_refusal *refusal) {
    weivel_handle answered = 0;
    weivel_c_keeping keeping = {NULL, &answered, 0};
    weivel_c_answered_step then = {weivel_c_record_keep_answered, &keeping, weivel_c_context_guard, context};
    weivel_c_status status;
    answer_handle(registration, 0, WEIVEL_C_STATUS_MISUSE);
    if (context == NULL || !is_an_id(id) || service == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    if (weivel_c_context_is_detached(context)) {
        return WEIVEL_C_STATUS_DETACHED;
    }
    keeping.record = weivel_c_record_prepare(context, WEIVEL_C_RECORD_SERVICE, id);
    if (keeping.record == NULL) {
        return WEIVEL_C_STATUS_OUT_OF_MEMORY;
    }
    keeping.record->object = service;
    if (answerer != NULL) {
        keeping.record->answerer = *answerer;
    }
    status = weivel_c_core_register_service_then(context->contexts->core, context->plugin, id, &answered, refusal,
                                                 &then);
    if (status != WEIVEL_C_STATUS_OK) {
        weivel_c_record_abandon(&keeping);
    }
    answer_handle(registration, answered, status);
    return status;
}

weivel_c_status weivel_c_context_unregister_service(weivel_c_context *context, weivel_handle registration,
                                                    weivel_c_refusal *refusal) {
    return weivel_c_context_end_handle(context, registration, WEIVEL_C_RECORD_SERVICE, WEIVEL_C_RECORD_SERVICE,
                                       refusal);
}
