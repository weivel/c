#ifndef WEIVEL_C_CONFORMANCE_LIFECYCLE_H
#define WEIVEL_C_CONFORMANCE_LIFECYCLE_H

/**
 * @file
 * The `lifecycle` op's run: the entries its plugins are built with, which run the vector's actions in order and
 * record what "The `lifecycle` op" of the kit's docs/vectors/layer/lifecycle.md records, and the trace, the pending
 * wants and what the steps saw.
 */

#include "world.h"

#define LIFECYCLE_LINES 64
#define LIFECYCLE_TEXT 160
#define LIFECYCLE_WANTS 32
#define LIFECYCLE_STEPS 16

struct lifecycle_run;
struct lifecycle_plugin;

/**
 * One entry function's actions, run from `next` on: by the entry function itself, and by a want's callback when an
 * awaited want ends after the entry function has answered pending. `completion` is what that pending answer finishes.
 *
 * @implNote No `size` on any struct here: each is the driver's own and never crosses a boundary.
 */
typedef struct action_sequence {
    struct lifecycle_plugin *owner;
    const weivel_json_value *actions;
    size_t next;
    weivel_c_completion *completion;
    int inside_the_entry;
} action_sequence;

typedef struct lifecycle_plugin {
    struct lifecycle_run *run;
    world_plugin *plugin;
    const weivel_json_value *given;
    action_sequence activation;
    action_sequence deactivation;
} lifecycle_plugin;

/** One want a plugin started: whose, for what, the sequence that awaits it or NULL, and whether it ended. */
typedef struct lifecycle_want {
    lifecycle_plugin *owner;
    const char *id;
    action_sequence *awaiting;
    int ended;
} lifecycle_want;

typedef struct lifecycle_run {
    conformance_world *world;
    lifecycle_plugin plugins[CONFORMANCE_PLUGINS];
    char trace[LIFECYCLE_LINES][LIFECYCLE_TEXT];
    uint32_t trace_count;
    lifecycle_want wants[LIFECYCLE_WANTS];
    uint32_t want_count;
    const char *plans[LIFECYCLE_STEPS][CONFORMANCE_PLUGINS];
    uint32_t plan_lengths[LIFECYCLE_STEPS];
    uint32_t plan_count;
    const char *verifies[LIFECYCLE_STEPS];
    uint32_t verify_count;
    int destroying;
    uint32_t loud_before_destroy;
    uint32_t thrown;
} lifecycle_run;

/** Records one line of the trace: the words joined by one space, an empty word left out. */
void lifecycle_record(lifecycle_run *run, const char *first, const char *second, const char *third);

/** Builds the given plugin's manifest value and entry, and registers it. Answers 1, or 0 with a problem. */
int lifecycle_build_plugin(lifecycle_run *run, const weivel_json_value *given);

/** A get through a context, recorded as `got <plugin> <id>` or `missed <plugin> <id>`. */
void lifecycle_get(lifecycle_run *run, const char *name, weivel_c_context *context, const char *id);

#endif
