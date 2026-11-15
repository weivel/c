#ifndef WEIVEL_C_CONFORMANCE_WORLD_H
#define WEIVEL_C_CONFORMANCE_WORLD_H

/**
 * @file
 * What every op of the driver runs in: one host on the real core, made through the host's public surface, on a clock
 * and a scheduler the driver owns, with a sink and a log that count what reaches them, and the plugins a vector gives,
 * each built as a manifest value and an entry.
 *
 * The scheduler runs a task the layer gave it only when the driver moves time forward over the task's due time, at
 * that time, and never on its own, as LAYER.md's "The `want` op's clock and scheduler" states.
 */

#include <stdint.h>

#include <weivel/c/host.h>
#include <weivel/json.h>

#include "vectors.h"

#define CONFORMANCE_PLUGINS 16
#define CONFORMANCE_DECLARED 8
#define CONFORMANCE_TASKS 64
#define CONFORMANCE_MOVES 32

/** @implNote No `size` on any struct here: each is the driver's own and never crosses a boundary. */
typedef struct virtual_task {
    int64_t at;
    weivel_c_task_callback callback;
    void *callback_data;
    int waiting;
} virtual_task;

typedef struct virtual_time {
    int64_t now;
    virtual_task tasks[CONFORMANCE_TASKS];
    uint32_t task_count;
    /** The clock's time each time a task actually ran, in order: what the layer's timer did. */
    int64_t fired[CONFORMANCE_MOVES];
    uint32_t fired_count;
} virtual_time;

/** One move the layer says it made to its timer: the clock's time, and the deadline armed, unless it disarmed. */
typedef struct timer_move {
    int64_t at;
    int64_t deadline;
    int armed;
} timer_move;

struct conformance_world;

/** One plugin as the driver builds it: its manifest value, its entry, and what its op keeps of it in `more`. */
typedef struct world_plugin {
    struct conformance_world *world;
    const char *name;
    weivel_handle handle;
    weivel_manifest manifest;
    weivel_service_offer offers[CONFORMANCE_DECLARED];
    uint32_t offer_count;
    weivel_service_want wants[CONFORMANCE_DECLARED];
    uint32_t want_count;
    const char *capabilities_provided[CONFORMANCE_DECLARED];
    uint32_t capabilities_provided_count;
    const char *capabilities_consumed[CONFORMANCE_DECLARED];
    uint32_t capabilities_consumed_count;
    weivel_c_entry entry;
    /** The context the host handed the plugin's activate; NULL until it is called. */
    weivel_c_context *context;
    void *more;
} world_plugin;

typedef struct conformance_world {
    weivel_c_host *host;
    virtual_time time;
    timer_move moves[CONFORMANCE_MOVES];
    uint32_t move_count;
    uint32_t sink_failures;
    uint32_t listener_failures;
    uint32_t log_lines;
    uint32_t error_lines;
    /** The sink's and the log's counts as the run left them, before the end's destroy added its own. */
    uint32_t sink_failures_of_the_run;
    uint32_t listener_failures_of_the_run;
    uint32_t error_lines_of_the_run;
    world_plugin plugins[CONFORMANCE_PLUGINS];
    uint32_t plugin_count;
    char *problem;
    int has_problem;
} conformance_world;

/** Makes the host. Answers 1, or 0 with the world's problem written. `problem` is CONFORMANCE_PROBLEM long. */
int conformance_world_begin(conformance_world *world, const char *core_path, const weivel_core_options *core_options,
                            char *problem);

/** Destroys the host, when nothing has yet, and frees it. */
void conformance_world_end(conformance_world *world);

/** Writes the world's first problem, and answers 0 so a caller can return it. */
int conformance_world_fail(conformance_world *world, const char *what, const char *about);

/** A plugin named `name` whose entry calls `activate` with the plugin itself as its data; NULL past the limit. */
world_plugin *conformance_world_add(conformance_world *world, const char *name, weivel_c_entry_function activate);

/** Reads a JSON array of strings into `into`. Answers 1, or 0 with a problem written. */
int conformance_world_strings(conformance_world *world, const weivel_json_value *array, const char **into,
                              uint32_t *count);

/** Lays out the plugin's manifest from its declarations and registers it with its entry. Answers 1, or 0. */
int conformance_world_register(conformance_world *world, world_plugin *plugin);

/** The plugin named `name`, or NULL with a problem written. */
world_plugin *conformance_world_plugin(conformance_world *world, const char *name);

/** The scheduler and the clock of the world, as the host is given them. */
weivel_c_scheduler conformance_virtual_scheduler(conformance_world *world);

/** The host's timer observer: records each move the host's timer tells, at the clock's time. */
weivel_c_status conformance_world_hear_timer_move(void *data, uint32_t armed, int64_t deadline);

/** Runs every task due by `time`, each at its own due time, then stands at `time`. */
void conformance_world_run_until(conformance_world *world, int64_t time);

/** Moves the clock to `time` and runs no task, as a scheduler that has not yet taken its turn. */
void conformance_world_move_clock_to(conformance_world *world, int64_t time);

/** 1 when `value` is an object whose keys are all among the `count` names of `known`; else 0, a problem written. */
int conformance_world_knows_keys(conformance_world *world, const weivel_json_value *value, const char *const *known,
                                 uint32_t count, const char *where);

#endif
