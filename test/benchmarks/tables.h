#ifndef WEIVEL_C_TEST_BENCHMARKS_TABLES_H
#define WEIVEL_C_TEST_BENCHMARKS_TABLES_H

/**
 * @file
 * What the benchmarks reach the core through beside the layer: the real table, a copy of it that counts every crossing
 * a resolve or a call of the layer's makes, and the bare control call, the core's weivel_get_service called through
 * the table with no wrapper, which the wrapper's control call is compared with.
 */

#include <stdio.h>
#include <string.h>

#include <weivel/c.h>

#include "worlds.h"

static weivel_c_core_table real_table;
static weivel_c_core_table counting_table;
static uint32_t crossings;

static weivel_status counted_get_service(weivel_core *core, weivel_handle plugin, const char *id,
                                         weivel_handle *implementation, uint32_t *pending_events) {
    crossings++;
    return real_table.get_service(core, plugin, id, implementation, pending_events);
}

static weivel_status counted_want_service(weivel_core *core, weivel_handle plugin, const char *id,
                                          const weivel_want_options *options, weivel_handle *want,
                                          uint32_t *pending_events) {
    crossings++;
    return real_table.want_service(core, plugin, id, options, want, pending_events);
}

static weivel_status counted_watch_service(weivel_core *core, weivel_handle plugin, const char *id,
                                           weivel_handle *watch, uint32_t *pending_events) {
    crossings++;
    return real_table.watch_service(core, plugin, id, watch, pending_events);
}

static weivel_status counted_next_event(weivel_core *core, weivel_event *event, uint32_t *pending_events) {
    crossings++;
    return real_table.next_event(core, event, pending_events);
}

static weivel_status counted_handle_state(weivel_core *core, weivel_handle handle, uint32_t *condition,
                                          uint32_t *pending_events) {
    crossings++;
    return real_table.handle_state(core, handle, condition, pending_events);
}

static weivel_status counted_next_deadline(weivel_core *core, int64_t *deadline, uint32_t *pending_events) {
    crossings++;
    return real_table.next_deadline(core, deadline, pending_events);
}

static weivel_status counted_advance(weivel_core *core, int64_t now, uint32_t *pending_events) {
    crossings++;
    return real_table.advance(core, now, pending_events);
}

static inline int load_tables(const char *core_path) {
    real_table = (weivel_c_core_table)WEIVEL_INIT(weivel_c_core_table);
    if (weivel_c_core_load(core_path, &real_table, NULL) != WEIVEL_C_STATUS_OK) {
        fprintf(stderr, "the core at %s did not load\n", core_path);
        return 0;
    }
    counting_table = real_table;
    counting_table.get_service = counted_get_service;
    counting_table.want_service = counted_want_service;
    counting_table.watch_service = counted_watch_service;
    counting_table.next_event = counted_next_event;
    counting_table.handle_state = counted_handle_state;
    counting_table.next_deadline = counted_next_deadline;
    counting_table.advance = counted_advance;
    return 1;
}

/** A core made through the bare table, with the same plugin and service as the wrapper's control world. */
typedef struct bare_world {
    weivel_core *core;
    weivel_handle plugin;
} bare_world;

static inline int start_bare_world(bare_world *world) {
    weivel_service_want want = WEIVEL_INIT(weivel_service_want);
    weivel_manifest manifest = WEIVEL_INIT(weivel_manifest);
    weivel_handle registration = 0;
    want.id = benchmark_control_service;
    manifest.import_name = "bench";
    manifest.api = WEIVEL_API_VERSION;
    manifest.services.consumes.items = &want;
    manifest.services.consumes.count = 1;
    memset(world, 0, sizeof *world);
    return real_table.core_create(NULL, &world->core, NULL) == WEIVEL_STATUS_OK &&
           real_table.register_manifest(world->core, &manifest, &world->plugin, NULL) == WEIVEL_STATUS_OK &&
           real_table.open_plugin(world->core, world->plugin, NULL, NULL) == WEIVEL_STATUS_OK &&
           real_table.register_service(world->core, WEIVEL_HOST, benchmark_control_service, &registration, NULL) ==
               WEIVEL_STATUS_OK;
}

/** The bare control call; `data` is the bare world. */
static int bare_control_call(void *data, uint32_t call) {
    bare_world *world = data;
    weivel_handle implementation = 0;
    (void)call;
    return real_table.get_service(world->core, world->plugin, benchmark_control_service, &implementation, NULL) ==
               WEIVEL_STATUS_OK &&
           implementation != 0;
}

static inline void end_bare_world(bare_world *world) {
    if (world->core != NULL) {
        real_table.core_destroy(world->core);
        world->core = NULL;
    }
}

#endif
