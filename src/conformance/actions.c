/**
 * @file
 * The entries a `lifecycle` vector's plugins are built with. Each records `activate <plugin>` or
 * `deactivate <plugin>` the moment the host calls it, then runs its actions in order: `{ want: id }` waits for the
 * want to end before the next action, so the entry answers pending and its actions go on from the want's callback,
 * finishing the completion once the last has run; `{ want: id, await: false }` starts it and goes on. `{ watch: id }`
 * installs a watch and goes on, `{ provide: id }` provides a capability with the plugin itself as its implementation,
 * and `{ all: id }` lists a capability's implementations by the provider each names.
 */

#include <stdio.h>
#include <string.h>

#include "lifecycle.h"
#include "observe.h"

void lifecycle_record(lifecycle_run *run, const char *first, const char *second, const char *third) {
    if (run->trace_count == LIFECYCLE_LINES) {
        conformance_world_fail(run->world, "the run records more lines than the driver holds", first);
        return;
    }
    int written = snprintf(run->trace[run->trace_count], LIFECYCLE_TEXT, "%s %s%s%s", first, second,
                           third[0] == '\0' ? "" : " ", third);
    if (written < 0 || written >= LIFECYCLE_TEXT) {
        conformance_world_fail(run->world, "the run records a line longer than the driver holds", first);
        return;
    }
    run->trace_count++;
}

void lifecycle_get(lifecycle_run *run, const char *name, weivel_c_context *context, const char *id) {
    void *service = NULL;
    weivel_c_status status = weivel_c_context_get_service(context, id, &service, NULL);
    lifecycle_record(run, status == WEIVEL_C_STATUS_OK ? "got" : "missed", name, id);
}

static uint32_t run_actions(action_sequence *sequence);

static void go_on(action_sequence *sequence) {
    weivel_c_completion *completion;
    if (run_actions(sequence) != WEIVEL_C_ENTRY_DONE || sequence->completion == NULL) {
        return;
    }
    completion = sequence->completion;
    sequence->completion = NULL;
    weivel_c_completion_finish(completion, WEIVEL_C_ENTRY_DONE, NULL);
}

static weivel_c_status want_ended(void *data, const weivel_c_settlement *settlement) {
    lifecycle_want *want = data;
    lifecycle_run *run = want->owner->run;
    const char *name = want->owner->plugin->name;
    char ending[LIFECYCLE_TEXT];
    const char *code = conformance_code_name(settlement->refusal.code);
    int written;
    want->ended = 1;
    if (settlement->service != NULL) {
        lifecycle_record(run, "settled", name, want->id);
    } else {
        written = snprintf(ending, sizeof ending, "%s %s", want->id, code == NULL ? "unknown" : code);
        if (written < 0 || (size_t)written >= sizeof ending) {
            conformance_world_fail(run->world, "the run records a line longer than the driver holds", name);
        } else {
            lifecycle_record(run, "rejected", name, ending);
        }
    }
    if (want->awaiting != NULL && !want->awaiting->inside_the_entry) {
        go_on(want->awaiting);
    }
    return WEIVEL_C_STATUS_OK;
}

/** Starts a want; answers it, or NULL with a problem written when it was not made. */
static lifecycle_want *start_want(lifecycle_plugin *owner, const char *id, action_sequence *awaiting) {
    lifecycle_run *run = owner->run;
    lifecycle_want *want;
    weivel_c_want_callback callback;
    if (run->want_count == LIFECYCLE_WANTS) {
        conformance_world_fail(run->world, "the run starts more wants than the driver holds", id);
        return NULL;
    }
    want = &run->wants[run->want_count++];
    want->owner = owner;
    want->id = id;
    want->awaiting = awaiting;
    want->ended = 0;
    callback.settled = want_ended;
    callback.data = want;
    if (weivel_c_context_want_service(owner->plugin->context, id, NULL, &callback, NULL, NULL) != WEIVEL_C_STATUS_OK) {
        want->ended = 1;
        conformance_world_fail(run->world, "a plugin's want was not made", id);
        return NULL;
    }
    return want;
}

static int is_awaited(const weivel_json_value *action) {
    int awaited = 1;
    weivel_json_as_bool(weivel_json_object_get(action, "await"), &awaited);
    return awaited;
}

static weivel_c_status hear_registered(void *data, const weivel_c_service_change *change) {
    lifecycle_plugin *owner = data;
    if (change->change == WEIVEL_CHANGE_REGISTERED) {
        lifecycle_record(owner->run, "heard", owner->plugin->name, change->id);
    }
    return WEIVEL_C_STATUS_OK;
}

static void watch(lifecycle_plugin *owner, const char *id) {
    weivel_c_service_watcher watcher = {hear_registered, NULL};
    watcher.data = owner;
    if (weivel_c_context_watch_service(owner->plugin->context, id, &watcher, NULL, NULL) != WEIVEL_C_STATUS_OK) {
        conformance_world_fail(owner->run->world, "a plugin's watch was not made", id);
    }
}

static void provide(lifecycle_plugin *owner, const char *id) {
    world_plugin *plugin = owner->plugin;
    if (weivel_c_context_provide_capability(plugin->context, id, plugin, NULL, NULL) == WEIVEL_C_STATUS_OK) {
        lifecycle_record(owner->run, "provided", plugin->name, id);
    }
}

/** Writes `id` and the provider each implementation names, each after one space, into `listed`. */
static int name_the_providers(char *listed, size_t room, const char *id, const weivel_c_implementations *found) {
    size_t used;
    uint32_t index;
    int written = snprintf(listed, room, "%s", id);
    if (written < 0 || (size_t)written >= room) {
        return 0;
    }
    used = (size_t)written;
    for (index = 0; index < found->count; index++) {
        const world_plugin *provider = found->items[index];
        written = snprintf(listed + used, room - used, " %s", provider->name);
        if (written < 0 || (size_t)written >= room - used) {
            return 0;
        }
        used += (size_t)written;
    }
    return 1;
}

static void list_all(lifecycle_plugin *owner, const char *id) {
    weivel_c_implementations found = WEIVEL_INIT(weivel_c_implementations);
    char listed[LIFECYCLE_TEXT];
    if (weivel_c_context_all_capabilities(owner->plugin->context, id, &found, NULL) != WEIVEL_C_STATUS_OK) {
        conformance_world_fail(owner->run->world, "a plugin's listing was not answered", id);
        return;
    }
    if (name_the_providers(listed, sizeof listed, id, &found)) {
        lifecycle_record(owner->run, "listed", owner->plugin->name, listed);
    } else {
        conformance_world_fail(owner->run->world, "the run records a line longer than the driver holds", id);
    }
    weivel_c_implementations_release(&found);
}

/** Runs an action that never waits: a watch, a provision or a listing, else a get. */
static void run_at_once(lifecycle_plugin *owner, const weivel_json_value *action) {
    const char *watched = weivel_json_as_string(weivel_json_object_get(action, "watch"));
    const char *provided = weivel_json_as_string(weivel_json_object_get(action, "provide"));
    const char *listed = weivel_json_as_string(weivel_json_object_get(action, "all"));
    if (watched != NULL) {
        watch(owner, watched);
    } else if (provided != NULL) {
        provide(owner, provided);
    } else if (listed != NULL) {
        list_all(owner, listed);
    } else {
        lifecycle_get(owner->run, owner->plugin->name, owner->plugin->context,
                      weivel_json_as_string(weivel_json_object_get(action, "get")));
    }
}

/** Runs one action; answers 0 when it is a want the sequence waits for, which has not ended yet. */
static int run_action(action_sequence *sequence, const weivel_json_value *action) {
    lifecycle_plugin *owner = sequence->owner;
    world_plugin *plugin = owner->plugin;
    const char *registered = weivel_json_as_string(weivel_json_object_get(action, "register"));
    const char *wanted = weivel_json_as_string(weivel_json_object_get(action, "want"));
    if (registered != NULL) {
        if (weivel_c_context_register_service(plugin->context, registered, plugin, NULL, NULL, NULL) ==
            WEIVEL_C_STATUS_OK) {
            lifecycle_record(owner->run, "registered", plugin->name, registered);
        }
        return 1;
    }
    if (wanted != NULL) {
        int awaited = is_awaited(action);
        lifecycle_want *want = start_want(owner, wanted, awaited ? sequence : NULL);
        return want == NULL || !awaited || want->ended;
    }
    run_at_once(owner, action);
    return 1;
}

static uint32_t run_actions(action_sequence *sequence) {
    while (sequence->next < weivel_json_array_length(sequence->actions)) {
        const weivel_json_value *action = weivel_json_array_at(sequence->actions, sequence->next++);
        if (!run_action(sequence, action)) {
            return WEIVEL_C_ENTRY_PENDING;
        }
    }
    return WEIVEL_C_ENTRY_DONE;
}

static uint32_t enter(action_sequence *sequence, const char *label, weivel_c_completion *completion) {
    uint32_t answer;
    lifecycle_record(sequence->owner->run, label, sequence->owner->plugin->name, "");
    sequence->next = 0;
    sequence->completion = completion;
    sequence->inside_the_entry = 1;
    answer = run_actions(sequence);
    sequence->inside_the_entry = 0;
    if (answer == WEIVEL_C_ENTRY_DONE) {
        sequence->completion = NULL;
    }
    return answer;
}

static uint32_t lifecycle_activate(void *data, weivel_c_context *context, weivel_c_completion *completion,
                                   const char **reason) {
    lifecycle_plugin *self = data;
    (void)reason;
    self->plugin->context = context;
    return enter(&self->activation, "activate", completion);
}

static uint32_t lifecycle_deactivate(void *data, weivel_c_context *context, weivel_c_completion *completion,
                                     const char **reason) {
    lifecycle_plugin *self = data;
    (void)context;
    (void)reason;
    return enter(&self->deactivation, "deactivate", completion);
}

static int check_actions(conformance_world *world, const weivel_json_value *actions) {
    size_t index;
    if (weivel_json_kind_of(actions) != WEIVEL_JSON_ARRAY) {
        return conformance_world_fail(world, "a plugin's actions are no array", NULL);
    }
    for (index = 0; index < weivel_json_array_length(actions); index++) {
        const weivel_json_value *action = weivel_json_array_at(actions, index);
        static const char *const keys[] = {"register", "want", "await", "get", "watch", "provide", "all"};
        static const char *const kinds[] = {"register", "want", "get", "watch", "provide", "all"};
        int named = 0;
        size_t kind;
        for (kind = 0; kind < sizeof kinds / sizeof kinds[0]; kind++) {
            named += weivel_json_as_string(weivel_json_object_get(action, kinds[kind])) != NULL;
        }
        if (named != 1 || !conformance_world_knows_keys(world, action, keys, 7, "an action")) {
            return conformance_world_fail(
                world, "an action names other than exactly one of register, want, get, watch, provide, all", NULL);
        }
    }
    return 1;
}

static void offer_and_want(world_plugin *plugin, const char **provides, uint32_t provided, const char **consumes,
                           uint32_t consumed) {
    uint32_t index;
    for (index = 0; index < provided; index++) {
        plugin->offers[index] = (weivel_service_offer)WEIVEL_INIT(weivel_service_offer);
        plugin->offers[index].id = provides[index];
    }
    for (index = 0; index < consumed; index++) {
        plugin->wants[index] = (weivel_service_want)WEIVEL_INIT(weivel_service_want);
        plugin->wants[index].id = consumes[index];
    }
    plugin->offer_count = provided;
    plugin->want_count = consumed;
}

/** Reads a plugin's optional `{ provides, consumes }` of capabilities into its manifest's lists. */
static int declare_capabilities(lifecycle_run *run, world_plugin *plugin, const weivel_json_value *given) {
    static const char *const keys[] = {"provides", "consumes"};
    const weivel_json_value *capabilities = weivel_json_object_get(given, "capabilities");
    if (capabilities == NULL) {
        return 1;
    }
    return conformance_world_knows_keys(run->world, capabilities, keys, 2, "a plugin's capabilities") &&
           conformance_world_strings(run->world, weivel_json_object_get(capabilities, "provides"),
                                     plugin->capabilities_provided, &plugin->capabilities_provided_count) &&
           conformance_world_strings(run->world, weivel_json_object_get(capabilities, "consumes"),
                                     plugin->capabilities_consumed, &plugin->capabilities_consumed_count);
}

static int declare(lifecycle_run *run, world_plugin *plugin, const weivel_json_value *given) {
    const char *provides[CONFORMANCE_DECLARED];
    const char *consumes[CONFORMANCE_DECLARED];
    uint32_t provided = 0;
    uint32_t consumed = 0;
    if (!conformance_world_strings(run->world, weivel_json_object_get(given, "provides"), provides, &provided) ||
        !conformance_world_strings(run->world, weivel_json_object_get(given, "consumes"), consumes, &consumed)) {
        return 0;
    }
    offer_and_want(plugin, provides, provided, consumes, consumed);
    return declare_capabilities(run, plugin, given);
}

static void start_sequences(lifecycle_plugin *self, const weivel_json_value *given) {
    self->activation.owner = self;
    self->activation.actions = weivel_json_object_get(given, "activate");
    self->deactivation.owner = self;
    self->deactivation.actions = weivel_json_object_get(given, "deactivate");
    self->plugin->entry.data = self;
    self->plugin->entry.deactivate = self->deactivation.actions == NULL ? NULL : lifecycle_deactivate;
}

int lifecycle_build_plugin(lifecycle_run *run, const weivel_json_value *given) {
    static const char *const keys[] = {"id", "provides", "consumes", "capabilities", "activate", "deactivate"};
    const char *name = weivel_json_as_string(weivel_json_object_get(given, "id"));
    lifecycle_plugin *self;
    world_plugin *plugin;
    if (!conformance_world_knows_keys(run->world, given, keys, 6, "a plugin") || name == NULL) {
        return conformance_world_fail(run->world, "a plugin has no string id", NULL);
    }
    plugin = conformance_world_add(run->world, name, lifecycle_activate);
    if (plugin == NULL) {
        return 0;
    }
    self = &run->plugins[run->world->plugin_count - 1];
    self->run = run;
    self->plugin = plugin;
    self->given = given;
    plugin->more = self;
    start_sequences(self, given);
    return declare(run, plugin, given) && check_actions(run->world, self->activation.actions) &&
           (self->deactivation.actions == NULL || check_actions(run->world, self->deactivation.actions)) &&
           conformance_world_register(run->world, plugin);
}
