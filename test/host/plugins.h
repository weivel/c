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

/** What a later plugin's activate saw of an earlier one of its plan as it was called. */
typedef struct looking_back {
    test_plugin *earlier;
    uint32_t state;
    uint32_t ledger_status;
} looking_back;

/** Notes where the plugin `more` looks back at stands, in the host and in the ledger, and answers done. */
static inline uint32_t activate_and_look_back(void *data, weivel_c_context *context, weivel_c_completion *completion,
                                              const char **reason) {
    test_plugin *self = data;
    looking_back *look = self->more;
    (void)completion;
    (void)reason;
    self->context = context;
    trace_line(&self->in->heard, "activate", self->manifest.import_name, "");
    look->state = state_of(self->in, look->earlier);
    look->ledger_status = ledger_status_of(self->in, look->earlier, NULL);
    return WEIVEL_C_ENTRY_DONE;
}

/** A watch of the host's own that writes each change of the service it watches into the trace. */
static inline weivel_c_status trace_a_service_change(void *data, const weivel_c_service_change *change) {
    host_world *in = data;
    trace_line(&in->heard, "heard", change->id,
               change->change == WEIVEL_CHANGE_REGISTERED ? "registered" : "unregistered");
    return WEIVEL_C_STATUS_OK;
}

static inline void trace_a_watch_as_the_host(host_world *in, const char *id) {
    weivel_c_context *context = NULL;
    weivel_c_service_watcher watcher = {trace_a_service_change, NULL};
    watcher.data = in;
    CHECK(weivel_c_host_context(in->host, &context) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_watch_service(context, id, &watcher, NULL, NULL) == WEIVEL_C_STATUS_OK);
}

static inline void name_acting_plugin(test_plugin *self, host_world *in, const char *name, weivel_c_entry_function activate,
                                      acting *act) {
    name_test_plugin(self, in, name);
    self->entry.activate = activate;
    self->more = act;
}

/**
 * Two plugins of one plan: `first` registers `first:events` in its activate and answers done, short of the
 * capability `extras` it declares, so the core refuses its verification; `second` looks back at `first`.
 */
typedef struct refused_then_looking {
    test_plugin first;
    test_plugin second;
    acting registering;
    looking_back look;
} refused_then_looking;

static inline void register_refused_then_looking(refused_then_looking *pair, host_world *in) {
    pair->registering = (acting){"first:events", WEIVEL_C_STATUS_FAILED};
    name_acting_plugin(&pair->first, in, "first", activate_and_register, &pair->registering);
    offers_service(&pair->first, "first:events");
    pair->first.capabilities_provided[pair->first.capabilities_provided_count++] = "extras";
    name_acting_plugin(&pair->second, in, "second", activate_and_look_back, NULL);
    pair->second.more = &pair->look;
    pair->look = (looking_back){&pair->first, 0, 0};
    register_test_plugin(&pair->first);
    register_test_plugin(&pair->second);
}

static inline int activate_refused_then_looking(refused_then_looking *pair, host_world *in) {
    test_plugin *both[2];
    both[0] = &pair->first;
    both[1] = &pair->second;
    return activate_in_order(in, both, 2, WEIVEL_EDGES_CAPABILITIES);
}

/** The place among the outcomes told of the first one `plugin` was told, or -1. */
static inline int outcome_place_of(outcomes *self, weivel_handle plugin) {
    int found = -1;
    uint32_t index;
    lock_test_mutex(&self->mutex);
    for (index = 0; index < self->count && index < HOST_WORLD_LINES && found < 0; index++) {
        if (self->plugins[index] == plugin) {
            found = (int)index;
        }
    }
    unlock_test_mutex(&self->mutex);
    return found;
}

static inline int same_text(const char *told, const char *kept) {
    return strcmp(told, kept == NULL ? "" : kept) == 0;
}

/** 1 when `first` was told quarantined with exactly the refusal its ledger row shows: the core's of its verification. */
static inline int is_quarantined_with_its_verifications_refusal(host_world *in, refused_then_looking *pair) {
    weivel_ledger_row row = WEIVEL_INIT(weivel_ledger_row);
    int place = outcome_place_of(&in->told, pair->first.handle);
    int same;
    if (place < 0 || in->told.kinds[place] != WEIVEL_C_OUTCOME_QUARANTINED ||
        ledger_status_of(in, &pair->first, &row) != WEIVEL_PLUGIN_STATUS_QUARANTINED) {
        return 0;
    }
    same = row.refusal.code == WEIVEL_REFUSAL_ACTIVATION_MISMATCH && in->told.codes[place] == row.refusal.code &&
           same_text(in->told.details[place], row.refusal.detail) && same_text(in->told.fixes[place], row.refusal.fix) &&
           same_text(in->told.subjects[place], row.subject) && in->told.has_fixes[place] == row.has_fix &&
           strcmp(in->told.subjects[place], "first") == 0;
    CHECK(weivel_c_host_ledger_row_release(in->host, &row) == WEIVEL_C_STATUS_OK);
    return same;
}

/** 1 when `second` was told active, and with no refusal. */
static inline int is_active_with_no_refusal(host_world *in, refused_then_looking *pair) {
    int place = outcome_place_of(&in->told, pair->second.handle);
    return place >= 0 && in->told.kinds[place] == WEIVEL_C_OUTCOME_ACTIVE &&
           in->told.codes[place] == WEIVEL_REFUSAL_NONE && in->told.details[place][0] == '\0' &&
           in->told.subjects[place][0] == '\0' && in->told.has_fixes[place] == 0;
}

#endif
