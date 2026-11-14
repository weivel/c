#ifndef WEIVEL_C_TEST_HOST_PLUGINS_H
#define WEIVEL_C_TEST_HOST_PLUGINS_H

/**
 * @file
 * Entry functions the host tests build their plugins from. Each records in the world's trace what a lifecycle vector
 * records: `activate <plugin>` and `deactivate <plugin>` as the host calls them, then what it does.
 */

#include "world.h"

/** What `more` holds for the entry functions that act on one id. */
typedef struct acting {
    const char *id;
    weivel_c_status answered;
} acting;

static inline uint32_t activate_and_answer_done(void *data, weivel_c_context *context,
                                                weivel_c_completion *completion, const char **reason) {
    test_plugin *self = data;
    (void)completion;
    (void)reason;
    self->context = context;
    trace_line(&self->in->heard, "activate", self->manifest.import_name, "");
    return WEIVEL_C_ENTRY_DONE;
}

static inline uint32_t activate_and_fail(void *data, weivel_c_context *context, weivel_c_completion *completion,
                                         const char **reason) {
    test_plugin *self = data;
    (void)completion;
    self->context = context;
    trace_line(&self->in->heard, "activate", self->manifest.import_name, "");
    *reason = "the disk is full";
    return WEIVEL_C_ENTRY_FAILED;
}

static inline uint32_t activate_and_wait(void *data, weivel_c_context *context, weivel_c_completion *completion,
                                         const char **reason) {
    test_plugin *self = data;
    (void)reason;
    self->context = context;
    self->activation = completion;
    trace_line(&self->in->heard, "activate", self->manifest.import_name, "");
    return WEIVEL_C_ENTRY_PENDING;
}

/** Records the settlement, and finishes the activate that waited on it as done. */
static inline weivel_c_status settle_and_finish(void *data, const weivel_c_settlement *settlement) {
    test_plugin *self = data;
    trace_settlement(data, settlement);
    if (self->activation != NULL) {
        weivel_c_completion *activation = self->activation;
        self->activation = NULL;
        weivel_c_completion_finish(activation, WEIVEL_C_ENTRY_DONE, NULL);
    }
    return WEIVEL_C_STATUS_OK;
}

/** Wants the id `more` names, and finishes once the want ends; a vector's `{ want: id }`. */
static inline uint32_t activate_and_await_a_want(void *data, weivel_c_context *context,
                                                 weivel_c_completion *completion, const char **reason) {
    test_plugin *self = data;
    acting *act = self->more;
    weivel_c_want_callback callback = {settle_and_finish, self};
    (void)reason;
    self->context = context;
    self->activation = completion;
    trace_line(&self->in->heard, "activate", self->manifest.import_name, "");
    act->answered = weivel_c_context_want_service(context, act->id, NULL, &callback, NULL, NULL);
    return WEIVEL_C_ENTRY_PENDING;
}

static int registered_object;

/** Registers the id `more` names; a vector's `{ register: id }`. */
static inline uint32_t activate_and_register(void *data, weivel_c_context *context, weivel_c_completion *completion,
                                             const char **reason) {
    test_plugin *self = data;
    acting *act = self->more;
    (void)completion;
    (void)reason;
    self->context = context;
    trace_line(&self->in->heard, "activate", self->manifest.import_name, "");
    act->answered = weivel_c_context_register_service(context, act->id, &registered_object, NULL, NULL, NULL);
    if (act->answered == WEIVEL_C_STATUS_OK) {
        trace_line(&self->in->heard, "registered", self->manifest.import_name, act->id);
    }
    return WEIVEL_C_ENTRY_DONE;
}

static inline uint32_t deactivate_and_answer_done(void *data, weivel_c_context *context,
                                                  weivel_c_completion *completion, const char **reason) {
    test_plugin *self = data;
    (void)context;
    (void)completion;
    (void)reason;
    trace_line(&self->in->heard, "deactivate", self->manifest.import_name, "");
    return WEIVEL_C_ENTRY_DONE;
}

static inline uint32_t deactivate_and_wait(void *data, weivel_c_context *context, weivel_c_completion *completion,
                                           const char **reason) {
    test_plugin *self = data;
    (void)context;
    (void)reason;
    self->deactivation = completion;
    trace_line(&self->in->heard, "deactivate", self->manifest.import_name, "");
    return WEIVEL_C_ENTRY_PENDING;
}

/** What a watch of a capability heard, in order. */
typedef struct watched_heard {
    uint32_t count;
    uint32_t changes[16];
} watched_heard;

static inline weivel_c_status hear_a_capability(void *data, const weivel_c_capability_change *change) {
    watched_heard *self = data;
    if (self->count < 16) {
        self->changes[self->count] = change->change;
    }
    self->count++;
    return WEIVEL_C_STATUS_OK;
}

static inline void name_acting_plugin(test_plugin *self, host_world *in, const char *name, weivel_c_entry_function activate,
                                      acting *act) {
    name_test_plugin(self, in, name);
    self->entry.activate = activate;
    self->more = act;
}

#endif
