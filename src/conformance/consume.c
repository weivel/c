/**
 * @file
 * The `consume` op: plugins declaring capabilities, opened through the host's activation with an activate that stays
 * pending, so each is opened and none is verified before a `verify` step asks the host to; then the vector's steps,
 * each a plugin's call through its own context or the host's own call.
 *
 * In C a listener "throws" by answering a status other than WEIVEL_C_STATUS_OK, which the layer hands its error sink.
 * A refusal of the run is a refusal a step's call through a context answered; a `verify` step's refusal is the check
 * the step makes, which the host acts on, and is no refusal of the run, as LAYER.md states for the refused verify.
 */

#include <stdio.h>
#include <string.h>

#include "observe.h"
#include "ops.h"

#define CONSUME_WATCHES 8
#define CONSUME_HEARD 16
#define CONSUME_REPORTED 16
#define CONSUME_TEXT 128

typedef struct heard_change {
    const char *provider;
    uint32_t change;
} heard_change;

/** One watch a step made, and what its listener was handed, in order. */
typedef struct consume_watch {
    heard_change heard[CONSUME_HEARD];
    uint32_t count;
    int throws;
} consume_watch;

typedef struct reported_reach {
    const char *plugin;
    char id[CONSUME_TEXT];
} reported_reach;

typedef struct consume_run {
    conformance_world *world;
    consume_watch watches[CONSUME_WATCHES];
    uint32_t watch_count;
    reported_reach reported[CONSUME_REPORTED];
    uint32_t reported_count;
    weivel_c_refusal first_refusal;
    int refused;
} consume_run;

static uint32_t open_and_wait(void *data, weivel_c_context *context, weivel_c_completion *completion,
                              const char **reason) {
    world_plugin *plugin = data;
    (void)completion;
    (void)reason;
    plugin->context = context;
    return WEIVEL_C_ENTRY_PENDING;
}

static weivel_c_status hear_reach(void *data, const weivel_c_plugin_event *event) {
    world_plugin *plugin = data;
    consume_run *run = plugin->more;
    if (event->kind == WEIVEL_C_PLUGIN_EVENT_REACH_REFUSED && run->reported_count < CONSUME_REPORTED) {
        reported_reach *reported = &run->reported[run->reported_count++];
        reported->plugin = plugin->name;
        snprintf(reported->id, sizeof reported->id, "%s", event->id == NULL ? "" : event->id);
    }
    return WEIVEL_C_STATUS_OK;
}

static int build_plugin(consume_run *run, const weivel_json_value *given) {
    static const char *const keys[] = {"id", "capabilities"};
    static const char *const capability_keys[] = {"provides", "consumes"};
    const weivel_json_value *capabilities = weivel_json_object_get(given, "capabilities");
    world_plugin *plugin;
    if (!conformance_world_knows_keys(run->world, given, keys, 2, "a plugin") ||
        !conformance_world_knows_keys(run->world, capabilities, capability_keys, 2, "a plugin's capabilities")) {
        return 0;
    }
    plugin = conformance_world_add(run->world, weivel_json_as_string(weivel_json_object_get(given, "id")),
                                   open_and_wait);
    if (plugin == NULL || plugin->name == NULL) {
        return conformance_world_fail(run->world, "a plugin has no string id", NULL);
    }
    plugin->more = run;
    return conformance_world_strings(run->world, weivel_json_object_get(capabilities, "provides"),
                                     plugin->capabilities_provided, &plugin->capabilities_provided_count) &&
           conformance_world_strings(run->world, weivel_json_object_get(capabilities, "consumes"),
                                     plugin->capabilities_consumed, &plugin->capabilities_consumed_count) &&
           conformance_world_register(run->world, plugin);
}

static weivel_c_status ignore_outcome(void *data, const weivel_c_outcome *outcome) {
    (void)data;
    (void)outcome;
    return WEIVEL_C_STATUS_OK;
}

static int open_every_plugin(consume_run *run) {
    conformance_world *world = run->world;
    weivel_handle handles[CONFORMANCE_PLUGINS];
    weivel_c_activation activation = WEIVEL_INIT(weivel_c_activation);
    weivel_activation_plan plan = WEIVEL_INIT(weivel_activation_plan);
    uint32_t index;
    for (index = 0; index < world->plugin_count; index++) {
        handles[index] = world->plugins[index].handle;
    }
    activation.plugins = handles;
    activation.count = world->plugin_count;
    activation.edge_source = WEIVEL_EDGES_CAPABILITIES;
    activation.listener.hear = ignore_outcome;
    if (weivel_c_host_activate(world->host, &activation, &plan, NULL) != WEIVEL_C_STATUS_OK) {
        return conformance_world_fail(world, "the host did not open the vector's plugins", NULL);
    }
    weivel_c_host_plan_release(world->host, &plan);
    return 1;
}

static int listen_to_every_plugin(consume_run *run) {
    uint32_t index;
    for (index = 0; index < run->world->plugin_count; index++) {
        world_plugin *plugin = &run->world->plugins[index];
        weivel_c_plugin_listener listener = {hear_reach, plugin};
        uint64_t registration = 0;
        if (plugin->context == NULL ||
            weivel_c_context_add_listener(plugin->context, &listener, &registration) != WEIVEL_C_STATUS_OK) {
            return conformance_world_fail(run->world, "the host did not hand a plugin its context", plugin->name);
        }
    }
    return 1;
}

static weivel_c_status hear_capability(void *data, const weivel_c_capability_change *change) {
    consume_watch *watch = data;
    const world_plugin *provider = change->implementation;
    if (watch->count < CONSUME_HEARD) {
        watch->heard[watch->count].provider = provider == NULL ? "?" : provider->name;
        watch->heard[watch->count].change = change->change;
        watch->count++;
    }
    return watch->throws ? WEIVEL_C_STATUS_FAILED : WEIVEL_C_STATUS_OK;
}

static void keep_refusal(consume_run *run, weivel_c_status status, weivel_c_refusal *refusal) {
    if (status == WEIVEL_C_STATUS_REFUSED && !run->refused) {
        run->first_refusal = *refusal;
        run->refused = 1;
        return;
    }
    weivel_c_refusal_release(refusal);
}

static int watch_step(consume_run *run, world_plugin *plugin, const weivel_json_value *step) {
    weivel_c_refusal refusal = WEIVEL_INIT(weivel_c_refusal);
    consume_watch *watch;
    weivel_c_capability_watcher watcher;
    int throws = 0;
    weivel_c_status status;
    if (run->watch_count == CONSUME_WATCHES) {
        return conformance_world_fail(run->world, "the vector makes more watches than the driver holds", NULL);
    }
    watch = &run->watches[run->watch_count++];
    weivel_json_as_bool(weivel_json_object_get(step, "throws"), &throws);
    watch->throws = throws;
    watcher.hear = hear_capability;
    watcher.data = watch;
    status = weivel_c_context_watch_capability(
        plugin->context, weivel_json_as_string(weivel_json_object_get(step, "watch")), &watcher, NULL, &refusal);
    keep_refusal(run, status, &refusal);
    return 1;
}

static int provide_step(consume_run *run, world_plugin *plugin, const weivel_json_value *step) {
    weivel_c_refusal refusal = WEIVEL_INIT(weivel_c_refusal);
    weivel_c_status status = weivel_c_context_provide_capability(
        plugin->context, weivel_json_as_string(weivel_json_object_get(step, "provide")), plugin, NULL, &refusal);
    keep_refusal(run, status, &refusal);
    return 1;
}

static int host_step(consume_run *run, world_plugin *plugin, const char *action) {
    uint32_t finished = 0;
    if (strcmp(action, "verify") == 0) {
        weivel_c_host_verify(run->world->host, plugin->handle, NULL);
    } else {
        weivel_c_host_release(run->world->host, plugin->handle, &finished);
    }
    return 1;
}

static const char *action_of(consume_run *run, const weivel_json_value *step) {
    static const char *const actions[] = {"watch", "provide", "verify", "release"};
    static const char *const watch_keys[] = {"plugin", "watch", "throws"};
    const char *named = NULL;
    uint32_t index;
    for (index = 0; index < 4; index++) {
        if (weivel_json_object_get(step, actions[index]) != NULL) {
            named = named == NULL ? actions[index] : "";
        }
    }
    if (named == NULL || named[0] == '\0') {
        conformance_world_fail(run->world, "a step names other than exactly one of watch, provide, verify, release",
                               NULL);
        return NULL;
    }
    if (strcmp(named, "watch") == 0) {
        return conformance_world_knows_keys(run->world, step, watch_keys, 3, "a step") ? named : NULL;
    }
    return conformance_world_knows_keys(run->world, step, (const char *const[]){"plugin", named}, 2, "a step")
               ? named
               : NULL;
}

static int run_step(consume_run *run, const weivel_json_value *step) {
    const char *action = action_of(run, step);
    world_plugin *plugin;
    if (action == NULL) {
        return 0;
    }
    plugin = conformance_world_plugin(run->world, weivel_json_as_string(weivel_json_object_get(step, "plugin")));
    if (plugin == NULL) {
        return 0;
    }
    if (strcmp(action, "watch") == 0) {
        return watch_step(run, plugin, step);
    }
    if (strcmp(action, "provide") == 0) {
        return provide_step(run, plugin, step);
    }
    return host_step(run, plugin, action);
}

static void observe_heard(void *data, const weivel_json_value *expectation, conformance_text *out) {
    consume_run *run = data;
    uint32_t watch;
    uint32_t index;
    (void)expectation;
    conformance_text_append(out, "[");
    for (watch = 0; watch < run->watch_count; watch++) {
        conformance_text_append(out, watch == 0 ? "[" : ",[");
        for (index = 0; index < run->watches[watch].count; index++) {
            const heard_change *heard = &run->watches[watch].heard[index];
            conformance_text_append(out, index == 0 ? "[" : ",[");
            conformance_text_append_quoted(out, heard->provider);
            conformance_text_append(out, heard->change == WEIVEL_CAPABILITY_PROVIDED ? ",\"provide\"]"
                                         : heard->change == WEIVEL_CAPABILITY_WITHDRAWN ? ",\"withdraw\"]"
                                                                                         : ",\"unknown\"]");
        }
        conformance_text_append(out, "]");
    }
    conformance_text_append(out, "]");
}

static void observe_reported(void *data, const weivel_json_value *expectation, conformance_text *out) {
    consume_run *run = data;
    uint32_t index;
    (void)expectation;
    conformance_text_append(out, "[");
    for (index = 0; index < run->reported_count; index++) {
        conformance_text_append(out, index == 0 ? "[" : ",[");
        conformance_text_append_quoted(out, run->reported[index].plugin);
        conformance_text_append(out, ",");
        conformance_text_append_quoted(out, run->reported[index].id);
        conformance_text_append(out, "]");
    }
    conformance_text_append(out, "]");
}

static void observe_refusal(void *data, const weivel_json_value *expectation, conformance_text *out) {
    consume_run *run = data;
    (void)expectation;
    if (!run->refused) {
        conformance_text_append(out, "null");
        return;
    }
    conformance_write_refusal(out, run->first_refusal.code, run->first_refusal.subject, run->first_refusal.has_fix);
}

/** Calls `visit` with each plugin's ledger row whose status is WEIVEL_PLUGIN_STATUS_QUARANTINED, in ledger order. */
static void each_quarantined_row(consume_run *run, void (*visit)(void *data, const weivel_ledger_row *row),
                                 void *data) {
    weivel_c_host *host = run->world->host;
    uint32_t index;
    for (index = 0; index < run->world->plugin_count; index++) {
        weivel_ledger_row row = WEIVEL_INIT(weivel_ledger_row);
        if (weivel_c_host_ledger_row(host, run->world->plugins[index].handle, &row, NULL) != WEIVEL_C_STATUS_OK) {
            continue;
        }
        if (row.status == WEIVEL_PLUGIN_STATUS_QUARANTINED) {
            visit(data, &row);
        }
        weivel_c_host_ledger_row_release(host, &row);
    }
}

static void write_quarantined_name(void *data, const weivel_ledger_row *row) {
    conformance_text *out = data;
    conformance_text_append(out, out->characters[out->length - 1] == '[' ? "" : ",");
    conformance_text_append_quoted(out, row->import_name == NULL ? "" : row->import_name);
}

static void observe_quarantined(void *data, const weivel_json_value *expectation, conformance_text *out) {
    (void)expectation;
    conformance_text_append(out, "[");
    each_quarantined_row(data, write_quarantined_name, out);
    conformance_text_append(out, "]");
}

typedef struct first_reason {
    conformance_text *out;
    int written;
} first_reason;

static void write_first_reason(void *data, const weivel_ledger_row *row) {
    first_reason *first = data;
    if (!first->written) {
        conformance_write_refusal(first->out, row->refusal.code, row->subject, row->has_fix);
        first->written = 1;
    }
}

static void observe_quarantine_reason(void *data, const weivel_json_value *expectation, conformance_text *out) {
    first_reason first = {out, 0};
    (void)expectation;
    each_quarantined_row(data, write_first_reason, &first);
    if (!first.written) {
        conformance_text_append(out, "null");
    }
}

static const conformance_field CONSUME_FIELDS[] = {{"heard", observe_heard},
                                                   {"reported", observe_reported},
                                                   {"refusal", observe_refusal},
                                                   {"quarantined", observe_quarantined},
                                                   {"quarantineReason", observe_quarantine_reason}};

static int build_every_plugin(consume_run *run, const weivel_json_value *plugins) {
    size_t index;
    for (index = 0; index < weivel_json_array_length(plugins); index++) {
        if (!build_plugin(run, weivel_json_array_at(plugins, index))) {
            return 0;
        }
    }
    return open_every_plugin(run) && listen_to_every_plugin(run);
}

int conformance_drive_consume(conformance_world *world, const conformance_vector *vector, conformance_text *observed) {
    static const char *const keys[] = {"plugins", "steps"};
    const weivel_json_value *steps = weivel_json_object_get(vector->given, "steps");
    consume_run run;
    size_t index;
    int driven;
    memset(&run, 0, sizeof run);
    run.world = world;
    run.first_refusal = (weivel_c_refusal)WEIVEL_INIT(weivel_c_refusal);
    driven = conformance_world_knows_keys(world, vector->given, keys, 2, "the given") &&
             build_every_plugin(&run, weivel_json_object_get(vector->given, "plugins"));
    for (index = 0; driven && index < weivel_json_array_length(steps); index++) {
        driven = run_step(&run, weivel_json_array_at(steps, index));
    }
    driven = driven && conformance_observe(world, vector->then, CONSUME_FIELDS,
                                           sizeof CONSUME_FIELDS / sizeof CONSUME_FIELDS[0], &run, observed);
    conformance_world_end(world);
    weivel_c_refusal_release(&run.first_refusal);
    return driven;
}
