#ifndef WEIVEL_C_TEST_HOST_WORLD_H
#define WEIVEL_C_TEST_HOST_WORLD_H

/**
 * @file
 * What the host tests share: a host on the real core through a table that counts each crossing, on a clock the test
 * owns, with a sink, a log and a trace that record what reaches them, an outcome listener that records each outcome,
 * and plugins built as manifest values with entries of the test's own.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <weivel/c/host.h>

#include "core/fixture.h"
#include "threads.h"

#define HOST_WORLD_LINES 64
#define HOST_WORLD_TEXT 128
#define HOST_WORLD_PLUGINS 16

/** Lines recorded in the order they were made, by any thread. */
typedef struct trace {
    test_mutex mutex;
    char lines[HOST_WORLD_LINES][HOST_WORLD_TEXT];
    uint32_t count;
} trace;

static inline void start_trace(trace *self) {
    memset(self, 0, sizeof *self);
    start_test_mutex(&self->mutex);
}

static inline void trace_line(trace *self, const char *first, const char *second, const char *third) {
    lock_test_mutex(&self->mutex);
    if (self->count < HOST_WORLD_LINES) {
        snprintf(self->lines[self->count], HOST_WORLD_TEXT, "%s %s%s%s", first, second, third[0] == '\0' ? "" : " ",
                 third);
    }
    self->count++;
    unlock_test_mutex(&self->mutex);
}

/** The place of `line` in the trace, or -1. */
static inline int place_of(trace *self, const char *line) {
    int found = -1;
    uint32_t index;
    lock_test_mutex(&self->mutex);
    for (index = 0; index < self->count && index < HOST_WORLD_LINES && found < 0; index++) {
        if (strcmp(self->lines[index], line) == 0) {
            found = (int)index;
        }
    }
    unlock_test_mutex(&self->mutex);
    return found;
}

/** 1 when both lines were made and `first` before `second`; prints the trace otherwise. */
static inline int is_before(trace *self, const char *first, const char *second) {
    int earlier = place_of(self, first);
    int later = place_of(self, second);
    uint32_t index;
    if (earlier >= 0 && later > earlier) {
        return 1;
    }
    fprintf(stderr, "expected \"%s\" before \"%s\" in:\n", first, second);
    for (index = 0; index < self->count && index < HOST_WORLD_LINES; index++) {
        fprintf(stderr, "  %s\n", self->lines[index]);
    }
    return 0;
}

/** Every outcome heard, in order. */
typedef struct outcomes {
    test_mutex mutex;
    weivel_handle plugins[HOST_WORLD_LINES];
    uint32_t kinds[HOST_WORLD_LINES];
    uint32_t codes[HOST_WORLD_LINES];
    char details[HOST_WORLD_LINES][HOST_WORLD_TEXT];
    uint32_t count;
    trace *also;
} outcomes;

static const char *const OUTCOME_NAMES[] = {"none", "active", "quarantined", "refused", "released", "failed"};

static inline weivel_c_status hear_outcome(void *data, const weivel_c_outcome *outcome) {
    outcomes *self = data;
    lock_test_mutex(&self->mutex);
    if (self->count < HOST_WORLD_LINES) {
        self->plugins[self->count] = outcome->plugin;
        self->kinds[self->count] = outcome->kind;
        self->codes[self->count] = outcome->refusal.code;
        snprintf(self->details[self->count], HOST_WORLD_TEXT, "%s",
                 outcome->refusal.detail == NULL ? "" : outcome->refusal.detail);
    }
    self->count++;
    unlock_test_mutex(&self->mutex);
    if (self->also != NULL) {
        trace_line(self->also, "outcome", outcome->import_name == NULL ? "?" : outcome->import_name,
                   OUTCOME_NAMES[outcome->kind <= 5 ? outcome->kind : 0]);
    }
    return WEIVEL_C_STATUS_OK;
}

/** How many outcomes `plugin` was told, and the kind of the last one in `kind`. */
static inline uint32_t outcomes_of(outcomes *self, weivel_handle plugin, uint32_t *kind) {
    uint32_t found = 0;
    uint32_t index;
    lock_test_mutex(&self->mutex);
    for (index = 0; index < self->count && index < HOST_WORLD_LINES; index++) {
        if (self->plugins[index] == plugin) {
            found++;
            *kind = self->kinds[index];
        }
    }
    unlock_test_mutex(&self->mutex);
    return found;
}

/** The host's log: how many lines it was handed, and what it does about each, outside its own mutex. */
typedef struct counted_log {
    test_mutex mutex;
    uint32_t count;
    void (*react)(void *more);
    void *more;
} counted_log;

static inline weivel_c_status count_log(void *data, const weivel_c_log_record *record) {
    counted_log *self = data;
    (void)record;
    lock_test_mutex(&self->mutex);
    self->count++;
    unlock_test_mutex(&self->mutex);
    if (self->react != NULL) {
        self->react(self->more);
    }
    return WEIVEL_C_STATUS_OK;
}

static inline void *host_world_schedule(void *data, int64_t at, weivel_c_task_callback callback, void *callback_data) {
    (void)at;
    (void)callback;
    (void)callback_data;
    return data;
}

static inline int host_world_cancel(void *data, void *task) {
    (void)data;
    (void)task;
    return 1;
}

typedef struct host_world {
    weivel_c_host *host;
    telling sink;
    test_mutex sink_mutex;
    counted_log log;
    trace heard;
    outcomes told;
    int64_t now;
    /** What the sink does about each failure, outside its own mutex. */
    void (*sink_react)(void *more);
    void *sink_more;
    /** What each reading of the clock does first; the wrapper reads it outside every lock. */
    void (*clock_react)(void *more);
    void *clock_more;
} host_world;

/** A clock the test moves, and a scheduler that keeps a task and never runs one. */
static inline int64_t host_world_now(void *data) {
    host_world *self = data;
    if (self->clock_react != NULL) {
        self->clock_react(self->clock_more);
    }
    return self->now;
}

/** A sink that records under a mutex of its own, since a host may tell it from several threads. */
static inline weivel_c_status tell_guarded(void *data, const weivel_c_failure *failure) {
    host_world *self = data;
    weivel_c_status answer;
    lock_test_mutex(&self->sink_mutex);
    answer = tell(&self->sink, failure);
    unlock_test_mutex(&self->sink_mutex);
    if (self->sink_react != NULL) {
        self->sink_react(self->sink_more);
    }
    return answer;
}

static inline weivel_c_host_options host_options_of(host_world *self, const weivel_c_core_table *table,
                                                    weivel_c_scheduler *scheduler) {
    weivel_c_host_options options = WEIVEL_INIT(weivel_c_host_options);
    *scheduler = (weivel_c_scheduler)WEIVEL_INIT(weivel_c_scheduler);
    scheduler->now = host_world_now;
    scheduler->schedule = host_world_schedule;
    scheduler->cancel = host_world_cancel;
    scheduler->data = self;
    options.table = table;
    options.scheduler = scheduler;
    options.error_sink.report = tell_guarded;
    options.error_sink.data = self;
    options.log.write = count_log;
    options.log.data = &self->log;
    return options;
}

static inline void begin_host_world_with(host_world *self, const weivel_c_core_table *table,
                                         const weivel_core_options *core_options) {
    weivel_c_scheduler scheduler;
    weivel_c_host_options options;
    memset(self, 0, sizeof *self);
    start_test_mutex(&self->sink_mutex);
    start_test_mutex(&self->log.mutex);
    start_test_mutex(&self->told.mutex);
    start_trace(&self->heard);
    self->told.also = &self->heard;
    forget_crossings();
    options = host_options_of(self, table, &scheduler);
    options.core_options = core_options;
    CHECK(weivel_c_host_create(&options, &self->host, NULL) == WEIVEL_C_STATUS_OK);
}

static inline void begin_host_world_on(host_world *self, const weivel_c_core_table *table) {
    begin_host_world_with(self, table, NULL);
}

static inline void begin_host_world(host_world *self) {
    begin_host_world_on(self, &counting_table);
}

static inline uint32_t sink_count(host_world *self) {
    uint32_t count;
    lock_test_mutex(&self->sink_mutex);
    count = self->sink.count;
    unlock_test_mutex(&self->sink_mutex);
    return count;
}

static inline void end_host_world(host_world *self) {
    uint32_t finished = 0;
    CHECK(weivel_c_host_destroy(self->host, &finished) == WEIVEL_C_STATUS_OK);
    CHECK(finished == 1);
    CHECK(weivel_c_host_free(self->host) == WEIVEL_C_STATUS_OK);
}

/** One plugin as a test builds it: its manifest value, its entry's own data and its handle. */
typedef struct test_plugin {
    weivel_manifest manifest;
    weivel_service_offer offers[4];
    uint32_t offer_count;
    weivel_service_want wants[4];
    uint32_t want_count;
    const char *capabilities_provided[2];
    uint32_t capabilities_provided_count;
    const char *capabilities_consumed[2];
    uint32_t capabilities_consumed_count;
    weivel_c_entry entry;
    weivel_handle handle;
    host_world *in;
    weivel_c_context *context;
    weivel_c_completion *activation;
    weivel_c_completion *deactivation;
    void *more;
} test_plugin;

static inline void name_test_plugin(test_plugin *self, host_world *in, const char *name) {
    memset(self, 0, sizeof *self);
    self->manifest = (weivel_manifest)WEIVEL_INIT(weivel_manifest);
    self->manifest.import_name = name;
    self->manifest.api = 1;
    self->entry = (weivel_c_entry)WEIVEL_INIT(weivel_c_entry);
    self->entry.data = self;
    self->in = in;
}

static inline void offers_service(test_plugin *self, const char *id) {
    weivel_service_offer *offer = &self->offers[self->offer_count++];
    *offer = (weivel_service_offer)WEIVEL_INIT(weivel_service_offer);
    offer->id = id;
}

static inline weivel_service_want *wants_service(test_plugin *self, const char *id) {
    weivel_service_want *want = &self->wants[self->want_count++];
    *want = want_of(id);
    return want;
}

static inline void lay_out_manifest(test_plugin *self) {
    self->manifest.services.provides.items = self->offer_count == 0 ? NULL : self->offers;
    self->manifest.services.provides.count = self->offer_count;
    self->manifest.services.consumes.items = self->want_count == 0 ? NULL : self->wants;
    self->manifest.services.consumes.count = self->want_count;
    self->manifest.capabilities.provides.items =
        self->capabilities_provided_count == 0 ? NULL : self->capabilities_provided;
    self->manifest.capabilities.provides.count = self->capabilities_provided_count;
    self->manifest.capabilities.consumes.items =
        self->capabilities_consumed_count == 0 ? NULL : self->capabilities_consumed;
    self->manifest.capabilities.consumes.count = self->capabilities_consumed_count;
}

static inline void register_test_plugin(test_plugin *self) {
    lay_out_manifest(self);
    CHECK(weivel_c_host_register(self->in->host, &self->manifest, &self->entry, &self->handle, NULL) ==
          WEIVEL_C_STATUS_OK);
}

/** Activates `count` plugins with `edge_source`, answers whether the plan's order is theirs, and releases the plan. */
static inline int activate_in_order(host_world *in, test_plugin *const *plugins, uint32_t count,
                                    uint32_t edge_source) {
    weivel_handle handles[HOST_WORLD_PLUGINS];
    weivel_c_activation activation = WEIVEL_INIT(weivel_c_activation);
    weivel_activation_plan plan = WEIVEL_INIT(weivel_activation_plan);
    uint32_t index;
    int in_order;
    CHECK(count <= HOST_WORLD_PLUGINS);
    for (index = 0; index < count && index < HOST_WORLD_PLUGINS; index++) {
        handles[index] = plugins[index]->handle;
    }
    activation.plugins = handles;
    activation.count = count;
    activation.edge_source = edge_source;
    activation.listener.hear = hear_outcome;
    activation.listener.data = &in->told;
    CHECK(weivel_c_host_activate(in->host, &activation, &plan, NULL) == WEIVEL_C_STATUS_OK);
    in_order = plan.order_count == count;
    for (index = 0; in_order && index < count; index++) {
        in_order = plan.order[index] == handles[index];
    }
    CHECK(weivel_c_host_plan_release(in->host, &plan) == WEIVEL_C_STATUS_OK);
    return in_order;
}

static inline uint32_t state_of(host_world *in, const test_plugin *plugin) {
    uint32_t state = 0;
    weivel_c_status status = weivel_c_host_plugin_state(in->host, plugin->handle, &state);
    return status == WEIVEL_C_STATUS_OK ? state : 0;
}

/** The ledger row's status of a plugin, or 0 when the core holds no row for it. */
static inline uint32_t ledger_status_of(host_world *in, const test_plugin *plugin, weivel_ledger_row *kept) {
    weivel_ledger_row row = WEIVEL_INIT(weivel_ledger_row);
    weivel_c_core *core = NULL;
    uint32_t status = 0;
    CHECK(weivel_c_host_core(in->host, &core) == WEIVEL_C_STATUS_OK);
    if (weivel_c_core_ledger_entry(core, plugin->handle, &row, NULL) == WEIVEL_C_STATUS_OK) {
        status = row.status;
        if (kept != NULL) {
            *kept = row;
            return status;
        }
        CHECK(weivel_c_core_ledger_row_release(core, &row) == WEIVEL_C_STATUS_OK);
    }
    return status;
}

/** Answers what a want settled to, in the trace, as a vector writes it. */
static inline weivel_c_status trace_settlement(void *data, const weivel_c_settlement *settlement) {
    test_plugin *self = data;
    if (settlement->service != NULL) {
        trace_line(&self->in->heard, "settled", self->manifest.import_name, settlement->id);
    } else {
        char line[HOST_WORLD_TEXT];
        snprintf(line, sizeof line, "%s %s", settlement->id,
                 settlement->refusal.code == WEIVEL_REFUSAL_STOPPED ? "stopped" : "refused");
        trace_line(&self->in->heard, "rejected", self->manifest.import_name, line);
    }
    return WEIVEL_C_STATUS_OK;
}

/** A get through a plugin's context, recorded as a vector records it. */
static inline void traced_get(test_plugin *self, weivel_c_context *context, const char *id) {
    void *service = NULL;
    weivel_c_status got = weivel_c_context_get_service(context, id, &service, NULL);
    trace_line(&self->in->heard, got == WEIVEL_C_STATUS_OK ? "got" : "missed", self->manifest.import_name, id);
}

#endif
