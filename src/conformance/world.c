#include "world.h"

#include <stdio.h>
#include <string.h>

#include <weivel/c/standard.h>

static void keep_report(conformance_world *world, const char *text) {
    if (world->reported_count < CONFORMANCE_REPORTED) {
        snprintf(world->reported[world->reported_count++], CONFORMANCE_REPORT_TEXT, "%s", text == NULL ? "" : text);
    }
}

static weivel_c_status count_failure(void *data, const weivel_c_failure *failure) {
    conformance_world *world = data;
    keep_report(world, failure->text);
    world->sink_failures++;
    world->listener_failures += failure->kind == WEIVEL_C_FAILURE_LISTENER;
    return WEIVEL_C_STATUS_OK;
}

static weivel_c_status count_line(void *data, const weivel_c_log_record *record) {
    conformance_world *world = data;
    keep_report(world, record->text);
    world->log_lines++;
    world->error_lines += record->level == WEIVEL_LOG_ERROR;
    return WEIVEL_C_STATUS_OK;
}

int conformance_world_fail(conformance_world *world, const char *what, const char *about) {
    if (!world->has_problem) {
        snprintf(world->problem, CONFORMANCE_PROBLEM, "%s%s%s", what, about == NULL ? "" : ": ",
                 about == NULL ? "" : about);
        world->has_problem = 1;
    }
    return 0;
}

int conformance_world_begin(conformance_world *world, const char *core_path, const weivel_core_options *core_options,
                            char *problem) {
    weivel_c_host_options options = WEIVEL_INIT(weivel_c_host_options);
    weivel_c_scheduler scheduler;
    weivel_c_status status;
    memset(world, 0, sizeof *world);
    world->problem = problem;
    scheduler = conformance_virtual_scheduler(world);
    options.core_path = core_path;
    options.core_options = core_options;
    options.scheduler = &scheduler;
    options.error_sink.report = count_failure;
    options.error_sink.data = world;
    options.log.write = count_line;
    options.log.data = world;
    options.timer_observer.moved = conformance_world_hear_timer_move;
    options.timer_observer.data = world;
    options.description_check.check = weivel_c_standard_check_description;
    status = weivel_c_host_create(&options, &world->host, NULL);
    if (status != WEIVEL_C_STATUS_OK) {
        return conformance_world_fail(world, "the host could not be made on the core", core_path);
    }
    return 1;
}

void conformance_world_end(conformance_world *world) {
    uint32_t finished = 0;
    if (world->host == NULL) {
        return;
    }
    world->sink_failures_of_the_run = world->sink_failures;
    world->listener_failures_of_the_run = world->listener_failures;
    world->error_lines_of_the_run = world->error_lines;
    weivel_c_host_destroy(world->host, &finished);
    if (finished == 1 && weivel_c_host_free(world->host) == WEIVEL_C_STATUS_OK) {
        world->host = NULL;
        return;
    }
    conformance_world_fail(world, "the host did not finish its destroy, so it was left unfreed", NULL);
}

world_plugin *conformance_world_add(conformance_world *world, const char *name, weivel_c_entry_function activate) {
    world_plugin *plugin;
    if (world->plugin_count == CONFORMANCE_PLUGINS) {
        conformance_world_fail(world, "the vector gives more plugins than the driver holds", name);
        return NULL;
    }
    plugin = &world->plugins[world->plugin_count++];
    memset(plugin, 0, sizeof *plugin);
    plugin->world = world;
    plugin->name = name;
    plugin->manifest = (weivel_manifest)WEIVEL_INIT(weivel_manifest);
    plugin->manifest.import_name = name;
    plugin->manifest.api = 1;
    plugin->entry = (weivel_c_entry)WEIVEL_INIT(weivel_c_entry);
    plugin->entry.activate = activate;
    plugin->entry.data = plugin;
    return plugin;
}

int conformance_world_strings(conformance_world *world, const weivel_json_value *array, const char **into,
                              uint32_t *count) {
    size_t index;
    if (weivel_json_kind_of(array) != WEIVEL_JSON_ARRAY || weivel_json_array_length(array) > CONFORMANCE_DECLARED) {
        return conformance_world_fail(world, "a list of ids is no array of at most eight", NULL);
    }
    for (index = 0; index < weivel_json_array_length(array); index++) {
        into[index] = weivel_json_as_string(weivel_json_array_at(array, index));
        if (into[index] == NULL) {
            return conformance_world_fail(world, "a list of ids holds something that is no string", NULL);
        }
    }
    *count = (uint32_t)weivel_json_array_length(array);
    return 1;
}

static void lay_out(world_plugin *plugin) {
    weivel_manifest *manifest = &plugin->manifest;
    manifest->services.provides.items = plugin->offer_count == 0 ? NULL : plugin->offers;
    manifest->services.provides.count = plugin->offer_count;
    manifest->services.consumes.items = plugin->want_count == 0 ? NULL : plugin->wants;
    manifest->services.consumes.count = plugin->want_count;
    manifest->capabilities.provides.items =
        plugin->capabilities_provided_count == 0 ? NULL : plugin->capabilities_provided;
    manifest->capabilities.provides.count = plugin->capabilities_provided_count;
    manifest->capabilities.consumes.items =
        plugin->capabilities_consumed_count == 0 ? NULL : plugin->capabilities_consumed;
    manifest->capabilities.consumes.count = plugin->capabilities_consumed_count;
}

int conformance_world_register(conformance_world *world, world_plugin *plugin) {
    lay_out(plugin);
    if (weivel_c_host_register(world->host, &plugin->manifest, &plugin->entry, &plugin->handle, NULL) !=
        WEIVEL_C_STATUS_OK) {
        return conformance_world_fail(world, "the host did not register the plugin", plugin->name);
    }
    return 1;
}

world_plugin *conformance_world_plugin(conformance_world *world, const char *name) {
    uint32_t index;
    for (index = 0; name != NULL && index < world->plugin_count; index++) {
        if (strcmp(world->plugins[index].name, name) == 0) {
            return &world->plugins[index];
        }
    }
    conformance_world_fail(world, "the vector names a plugin it does not give", name);
    return NULL;
}

static int is_known(const char *key, const char *const *known, uint32_t count) {
    uint32_t index;
    for (index = 0; index < count; index++) {
        if (strcmp(key, known[index]) == 0) {
            return 1;
        }
    }
    return 0;
}

int conformance_world_knows_keys(conformance_world *world, const weivel_json_value *value, const char *const *known,
                                 uint32_t count, const char *where) {
    size_t index;
    if (weivel_json_kind_of(value) != WEIVEL_JSON_OBJECT) {
        return conformance_world_fail(world, "the driver expected an object", where);
    }
    for (index = 0; index < weivel_json_object_size(value); index++) {
        if (!is_known(weivel_json_object_key_at(value, index), known, count)) {
            conformance_world_fail(world, "the vector carries a key this driver does not drive",
                                   weivel_json_object_key_at(value, index));
            return 0;
        }
    }
    return 1;
}
