/**
 * @file
 * The `lifecycle` op's steps, each made through the host's public surface, and what the driver reads after the last.
 *
 * A C host answers a status where another language's would throw, so a step "throws to the suite" when its call
 * answers a status that is no answer of the host's to a well-made call: anything but OK, REFUSED, NONE and DESTROYED.
 * A verify is `quiet` when it answers one of those and nothing reached the host's log or error sink during it.
 */

#include <stdio.h>
#include <string.h>

#include "lifecycle.h"
#include "observe.h"
#include "ops.h"

static weivel_c_status ignore_outcome(void *data, const weivel_c_outcome *outcome) {
    (void)data;
    (void)outcome;
    return WEIVEL_C_STATUS_OK;
}

static int is_a_thrown_status(weivel_c_status status) {
    return status != WEIVEL_C_STATUS_OK && status != WEIVEL_C_STATUS_REFUSED && status != WEIVEL_C_STATUS_NONE &&
           status != WEIVEL_C_STATUS_DESTROYED;
}

static uint32_t loud_so_far(const conformance_world *world) {
    return world->error_lines + world->sink_failures;
}

static const char *name_of(lifecycle_run *run, weivel_handle handle) {
    uint32_t index;
    for (index = 0; index < run->world->plugin_count; index++) {
        if (run->world->plugins[index].handle == handle) {
            return run->world->plugins[index].name;
        }
    }
    return "?";
}

static void keep_plan(lifecycle_run *run, const weivel_activation_plan *plan) {
    uint32_t index;
    if (run->plan_count == LIFECYCLE_STEPS) {
        return;
    }
    for (index = 0; index < plan->order_count && index < CONFORMANCE_PLUGINS; index++) {
        run->plans[run->plan_count][index] = name_of(run, plan->order[index]);
    }
    run->plan_lengths[run->plan_count++] = index;
}

static uint32_t edge_source_of(const char *name) {
    if (name != NULL && strcmp(name, "capabilities") == 0) {
        return WEIVEL_EDGES_CAPABILITIES;
    }
    return name != NULL && strcmp(name, "services") == 0 ? WEIVEL_EDGES_SERVICES : 0;
}

static int activate_step(lifecycle_run *run, const weivel_json_value *names, uint32_t edge_source) {
    weivel_handle handles[CONFORMANCE_PLUGINS];
    weivel_c_activation activation = WEIVEL_INIT(weivel_c_activation);
    weivel_activation_plan plan = WEIVEL_INIT(weivel_activation_plan);
    size_t index;
    for (index = 0; index < weivel_json_array_length(names) && index < CONFORMANCE_PLUGINS; index++) {
        world_plugin *plugin =
            conformance_world_plugin(run->world, weivel_json_as_string(weivel_json_array_at(names, index)));
        if (plugin == NULL) {
            return 0;
        }
        handles[index] = plugin->handle;
    }
    activation.plugins = handles;
    activation.count = (uint32_t)index;
    activation.edge_source = edge_source;
    activation.listener.hear = ignore_outcome;
    if (weivel_c_host_activate(run->world->host, &activation, &plan, NULL) != WEIVEL_C_STATUS_OK) {
        return conformance_world_fail(run->world, "the host did not activate the step's plugins", NULL);
    }
    keep_plan(run, &plan);
    weivel_c_host_plan_release(run->world->host, &plan);
    return 1;
}

static void destroy_step(lifecycle_run *run) {
    uint32_t finished = 0;
    run->destroying = 1;
    run->loud_before_destroy = loud_so_far(run->world);
    run->thrown += weivel_c_host_destroy(run->world->host, &finished) != WEIVEL_C_STATUS_OK;
}

static void verify_step(lifecycle_run *run, world_plugin *plugin) {
    uint32_t written = run->world->log_lines + run->world->sink_failures;
    weivel_c_status status = weivel_c_host_verify(run->world->host, plugin->handle, NULL);
    int quiet = !is_a_thrown_status(status) && run->world->log_lines + run->world->sink_failures == written;
    run->thrown += is_a_thrown_status(status) && run->destroying;
    if (run->verify_count < LIFECYCLE_STEPS) {
        run->verifies[run->verify_count++] = quiet ? "quiet" : "loud";
    }
}

static int plugin_get_step(lifecycle_run *run, const weivel_json_value *step) {
    world_plugin *plugin =
        conformance_world_plugin(run->world, weivel_json_as_string(weivel_json_object_get(step, "plugin")));
    if (plugin == NULL || plugin->context == NULL) {
        return plugin == NULL ? 0 : conformance_world_fail(run->world, "a step names a plugin never activated", NULL);
    }
    lifecycle_get(run, plugin->name, plugin->context, weivel_json_as_string(weivel_json_object_get(step, "get")));
    return 1;
}

static world_plugin *named_plugin(lifecycle_run *run, const weivel_json_value *step, const char *key) {
    return conformance_world_plugin(run->world, weivel_json_as_string(weivel_json_object_get(step, key)));
}

static int run_step(lifecycle_run *run, const weivel_json_value *step, uint32_t edge_source) {
    world_plugin *plugin;
    uint32_t finished = 0;
    if (weivel_json_object_get(step, "activate") != NULL) {
        return activate_step(run, weivel_json_object_get(step, "activate"), edge_source);
    }
    if (weivel_json_object_get(step, "destroy") != NULL) {
        destroy_step(run);
        return 1;
    }
    if (weivel_json_object_get(step, "plugin") != NULL) {
        return plugin_get_step(run, step);
    }
    plugin = named_plugin(run, step, weivel_json_object_get(step, "release") != NULL ? "release" : "verify");
    if (plugin == NULL) {
        return 0;
    }
    if (weivel_json_object_get(step, "release") != NULL) {
        run->thrown += is_a_thrown_status(weivel_c_host_release(run->world->host, plugin->handle, &finished));
    } else {
        verify_step(run, plugin);
    }
    return 1;
}

static int check_step(lifecycle_run *run, const weivel_json_value *step) {
    static const char *const kinds[] = {"activate", "release", "destroy", "verify", "get"};
    static const char *const get_keys[] = {"plugin", "get"};
    uint32_t named = 0;
    uint32_t index;
    const char *kind = NULL;
    for (index = 0; index < 5; index++) {
        if (weivel_json_object_get(step, kinds[index]) != NULL) {
            named++;
            kind = kinds[index];
        }
    }
    if (named != 1) {
        return conformance_world_fail(run->world, "a step names other than exactly one kind of step", NULL);
    }
    if (strcmp(kind, "get") == 0) {
        return conformance_world_knows_keys(run->world, step, get_keys, 2, "a step");
    }
    return conformance_world_knows_keys(run->world, step, &kind, 1, "a step");
}

static void observe_plans(void *data, const weivel_json_value *expectation, conformance_text *out) {
    lifecycle_run *run = data;
    uint32_t index;
    (void)expectation;
    conformance_text_append(out, "[");
    for (index = 0; index < run->plan_count; index++) {
        conformance_text_append(out, index == 0 ? "" : ",");
        conformance_write_names(out, run->plans[index], run->plan_lengths[index], 0);
    }
    conformance_text_append(out, "]");
}

static int place_of(lifecycle_run *run, const char *line) {
    uint32_t index;
    for (index = 0; line != NULL && index < run->trace_count; index++) {
        if (strcmp(run->trace[index], line) == 0) {
            return (int)index;
        }
    }
    return -1;
}

static void write_trace(lifecycle_run *run, conformance_text *out) {
    uint32_t index;
    conformance_text_append(out, "not in that order in [");
    for (index = 0; index < run->trace_count; index++) {
        conformance_text_append(out, index == 0 ? "" : ", ");
        conformance_text_append(out, run->trace[index]);
    }
    conformance_text_append(out, "]");
}

static void observe_pair(lifecycle_run *run, const weivel_json_value *pair, conformance_text *out) {
    const char *earlier = weivel_json_as_string(weivel_json_array_at(pair, 0));
    const char *later = weivel_json_as_string(weivel_json_array_at(pair, 1));
    int earlier_at = place_of(run, earlier);
    int later_at = place_of(run, later);
    conformance_text_append(out, "[");
    conformance_text_append_quoted(out, earlier == NULL ? "" : earlier);
    conformance_text_append(out, ",");
    conformance_text_append_quoted(out, later == NULL ? "" : later);
    if (earlier_at < 0 || later_at < 0 || earlier_at >= later_at) {
        conformance_text text;
        conformance_text_start(&text);
        write_trace(run, &text);
        conformance_text_append(out, ",");
        conformance_text_append_quoted(out, text.failed ? "" : text.characters);
        conformance_text_end(&text);
    }
    conformance_text_append(out, "]");
}

/** The `before` pairs as asked, each one the trace breaks marked so, so the answer equals `then` only when all hold. */
static void observe_trace(void *data, const weivel_json_value *expectation, conformance_text *out) {
    const weivel_json_value *pairs = weivel_json_object_get(expectation, "before");
    size_t index;
    if (pairs == NULL) {
        conformance_text_append(out, "\"a lifecycle vector's trace is judged with before, and this one names none\"");
        return;
    }
    conformance_text_append(out, "{\"before\":[");
    for (index = 0; index < weivel_json_array_length(pairs); index++) {
        conformance_text_append(out, index == 0 ? "" : ",");
        observe_pair(data, weivel_json_array_at(pairs, index), out);
    }
    conformance_text_append(out, "]}");
}

static void observe_active(void *data, const weivel_json_value *expectation, conformance_text *out) {
    lifecycle_run *run = data;
    const char *names[CONFORMANCE_PLUGINS];
    uint32_t count = 0;
    weivel_c_core *core = NULL;
    uint32_t index;
    (void)expectation;
    for (index = 0; weivel_c_host_core(run->world->host, &core) == WEIVEL_C_STATUS_OK &&
                    index < run->world->plugin_count;
         index++) {
        weivel_ledger_row row = WEIVEL_INIT(weivel_ledger_row);
        if (weivel_c_core_ledger_entry(core, run->world->plugins[index].handle, &row, NULL) == WEIVEL_C_STATUS_OK) {
            if (row.status == WEIVEL_PLUGIN_STATUS_ACTIVE) {
                names[count++] = run->world->plugins[index].name;
            }
            weivel_c_core_ledger_row_release(core, &row);
        }
    }
    conformance_write_names(out, names, count, 1);
}

static void observe_forgotten(void *data, const weivel_json_value *expectation, conformance_text *out) {
    lifecycle_run *run = data;
    const char *names[CONFORMANCE_PLUGINS];
    uint32_t count = 0;
    uint32_t index;
    (void)expectation;
    for (index = 0; index < run->world->plugin_count; index++) {
        uint32_t state = 0;
        if (weivel_c_host_plugin_state(run->world->host, run->world->plugins[index].handle, &state) ==
            WEIVEL_C_STATUS_NONE) {
            names[count++] = run->world->plugins[index].name;
        }
    }
    conformance_write_names(out, names, count, 1);
}

static void observe_pending(void *data, const weivel_json_value *expectation, conformance_text *out) {
    lifecycle_run *run = data;
    char lines[LIFECYCLE_WANTS][LIFECYCLE_TEXT];
    const char *names[LIFECYCLE_WANTS];
    uint32_t count = 0;
    uint32_t index;
    int written;
    (void)expectation;
    for (index = 0; index < run->want_count; index++) {
        if (!run->wants[index].ended) {
            written = snprintf(lines[count], LIFECYCLE_TEXT, "%s %s", run->wants[index].owner->plugin->name,
                               run->wants[index].id);
            if (written < 0 || written >= LIFECYCLE_TEXT) {
                conformance_world_fail(run->world, "a pending want is named longer than the driver holds",
                                       run->wants[index].id);
                continue;
            }
            names[count] = lines[count];
            count++;
        }
    }
    conformance_write_names(out, names, count, 1);
}

static void observe_verifies(void *data, const weivel_json_value *expectation, conformance_text *out) {
    lifecycle_run *run = data;
    (void)expectation;
    conformance_write_names(out, run->verifies, run->verify_count, 0);
}

static void observe_loud(void *data, const weivel_json_value *expectation, conformance_text *out) {
    lifecycle_run *run = data;
    uint32_t loud = run->thrown + (run->destroying ? loud_so_far(run->world) - run->loud_before_destroy : 0);
    (void)expectation;
    conformance_text_append_integer(out, loud);
}

static const conformance_field LIFECYCLE_FIELDS[] = {
    {"plans", observe_plans},       {"trace", observe_trace},       {"active", observe_active},
    {"forgotten", observe_forgotten}, {"pending", observe_pending}, {"verifies", observe_verifies},
    {"loud", observe_loud}};

static int build_and_check(lifecycle_run *run, const weivel_json_value *given) {
    static const char *const keys[] = {"edgeSource", "plugins", "steps"};
    const weivel_json_value *plugins = weivel_json_object_get(given, "plugins");
    const weivel_json_value *steps = weivel_json_object_get(given, "steps");
    size_t index;
    if (!conformance_world_knows_keys(run->world, given, keys, 3, "the given") ||
        edge_source_of(weivel_json_as_string(weivel_json_object_get(given, "edgeSource"))) == 0) {
        return conformance_world_fail(run->world, "the vector names an edge source this driver does not know", NULL);
    }
    for (index = 0; index < weivel_json_array_length(plugins); index++) {
        if (!lifecycle_build_plugin(run, weivel_json_array_at(plugins, index))) {
            return 0;
        }
    }
    for (index = 0; index < weivel_json_array_length(steps); index++) {
        if (!check_step(run, weivel_json_array_at(steps, index))) {
            return 0;
        }
    }
    return 1;
}

int conformance_drive_lifecycle(conformance_world *world, const conformance_vector *vector,
                                conformance_text *observed) {
    lifecycle_run run;
    const weivel_json_value *steps = weivel_json_object_get(vector->given, "steps");
    uint32_t edge_source = edge_source_of(weivel_json_as_string(weivel_json_object_get(vector->given, "edgeSource")));
    size_t index;
    int driven;
    memset(&run, 0, sizeof run);
    run.world = world;
    driven = build_and_check(&run, vector->given);
    for (index = 0; driven && index < weivel_json_array_length(steps); index++) {
        driven = run_step(&run, weivel_json_array_at(steps, index), edge_source) && !world->has_problem;
    }
    driven = driven && !world->has_problem &&
             conformance_observe(world, vector->then, LIFECYCLE_FIELDS,
                                 sizeof LIFECYCLE_FIELDS / sizeof LIFECYCLE_FIELDS[0], &run, observed);
    conformance_world_end(world);
    return driven;
}
