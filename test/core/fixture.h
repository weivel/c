#ifndef WEIVEL_C_TEST_CORE_FIXTURE_H
#define WEIVEL_C_TEST_CORE_FIXTURE_H

/**
 * @file
 * What the wrapper's tests share: a table that counts each crossing into the real core and can bend one, a listener
 * and a sink that record what they are told, and plugins registered in a line.
 */

#include <stdlib.h>

#include <weivel/c.h>

#include "check.h"

#define FIXTURE_RECORDS 256
#define FIXTURE_TEXT 256

typedef struct crossings {
    uint32_t core_create;
    uint32_t core_destroy;
    uint32_t next_event;
    uint32_t events;
    uint32_t event_release;
    uint32_t advance;
    uint32_t handle_state;
    uint32_t unreleased;
    uint32_t most_unreleased;
    uint32_t unsized_events;
    int64_t advanced_to;
} crossings;

static weivel_c_core_table real_table;
static weivel_c_core_table counting_table;
static crossings crossed;

/** Lets a test change an event between the core and the layer; NULL leaves it. */
static void (*bend_event)(weivel_event *event);
/** Added to what weivel_handle_state reports waiting, so a test can report an event that is not there. */
static uint32_t phantom_events;
/** 1 makes weivel_handle_state a call no layer should build: its required `condition` is withheld. */
static int withhold_condition;

static weivel_status counted_core_create(const weivel_core_options *options, weivel_core **core,
                                         uint32_t *pending_events) {
    crossed.core_create++;
    return real_table.core_create(options, core, pending_events);
}

static weivel_status counted_core_destroy(weivel_core *core) {
    crossed.core_destroy++;
    return real_table.core_destroy(core);
}

static weivel_status counted_next_event(weivel_core *core, weivel_event *event, uint32_t *pending_events) {
    weivel_status status;
    crossed.next_event++;
    crossed.unsized_events += event->size != sizeof *event || event->kind != WEIVEL_EVENT_NONE;
    status = real_table.next_event(core, event, pending_events);
    if (status == WEIVEL_STATUS_OK) {
        crossed.events++;
        crossed.unreleased++;
        crossed.most_unreleased =
            crossed.unreleased > crossed.most_unreleased ? crossed.unreleased : crossed.most_unreleased;
        if (bend_event != NULL) {
            bend_event(event);
        }
    }
    return status;
}

static weivel_status counted_event_release(weivel_event *event) {
    crossed.event_release++;
    crossed.unreleased--;
    return real_table.event_release(event);
}

static weivel_status counted_advance(weivel_core *core, int64_t now, uint32_t *pending_events) {
    crossed.advance++;
    crossed.advanced_to = now;
    return real_table.advance(core, now, pending_events);
}

static weivel_status counted_handle_state(weivel_core *core, weivel_handle handle, uint32_t *condition,
                                          uint32_t *pending_events) {
    weivel_status status;
    crossed.handle_state++;
    status = real_table.handle_state(core, handle, withhold_condition ? NULL : condition, pending_events);
    *pending_events += phantom_events;
    return status;
}

static inline int load_tables(const char *core_path) {
    real_table = (weivel_c_core_table)WEIVEL_INIT(weivel_c_core_table);
    if (weivel_c_core_load(core_path, &real_table, NULL) != WEIVEL_C_STATUS_OK) {
        fprintf(stderr, "the core at %s did not load\n", core_path);
        return 0;
    }
    counting_table = real_table;
    counting_table.core_create = counted_core_create;
    counting_table.core_destroy = counted_core_destroy;
    counting_table.next_event = counted_next_event;
    counting_table.event_release = counted_event_release;
    counting_table.advance = counted_advance;
    counting_table.handle_state = counted_handle_state;
    return 1;
}

static inline void forget_crossings(void) {
    memset(&crossed, 0, sizeof crossed);
    bend_event = NULL;
    phantom_events = 0;
    withhold_condition = 0;
}

typedef struct heard {
    uint32_t kind;
    uint64_t sequence;
    weivel_handle implementation;
    uint32_t change;
    uint32_t code;
} heard;

/** A listener's record, and what it does about each event: `react` may call into the core. */
typedef struct hearing {
    heard items[FIXTURE_RECORDS];
    uint32_t count;
    uint32_t depth;
    uint32_t deepest;
    weivel_c_status (*react)(struct hearing *self, weivel_c_core *core, const weivel_c_event *event);
    void *more;
} hearing;

static inline heard record_of(const weivel_c_event *event) {
    heard record = {event->kind, event->sequence, 0, 0, 0};
    if (event->kind == WEIVEL_EVENT_WATCH) {
        record.implementation = event->data.watch.implementation;
        record.change = event->data.watch.change;
    } else if (event->kind == WEIVEL_EVENT_REFUSAL) {
        record.code = event->data.refused.refusal.code;
    } else if (event->kind == WEIVEL_EVENT_REACH_REFUSED) {
        record.code = event->data.reach.refusal.code;
    } else if (event->kind == WEIVEL_EVENT_WANT_SETTLED) {
        record.implementation = event->data.want.implementation;
        record.code = event->data.want.refusal.code;
    }
    return record;
}

static weivel_c_status hear(void *data, weivel_c_core *core, const weivel_c_event *event) {
    hearing *self = data;
    weivel_c_status status = WEIVEL_C_STATUS_OK;
    self->depth++;
    self->deepest = self->depth > self->deepest ? self->depth : self->deepest;
    if (self->count < FIXTURE_RECORDS) {
        self->items[self->count] = record_of(event);
    }
    self->count++;
    if (self->react != NULL) {
        status = self->react(self, core, event);
    }
    self->depth--;
    return status;
}

static inline uint32_t count_heard(const hearing *self, uint32_t kind) {
    uint32_t found = 0;
    uint32_t index;
    for (index = 0; index < self->count && index < FIXTURE_RECORDS; index++) {
        found += self->items[index].kind == kind;
    }
    return found;
}

typedef struct told_failure {
    weivel_c_failure failure;
    char text[FIXTURE_TEXT];
} told_failure;

/** A sink's record, and the status it answers. */
typedef struct telling {
    told_failure items[16];
    uint32_t count;
    weivel_c_status answer;
} telling;

static weivel_c_status tell(void *data, const weivel_c_failure *failure) {
    telling *self = data;
    if (self->count < 16) {
        self->items[self->count].failure = *failure;
        snprintf(self->items[self->count].text, FIXTURE_TEXT, "%s", failure->text);
    }
    self->count++;
    return self->answer;
}

/** A core on the counting table whose every event `listener` hears and whose every failure `sink` is told. */
static inline weivel_c_core *make_core(const weivel_core_options *core_options, hearing *listener, telling *sink) {
    weivel_c_core_options options = WEIVEL_INIT(weivel_c_core_options);
    weivel_c_core *core = NULL;
    options.table = &counting_table;
    options.core_options = core_options;
    if (sink != NULL) {
        options.error_sink.report = tell;
        options.error_sink.data = sink;
    }
    if (listener != NULL) {
        options.listener.hear = hear;
        options.listener.data = listener;
    }
    CHECK(weivel_c_core_create(&options, &core) == WEIVEL_C_STATUS_OK);
    return core;
}

static inline void end_core(weivel_c_core *core) {
    CHECK(weivel_c_core_destroy(core) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_free(core) == WEIVEL_C_STATUS_OK);
}

/** Registers and opens a plugin that provides `provided` services and consumes `consumed` ones. */
static inline weivel_handle open_plugin(weivel_c_core *core, const char *name, const char *const *provided,
                                        uint32_t provided_count, const weivel_service_want *consumed,
                                        uint32_t consumed_count) {
    weivel_service_offer offers[4];
    weivel_manifest manifest = WEIVEL_INIT(weivel_manifest);
    weivel_handle plugin = 0;
    uint32_t index;
    for (index = 0; index < provided_count && index < 4; index++) {
        offers[index] = (weivel_service_offer)WEIVEL_INIT(weivel_service_offer);
        offers[index].id = provided[index];
    }
    manifest.import_name = name;
    manifest.api = 1;
    manifest.services.provides.items = provided_count == 0 ? NULL : offers;
    manifest.services.provides.count = provided_count;
    manifest.services.consumes.items = consumed;
    manifest.services.consumes.count = consumed_count;
    CHECK(weivel_c_core_register_manifest(core, &manifest, &plugin, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_open_plugin(core, plugin, NULL, NULL) == WEIVEL_C_STATUS_OK);
    return plugin;
}

static inline weivel_service_want want_of(const char *id) {
    weivel_service_want want = WEIVEL_INIT(weivel_service_want);
    want.id = id;
    return want;
}

#endif
