#ifndef WEIVEL_C_TEST_CONTEXT_WORLD_H
#define WEIVEL_C_TEST_CONTEXT_WORLD_H

/**
 * @file
 * What the context tests share: a table that counts every crossing into the real core, a host's worth of core,
 * handles, timer and contexts on a clock the test owns, plugins opened as a host opens them, and callbacks that record
 * what they were handed.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "core/fixture.h"
#include "threads.h"

#if !defined(WEIVEL_C_USING_SHARED)
#include "../../src/handles/table.h"
#endif

#define WORLD_RECORDS 32
#define WORLD_TEXT 192

/** Every crossing the fixture's own counters do not count. */
static uint32_t other_crossings;
/** What the layer last asked a want with, and how often it asked one at all. */
static weivel_want_options asked_options;
static int asked_with_options;
static uint32_t wants_asked;
/** What the layer last reported as a capacity, the time it reported it at, and how often it reported one. */
static weivel_capacity reported_capacity;
static int64_t reported_now;
static uint32_t capacities_reported;
/** When not 0, the handle the core's next registration of a service is answered as, in place of its own. */
static weivel_handle registration_answered_instead;

#define COUNTED(name, parameters, arguments)        \
    static weivel_status every_##name parameters {  \
        other_crossings++;                          \
        return real_table.name arguments;           \
    }

COUNTED(supports, (weivel_core *core, const weivel_manifest *manifest, weivel_support *answer, uint32_t *pending),
        (core, manifest, answer, pending))
COUNTED(support_release, (weivel_support *answer), (answer))
COUNTED(register_manifest,
        (weivel_core *core, const weivel_manifest *manifest, weivel_handle *plugin, uint32_t *pending),
        (core, manifest, plugin, pending))
COUNTED(plan_activation,
        (weivel_core *core, const weivel_handle *plugins, uint32_t count, uint32_t edge_source,
         const weivel_strings *host_provided, weivel_activation_plan *plan, uint32_t *pending),
        (core, plugins, count, edge_source, host_provided, plan, pending))
COUNTED(activation_plan_release, (weivel_activation_plan *plan), (plan))
COUNTED(open_plugin,
        (weivel_core *core, weivel_handle plugin, const weivel_plugin_options *options, uint32_t *pending),
        (core, plugin, options, pending))
COUNTED(plugin_payload, (weivel_core *core, weivel_handle plugin, uint64_t *payload, uint32_t *pending),
        (core, plugin, payload, pending))
COUNTED(release_plugin, (weivel_core *core, weivel_handle plugin, uint32_t *pending), (core, plugin, pending))
COUNTED(quarantine_plugin,
        (weivel_core *core, weivel_handle plugin, const weivel_quarantine_reason *reason, uint32_t *pending),
        (core, plugin, reason, pending))
COUNTED(forget_plugin, (weivel_core *core, weivel_handle plugin, uint32_t *pending), (core, plugin, pending))
COUNTED(unregister, (weivel_core *core, weivel_handle handle, uint32_t *pending), (core, handle, pending))
COUNTED(get_service,
        (weivel_core *core, weivel_handle plugin, const char *id, weivel_handle *implementation, uint32_t *pending),
        (core, plugin, id, implementation, pending))
COUNTED(watch_service,
        (weivel_core *core, weivel_handle plugin, const char *id, weivel_handle *watch, uint32_t *pending),
        (core, plugin, id, watch, pending))
COUNTED(service_ids, (weivel_core *core, weivel_handle plugin, weivel_service_list *list, uint32_t *pending),
        (core, plugin, list, pending))
COUNTED(unbound_services, (weivel_core *core, weivel_handle plugin, weivel_service_list *list, uint32_t *pending),
        (core, plugin, list, pending))
COUNTED(service_list_release, (weivel_service_list *list), (list))
COUNTED(provide_capability,
        (weivel_core *core, weivel_handle plugin, const char *id, weivel_handle *implementation, uint32_t *pending),
        (core, plugin, id, implementation, pending))
COUNTED(verify_activation, (weivel_core *core, weivel_handle plugin, uint32_t *pending), (core, plugin, pending))
COUNTED(all_capabilities,
        (weivel_core *core, weivel_handle plugin, const char *id, weivel_capability_list *list, uint32_t *pending),
        (core, plugin, id, list, pending))
COUNTED(capability_list_release, (weivel_capability_list *list), (list))
COUNTED(watch_capability,
        (weivel_core *core, weivel_handle plugin, const char *id, weivel_handle *watch, uint32_t *pending),
        (core, plugin, id, watch, pending))
COUNTED(ledger, (weivel_core *core, weivel_ledger_snapshot *snapshot, uint32_t *pending), (core, snapshot, pending))
COUNTED(ledger_snapshot_release, (weivel_ledger_snapshot *snapshot), (snapshot))
COUNTED(ledger_entry, (weivel_core *core, weivel_handle plugin, weivel_ledger_row *row, uint32_t *pending),
        (core, plugin, row, pending))
COUNTED(ledger_row_release, (weivel_ledger_row *row), (row))

static weivel_status every_want_service(weivel_core *core, weivel_handle plugin, const char *id,
                                        const weivel_want_options *options, weivel_handle *want,
                                        uint32_t *pending) {
    other_crossings++;
    wants_asked++;
    asked_with_options = options != NULL;
    if (options != NULL) {
        asked_options = *options;
    }
    return real_table.want_service(core, plugin, id, options, want, pending);
}

static weivel_status every_report_capacity(weivel_core *core, weivel_handle want, weivel_handle registration,
                                           const weivel_capacity *capacity, int64_t now, uint32_t *pending) {
    other_crossings++;
    capacities_reported++;
    reported_capacity = *capacity;
    reported_now = now;
    return real_table.report_capacity(core, want, registration, capacity, now, pending);
}

static weivel_status every_register_service(weivel_core *core, weivel_handle owner, const char *id,
                                            weivel_handle *registration, uint32_t *pending) {
    weivel_status status;
    other_crossings++;
    status = real_table.register_service(core, owner, id, registration, pending);
    if (status == WEIVEL_STATUS_OK && registration_answered_instead != 0) {
        *registration = registration_answered_instead;
        registration_answered_instead = 0;
    }
    return status;
}

#define COUNT_EVERY(name) counting_table.name = every_##name;

static inline int load_every_table(const char *core_path) {
    if (!load_tables(core_path)) {
        return 0;
    }
    COUNT_EVERY(supports)
    COUNT_EVERY(support_release)
    COUNT_EVERY(register_manifest)
    COUNT_EVERY(plan_activation)
    COUNT_EVERY(activation_plan_release)
    COUNT_EVERY(open_plugin)
    COUNT_EVERY(plugin_payload)
    COUNT_EVERY(release_plugin)
    COUNT_EVERY(quarantine_plugin)
    COUNT_EVERY(forget_plugin)
    COUNT_EVERY(register_service)
    COUNT_EVERY(unregister)
    COUNT_EVERY(get_service)
    COUNT_EVERY(want_service)
    COUNT_EVERY(report_capacity)
    COUNT_EVERY(watch_service)
    COUNT_EVERY(service_ids)
    COUNT_EVERY(unbound_services)
    COUNT_EVERY(service_list_release)
    COUNT_EVERY(provide_capability)
    COUNT_EVERY(verify_activation)
    COUNT_EVERY(all_capabilities)
    COUNT_EVERY(capability_list_release)
    COUNT_EVERY(watch_capability)
    COUNT_EVERY(ledger)
    COUNT_EVERY(ledger_snapshot_release)
    COUNT_EVERY(ledger_entry)
    COUNT_EVERY(ledger_row_release)
    return 1;
}

/** Every crossing into the core since the counters were last forgotten, whichever function it went through. */
static inline uint32_t every_crossing(void) {
    return other_crossings + crossed.core_create + crossed.core_destroy + crossed.next_event +
           crossed.event_release + crossed.next_deadline + crossed.advance + crossed.handle_state;
}

static inline void forget_every_crossing(void) {
    forget_crossings();
    other_crossings = 0;
    wants_asked = 0;
    asked_with_options = 0;
    capacities_reported = 0;
    reported_now = 0;
    registration_answered_instead = 0;
    memset(&asked_options, 0, sizeof asked_options);
    memset(&reported_capacity, 0, sizeof reported_capacity);
}

/**
 * A clock the test moves and a scheduler that keeps a task and never runs one. `on_read`, when set, runs each time the
 * clock is read, outside every lock of the layer's, which is where a test holds a thread still between two steps.
 */
typedef struct world_clock {
    int64_t now;
    uint32_t reads;
    void (*on_read)(void *more);
    void *more;
} world_clock;

static int64_t world_now(void *data) {
    world_clock *self = data;
    self->reads++;
    if (self->on_read != NULL) {
        self->on_read(self->more);
    }
    return self->now;
}

static void *world_schedule(void *data, int64_t at, weivel_c_task_callback callback, void *callback_data) {
    (void)at;
    (void)callback;
    (void)callback_data;
    return data;
}

static int world_cancel(void *data, void *task) {
    (void)data;
    (void)task;
    return 1;
}

typedef struct logged_line {
    uint32_t level;
    weivel_handle plugin;
    int named;
    char name[WORLD_TEXT];
    char text[WORLD_TEXT];
} logged_line;

/** The host's log: what it was handed, the status it answers, and what it does meanwhile. */
typedef struct logged {
    logged_line items[WORLD_RECORDS];
    uint32_t count;
    weivel_c_status answer;
    void (*react)(void *more);
    void *more;
} logged;

static weivel_c_status write_log(void *data, const weivel_c_log_record *record) {
    logged *self = data;
    if (self->count < WORLD_RECORDS) {
        logged_line *line = &self->items[self->count];
        line->level = record->level;
        line->plugin = record->plugin;
        line->named = record->plugin_name != NULL;
        snprintf(line->name, WORLD_TEXT, "%s", record->plugin_name == NULL ? "" : record->plugin_name);
        snprintf(line->text, WORLD_TEXT, "%s", record->text);
    }
    self->count++;
    if (self->react != NULL) {
        self->react(self->more);
    }
    return self->answer;
}

typedef struct world {
    telling sink;
    hearing events;
    logged log;
    world_clock clock;
    weivel_c_core *core;
    weivel_c_handles *handles;
    weivel_c_timer *timer;
    weivel_c_contexts *contexts;
} world;

/** The core of a world, made first so a test can put it on an allocator of its own before anything else is made. */
static inline void begin_world_core(world *self, const weivel_core_options *core_options) {
    memset(self, 0, sizeof *self);
    forget_every_crossing();
    self->core = make_core(core_options, &self->events, &self->sink);
}

static inline void begin_world_rest(world *self, int with_clock) {
    weivel_c_scheduler scheduler = WEIVEL_INIT(weivel_c_scheduler);
    weivel_c_contexts_options options = WEIVEL_INIT(weivel_c_contexts_options);
    scheduler.now = world_now;
    scheduler.schedule = world_schedule;
    scheduler.cancel = world_cancel;
    scheduler.data = &self->clock;
    CHECK(weivel_c_handles_create(&self->handles) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_timer_create(self->core, with_clock ? &scheduler : NULL, NULL, &self->timer) == WEIVEL_C_STATUS_OK);
    options.core = self->core;
    options.handles = self->handles;
    options.timer = self->timer;
    options.log.write = write_log;
    options.log.data = &self->log;
    CHECK(weivel_c_contexts_create(&options, &self->contexts) == WEIVEL_C_STATUS_OK);
}

static inline void begin_world(world *self, const weivel_core_options *core_options, int with_clock) {
    begin_world_core(self, core_options);
    begin_world_rest(self, with_clock);
}

#if !defined(WEIVEL_C_USING_SHARED)

static inline void release_plainly(void *data, void *memory) {
    (void)data;
    free(memory);
}

/** An allocator that fails the one allocation it is told to, and every one while `fails_all` is set. */
typedef struct scarce_memory {
    uint32_t allocations;
    uint32_t fail_at;
    int fails_all;
} scarce_memory;

static inline void *allocate_scarcely(void *data, size_t size) {
    scarce_memory *self = data;
    self->allocations++;
    return self->fails_all || self->allocations == self->fail_at ? NULL : malloc(size);
}

/** What the filler entries of a table brought to the brink stand for; only its address matters. */
static int world_filler_object;

/** Fills the table until its next addition must grow it, so the next allocation it makes is one a test can fail. */
static inline void bring_the_table_to_the_brink(world *in, scarce_memory *memory) {
    weivel_c_handle_entry filler = WEIVEL_INIT(weivel_c_handle_entry);
    weivel_c_status reserved = WEIVEL_C_STATUS_OK;
    filler.kind = WEIVEL_C_HANDLE_WATCH;
    filler.object = &world_filler_object;
    while (reserved == WEIVEL_C_STATUS_OK) {
        memory->fails_all = 1;
        reserved = weivel_c_handles_reserve(in->handles);
        memory->fails_all = 0;
        if (reserved == WEIVEL_C_STATUS_OK) {
            filler.handle += 1;
            CHECK(weivel_c_handles_add_reserved(in->handles, &filler, NULL) == WEIVEL_C_STATUS_OK);
        }
    }
    CHECK(reserved == WEIVEL_C_STATUS_OUT_OF_MEMORY);
}

#endif

/** In a build that reaches the table's seams, that nothing reserved in it is left unused once the contexts are freed. */
static inline void check_no_reservation_is_left(world *self) {
#if !defined(WEIVEL_C_USING_SHARED)
    CHECK(weivel_c_handles_reserved(self->handles) == 0);
#else
    (void)self;
#endif
}

static inline void end_world(world *self) {
    CHECK(weivel_c_timer_destroy(self->timer) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_destroy(self->core) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_contexts_free(self->contexts) == WEIVEL_C_STATUS_OK);
    check_no_reservation_is_left(self);
    CHECK(weivel_c_timer_free(self->timer) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_free(self->core) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_handles_free(self->handles) == WEIVEL_C_STATUS_OK);
}

static inline uint32_t records_of(world *self) {
    uint32_t records = 0;
    CHECK(weivel_c_contexts_count(self->contexts, NULL, &records) == WEIVEL_C_STATUS_OK);
    return records;
}

static inline uint32_t holdings_of(world *self) {
    uint32_t holdings = 0;
    CHECK(weivel_c_handles_count(self->handles, NULL, &holdings) == WEIVEL_C_STATUS_OK);
    return holdings;
}

static inline uint32_t entries_of(world *self) {
    uint32_t entries = 0;
    CHECK(weivel_c_handles_count(self->handles, &entries, NULL) == WEIVEL_C_STATUS_OK);
    return entries;
}

static inline uint32_t told_of(const world *self, uint32_t kind) {
    uint32_t found = 0;
    uint32_t index;
    for (index = 0; index < self->sink.count && index < 16; index++) {
        found += self->sink.items[index].failure.kind == kind;
    }
    return found;
}

/** One plugin as a host keeps it: its manifest value, its handle and its context. */
typedef struct plugin {
    weivel_manifest manifest;
    weivel_service_offer offers[4];
    uint32_t offer_count;
    weivel_service_want wants[4];
    uint32_t want_count;
    const char *capabilities_provided[2];
    uint32_t capabilities_provided_count;
    const char *capabilities_consumed[2];
    uint32_t capabilities_consumed_count;
    weivel_handle handle;
    weivel_c_context *context;
} plugin;

static inline void name_plugin(plugin *self, const char *name) {
    memset(self, 0, sizeof *self);
    self->manifest = (weivel_manifest)WEIVEL_INIT(weivel_manifest);
    self->manifest.import_name = name;
    self->manifest.api = 1;
}

static inline weivel_service_offer *provides(plugin *self, const char *id) {
    weivel_service_offer *offer = &self->offers[self->offer_count++];
    *offer = (weivel_service_offer)WEIVEL_INIT(weivel_service_offer);
    offer->id = id;
    return offer;
}

static inline weivel_service_want *consumes(plugin *self, const char *id) {
    weivel_service_want *want = &self->wants[self->want_count++];
    *want = want_of(id);
    return want;
}

/** Registers the plugin, builds its context and opens it with the context's token, as a host does. */
static inline void open_in(world *in, plugin *self) {
    weivel_plugin_options options = WEIVEL_INIT(weivel_plugin_options);
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
    CHECK(weivel_c_core_register_manifest(in->core, &self->manifest, &self->handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_create(in->contexts, self->handle, &self->manifest, &self->context) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_token(self->context, &options.payload) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_open_plugin(in->core, self->handle, &options, NULL) == WEIVEL_C_STATUS_OK);
}

/** A provider `name` of the service `<name>:a` and a consumer `c` of it, opened in a new world. */
static inline void begin_pair(world *in, plugin *provider, plugin *consumer, int with_clock) {
    begin_world(in, NULL, with_clock);
    name_plugin(provider, "p");
    provides(provider, "p:a");
    name_plugin(consumer, "c");
    consumes(consumer, "p:a");
    open_in(in, provider);
    open_in(in, consumer);
}

typedef struct settled_record {
    uint32_t count;
    weivel_handle want;
    void *service;
    uint32_t code;
    uint32_t has_fix;
    uint32_t degraded;
    char id[WORLD_TEXT];
    char subject[WORLD_TEXT];
    char fix[WORLD_TEXT];
    char detail[WORLD_TEXT];
    weivel_c_status answer;
    void (*react)(void *more);
    void *more;
} settled_record;

static inline void copy_text(char *into, const char *text) {
    snprintf(into, WORLD_TEXT, "%s", text == NULL ? "" : text);
}

static weivel_c_status record_settlement(void *data, const weivel_c_settlement *settlement) {
    settled_record *self = data;
    self->count++;
    self->want = settlement->want;
    self->service = settlement->service;
    self->code = settlement->refusal.code;
    self->has_fix = settlement->refusal.has_fix;
    self->degraded = settlement->degraded;
    copy_text(self->id, settlement->id);
    copy_text(self->subject, settlement->refusal.subject);
    copy_text(self->fix, settlement->refusal.fix);
    copy_text(self->detail, settlement->refusal.detail);
    if (self->react != NULL) {
        self->react(self->more);
    }
    return self->answer;
}

static inline weivel_c_want_callback settling(settled_record *record) {
    weivel_c_want_callback callback = {record_settlement, record};
    return callback;
}

typedef struct watched_record {
    uint32_t count;
    uint32_t changes[WORLD_RECORDS];
    void *objects[WORLD_RECORDS];
    char id[WORLD_TEXT];
    weivel_c_status answer;
    void (*react)(void *more);
    void *more;
} watched_record;

static inline weivel_c_status record_change(watched_record *self, uint32_t change, void *object, const char *id) {
    if (self->count < WORLD_RECORDS) {
        self->changes[self->count] = change;
        self->objects[self->count] = object;
    }
    self->count++;
    copy_text(self->id, id);
    if (self->react != NULL) {
        self->react(self->more);
    }
    return self->answer;
}

static weivel_c_status record_service_change(void *data, const weivel_c_service_change *change) {
    return record_change(data, change->change, change->service, change->id);
}

static weivel_c_status record_capability_change(void *data, const weivel_c_capability_change *change) {
    return record_change(data, change->change, change->implementation, change->id);
}

static inline weivel_c_service_watcher watching_services(watched_record *record) {
    weivel_c_service_watcher watcher = {record_service_change, record};
    return watcher;
}

static inline weivel_c_capability_watcher watching_capabilities(watched_record *record) {
    weivel_c_capability_watcher watcher = {record_capability_change, record};
    return watcher;
}

typedef struct reported_event {
    uint32_t kind;
    uint32_t reach;
    void *object;
    uint32_t code;
    uint32_t has_fix;
    char id[WORLD_TEXT];
    char subject[WORLD_TEXT];
} reported_event;

typedef struct reported_record {
    reported_event items[WORLD_RECORDS];
    uint32_t count;
    weivel_c_status answer;
    void (*react)(void *more);
    void *more;
} reported_record;

static weivel_c_status record_plugin_event(void *data, const weivel_c_plugin_event *event) {
    reported_record *self = data;
    if (self->count < WORLD_RECORDS) {
        reported_event *item = &self->items[self->count];
        item->kind = event->kind;
        item->reach = event->reach;
        item->object = event->object;
        item->code = event->refusal.code;
        item->has_fix = event->refusal.has_fix;
        copy_text(item->id, event->id);
        copy_text(item->subject, event->refusal.subject);
    }
    self->count++;
    if (self->react != NULL) {
        self->react(self->more);
    }
    return self->answer;
}

static inline uint64_t listen_to(plugin *self, reported_record *record) {
    weivel_c_plugin_listener listener = {record_plugin_event, record};
    uint64_t registration = 0;
    CHECK(weivel_c_context_add_listener(self->context, &listener, &registration) == WEIVEL_C_STATUS_OK);
    CHECK(registration != 0);
    return registration;
}

/** Runs `run` as a step of the dispatch, which is where a host ends a plugin's entries and frees its context. */
static inline void in_dispatch(world *in, weivel_c_status (*run)(void *data, weivel_c_core *core), void *data) {
    weivel_c_step step = WEIVEL_INIT(weivel_c_step);
    uint32_t dispatched_here = 0;
    step.run = run;
    step.data = data;
    CHECK(weivel_c_core_defer(in->core, &step, &dispatched_here) == WEIVEL_C_STATUS_OK);
    CHECK(dispatched_here == 1);
}

static weivel_c_status end_entries_step(void *data, weivel_c_core *core) {
    (void)core;
    return weivel_c_context_end_entries(data);
}

static weivel_c_status free_context_step(void *data, weivel_c_core *core) {
    (void)core;
    return weivel_c_context_free(data);
}

static inline void end_entries_of(world *in, plugin *self) {
    in_dispatch(in, end_entries_step, self->context);
}

static inline void free_context_of(world *in, plugin *self) {
    in_dispatch(in, free_context_step, self->context);
    self->context = NULL;
}

#endif
