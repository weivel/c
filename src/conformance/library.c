/**
 * @file
 * The `library` op: the vector's declaration read by the standard's reader and loaded through the host's public load
 * call with the core's echo library beside it; holders opened through the host's activation with an activate that
 * stays pending, each consuming every declared id; then the vector's steps, as "The `library` op in a layer" of the
 * kit's docs/vectors/layer/library.md states. Each step runs only once the layer has nothing left to run from the
 * one before, which in C is so of every call made where no dispatch runs, as each step's is.
 *
 * A `description` vector is driven here too, on the standard's described library: C binds nothing of a description,
 * so its load, its gets and what they serve are all the driver witnesses of it, and a step that calls an operation
 * through a binding is one this driver does not drive.
 *
 * In C a holder lets go of a revoked holding with weivel_c_context_release_holding, handed the `handle` its
 * revocation named, and of a kept bytes answer with the object's `release_bytes`.
 *
 * A C host's release forgets the plugin in the same call, once the events the core's release queued have been heard,
 * so a released plugin's late call can be made only while it hears them. The `get` steps a holder makes straight after
 * its `releaseHolder` are therefore made from its listener, as it hears the revocation its release queued, and the
 * `forgetHolder` that follows finds the plugin forgotten already.
 */

#include <stdio.h>
#include <string.h>

#include <weivel/c/host/library.h>
#include <weivel/library.h>

#include "../core/binary.h"
#include "echo.h"
#include "observe.h"
#include "ops.h"

#define LIBRARY_STEPS 32
#define LIBRARY_HOLDINGS 32
#define LIBRARY_TEXT 64

/** What one holder resolved of one declared id, and what it holds of it. */
typedef struct library_holding {
    world_plugin *holder;
    const char *id;
    void *object;
    /** The handle the revocation of what it resolved named, 0 until one is heard. */
    weivel_handle revoked;
    weivel_c_library_answer kept;
} library_holding;

/** A pair of names a run answers: a holder and an id it was revoked, or an owner and an id whose holdings ended. */
typedef struct revocation {
    const char *holder;
    const char *id;
} revocation;

/** A registration an owner made awaiting releases, so its release event can be told by the id it was made under. */
typedef struct owner_registration {
    world_plugin *owner;
    const char *id;
    weivel_handle registration;
} owner_registration;

typedef struct library_run {
    conformance_world *world;
    weivel_standard_library declaration;
    weivel_c_library *library;
    weivel_c_status load_status;
    int load_made;
    weivel_c_completion *completions[CONFORMANCE_PLUGINS];
    library_holding holdings[LIBRARY_HOLDINGS];
    uint32_t holding_count;
    int got[LIBRARY_STEPS];
    uint32_t got_count;
    char echoed[LIBRARY_STEPS][LIBRARY_TEXT];
    uint32_t echoed_count;
    revocation revoked[LIBRARY_HOLDINGS];
    uint32_t revoked_count;
    uint64_t unloads[LIBRARY_STEPS];
    uint32_t unloads_count;
    int same_object;
    /** Every id the owners provide, the vector's own texts. */
    const char *provided[CONFORMANCE_DECLARED];
    uint32_t provided_count;
    owner_registration registrations[LIBRARY_HOLDINGS];
    uint32_t registration_count;
    revocation released[LIBRARY_HOLDINGS];
    uint32_t released_count;
    const weivel_json_value *steps;
    /** The step the run goes on with after the one running. */
    size_t next_step;
    /** While a holder is being released: that holder, and the steps of its late gets, [late_from, late_to). */
    world_plugin *releasing;
    size_t late_from;
    size_t late_to;
    int late_made;
} library_run;

/**
 * The counter the echo binary adds one to when it is closed. It lives for the whole process, since a binary the
 * layer never closes is closed only at the process's exit and writes to it then.
 */
static uint64_t unload_counter;

static library_run *run_of(world_plugin *plugin) {
    return plugin->more;
}

static uint32_t index_of(const world_plugin *plugin) {
    return (uint32_t)(plugin - plugin->world->plugins);
}

static uint32_t open_and_wait(void *data, weivel_c_context *context, weivel_c_completion *completion,
                              const char **reason) {
    world_plugin *plugin = data;
    (void)reason;
    plugin->context = context;
    run_of(plugin)->completions[index_of(plugin)] = completion;
    return WEIVEL_C_ENTRY_PENDING;
}

/**
 * The declared or provided id equal to `id`, the declaration's or the vector's own text, so a kept pointer outlives the
 * event it came from.
 */
static const char *declared_id(library_run *run, const char *id) {
    size_t at;
    for (at = 0; id != NULL && at < run->declaration.service_count; at++) {
        if (strcmp(run->declaration.services[at].id, id) == 0) {
            return run->declaration.services[at].id;
        }
    }
    for (at = 0; id != NULL && at < run->provided_count; at++) {
        if (strcmp(run->provided[at], id) == 0) {
            return run->provided[at];
        }
    }
    return NULL;
}

static const weivel_standard_library_service *declared_service(library_run *run, const char *id) {
    size_t at;
    for (at = 0; id != NULL && at < run->declaration.service_count; at++) {
        if (strcmp(run->declaration.services[at].id, id) == 0) {
            return &run->declaration.services[at];
        }
    }
    return NULL;
}

static int is_a_definition(const weivel_standard_library_service *service) {
    return service != NULL && service->functions[0].kind == WEIVEL_STANDARD_FUNCTION_DEFINITION;
}

static library_holding *holding_of(library_run *run, world_plugin *holder, const char *id) {
    uint32_t at;
    for (at = 0; at < run->holding_count; at++) {
        if (run->holdings[at].holder == holder && strcmp(run->holdings[at].id, id) == 0) {
            return &run->holdings[at];
        }
    }
    return NULL;
}

/** The holding of `holder` for `id`, made if it has none; NULL with a problem past the limit or for an id unlisted. */
static library_holding *holding_for(library_run *run, world_plugin *holder, const char *id) {
    library_holding *holding;
    const char *kept = declared_id(run, id);
    if (kept == NULL) {
        conformance_world_fail(run->world, "a step names an id the declaration does not list", id);
        return NULL;
    }
    holding = holding_of(run, holder, kept);
    if (holding != NULL) {
        return holding;
    }
    if (run->holding_count == LIBRARY_HOLDINGS) {
        conformance_world_fail(run->world, "the run holds more than the driver keeps", id);
        return NULL;
    }
    holding = &run->holdings[run->holding_count++];
    memset(holding, 0, sizeof *holding);
    holding->holder = holder;
    holding->id = kept;
    holding->kept = (weivel_c_library_answer)WEIVEL_INIT(weivel_c_library_answer);
    holding->kept.body = (weivel_c_bytes)WEIVEL_INIT(weivel_c_bytes);
    return holding;
}

static int get_step(library_run *run, world_plugin *holder, const char *id);

static const char *string_at(const weivel_json_value *step, const char *key) {
    return weivel_json_as_string(weivel_json_object_get(step, key));
}

/** Makes a released holder's late gets, once, from the first revocation its own release queued. */
static void make_the_late_gets(library_run *run, world_plugin *holder) {
    size_t at;
    if (run->releasing != holder || run->late_made) {
        return;
    }
    run->late_made = 1;
    for (at = run->late_from; at < run->late_to; at++) {
        get_step(run, holder, string_at(weivel_json_array_at(run->steps, at), "get"));
    }
}

static void hear_released(library_run *run, world_plugin *owner, weivel_handle registration) {
    uint32_t at;
    for (at = 0; at < run->registration_count; at++) {
        if (run->registrations[at].owner == owner && run->registrations[at].registration == registration &&
            run->released_count < LIBRARY_HOLDINGS) {
            run->released[run->released_count].holder = owner->name;
            run->released[run->released_count++].id = run->registrations[at].id;
        }
    }
}

static weivel_c_status hear_plugin_event(void *data, const weivel_c_plugin_event *event) {
    world_plugin *plugin = data;
    library_run *run = run_of(plugin);
    const char *id = declared_id(run, event->id);
    library_holding *holding = id == NULL ? NULL : holding_of(run, plugin, id);
    if (event->kind == WEIVEL_C_PLUGIN_EVENT_HOLDINGS_RELEASED) {
        hear_released(run, plugin, event->handle);
        return WEIVEL_C_STATUS_OK;
    }
    if (event->kind != WEIVEL_C_PLUGIN_EVENT_REVOKED || id == NULL) {
        return WEIVEL_C_STATUS_OK;
    }
    if (run->revoked_count < LIBRARY_HOLDINGS) {
        run->revoked[run->revoked_count].holder = plugin->name;
        run->revoked[run->revoked_count++].id = id;
    }
    if (holding != NULL) {
        holding->revoked = event->handle;
    }
    make_the_late_gets(run, plugin);
    return WEIVEL_C_STATUS_OK;
}

static int read_declaration(library_run *run, const weivel_json_value *declared) {
    conformance_text text;
    weivel_status read;
    run->declaration = (weivel_standard_library)WEIVEL_INIT(weivel_standard_library);
    conformance_text_start(&text);
    conformance_text_append_json(&text, declared);
    read = text.failed ? WEIVEL_STATUS_OUT_OF_MEMORY
                       : weivel_standard_read_library(text.characters, text.length, &run->declaration, NULL);
    conformance_text_end(&text);
    if (read != WEIVEL_STATUS_OK) {
        return conformance_world_fail(run->world, "the standard's reader did not read the vector's declaration", NULL);
    }
    return 1;
}

static void wants(world_plugin *plugin, const char *id) {
    plugin->wants[plugin->want_count] = (weivel_service_want)WEIVEL_INIT(weivel_service_want);
    plugin->wants[plugin->want_count++].id = id;
}

static int add_holder(library_run *run, const char *name) {
    world_plugin *plugin = conformance_world_add(run->world, name, open_and_wait);
    size_t at;
    if (plugin == NULL || name == NULL) {
        return conformance_world_fail(run->world, "a holder is no string", NULL);
    }
    if (run->declaration.service_count + run->provided_count > CONFORMANCE_DECLARED) {
        return conformance_world_fail(run->world, "the vector lists more ids than a holder consumes", NULL);
    }
    plugin->more = run;
    for (at = 0; at < run->declaration.service_count; at++) {
        wants(plugin, run->declaration.services[at].id);
    }
    for (at = 0; at < run->provided_count; at++) {
        wants(plugin, run->provided[at]);
    }
    return conformance_world_register(run->world, plugin);
}

static int add_owner(library_run *run, const weivel_json_value *given) {
    static const char *const keys[] = {"id", "provides"};
    world_plugin *plugin;
    uint32_t at;
    if (!conformance_world_knows_keys(run->world, given, keys, 2, "an owner")) {
        return 0;
    }
    plugin = conformance_world_add(run->world, string_at(given, "id"), open_and_wait);
    if (plugin == NULL || plugin->name == NULL) {
        return conformance_world_fail(run->world, "an owner has no string id", NULL);
    }
    plugin->more = run;
    if (!conformance_world_strings(run->world, weivel_json_object_get(given, "provides"),
                                   run->provided + run->provided_count, &at) ||
        run->provided_count + at > CONFORMANCE_DECLARED) {
        return conformance_world_fail(run->world, "the owners provide more ids than the driver holds", NULL);
    }
    for (plugin->offer_count = 0; plugin->offer_count < at; plugin->offer_count++) {
        plugin->offers[plugin->offer_count] = (weivel_service_offer)WEIVEL_INIT(weivel_service_offer);
        plugin->offers[plugin->offer_count].id = run->provided[run->provided_count + plugin->offer_count];
    }
    run->provided_count += at;
    return conformance_world_register(run->world, plugin);
}

static weivel_c_status ignore_outcome(void *data, const weivel_c_outcome *outcome) {
    (void)data;
    (void)outcome;
    return WEIVEL_C_STATUS_OK;
}

static int open_the_holders(library_run *run) {
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
    if (world->plugin_count > 0 &&
        weivel_c_host_activate(world->host, &activation, &plan, NULL) != WEIVEL_C_STATUS_OK) {
        return conformance_world_fail(world, "the host did not open the vector's holders", NULL);
    }
    weivel_c_host_plan_release(world->host, &plan);
    for (index = 0; index < world->plugin_count; index++) {
        world_plugin *plugin = &world->plugins[index];
        weivel_c_plugin_listener listener = {hear_plugin_event, plugin};
        uint64_t registration = 0;
        if (plugin->context == NULL ||
            weivel_c_context_add_listener(plugin->context, &listener, &registration) != WEIVEL_C_STATUS_OK) {
            return conformance_world_fail(world, "the host did not hand a holder its context", plugin->name);
        }
    }
    return 1;
}

/**
 * Whether `object` is the very address the definition's symbol answers, read by opening the binary again while the
 * layer holds it open, so the same image answers, and closing it at once, so no handle of the driver's outlives the
 * step.
 */
static int is_the_symbols_own_answer(library_run *run, const weivel_standard_library_service *service,
                                     const void *object) {
    void *binary = NULL;
    weivel_c_binary_function found;
    int same;
    if (weivel_c_binary_open(run->world->library_path, &binary) != WEIVEL_C_STATUS_OK) {
        return conformance_world_fail(run->world, "the driver could not open the library to read its definition",
                                      run->world->library_path);
    }
    found = weivel_c_binary_find(binary, service->functions[0].symbol);
    same = found != NULL && ((const void *(*)(void))found)() == object;
    weivel_c_binary_close(binary);
    return same;
}

static int get_step(library_run *run, world_plugin *holder, const char *id) {
    library_holding *holding = holding_for(run, holder, id);
    const weivel_standard_library_service *service = declared_service(run, id);
    void *object = NULL;
    int served;
    if (holding == NULL || run->got_count == LIBRARY_STEPS) {
        return holding == NULL ? 0 : conformance_world_fail(run->world, "the run gets more than the driver keeps", id);
    }
    served = weivel_c_context_get_service(holder->context, id, &object, NULL) == WEIVEL_C_STATUS_OK;
    run->got[run->got_count++] = served;
    if (served) {
        holding->object = object;
        holding->revoked = 0;
        if (is_a_definition(service) && !is_the_symbols_own_answer(run, service, object)) {
            run->same_object = 0;
        }
    }
    return 1;
}

/** The definition `holder` resolved, the first of its holdings that is one; NULL with a problem when it has none. */
static const echo_definition *definition_of(library_run *run, world_plugin *holder) {
    uint32_t at;
    for (at = 0; at < run->holding_count; at++) {
        library_holding *holding = &run->holdings[at];
        if (holding->holder == holder && holding->object != NULL &&
            is_a_definition(declared_service(run, holding->id))) {
            return holding->object;
        }
    }
    conformance_world_fail(run->world, "a step calls a definition its holder never resolved", holder->name);
    return NULL;
}

static int echo_step(library_run *run, world_plugin *holder, const char *text) {
    const echo_definition *definition = definition_of(run, holder);
    uint8_t *output = NULL;
    size_t length = 0;
    int32_t status;
    if (definition == NULL || text == NULL || run->echoed_count == LIBRARY_STEPS) {
        return definition == NULL ? 0
                                  : conformance_world_fail(run->world,
                                                           "an echo step is not as the kit's "
                                                           "docs/vectors/layer/library.md has it",
                                                           NULL);
    }
    status = definition->echo(definition->context, (const uint8_t *)text, strlen(text), &output, &length);
    if (status == 0 && length < LIBRARY_TEXT) {
        memcpy(run->echoed[run->echoed_count], output, length);
        run->echoed[run->echoed_count][length] = '\0';
    } else {
        snprintf(run->echoed[run->echoed_count], LIBRARY_TEXT, "echo answered status %d", (int)status);
    }
    run->echoed_count++;
    definition->release(definition->context, output);
    return 1;
}

static int count_unloads_step(library_run *run, world_plugin *holder) {
    const echo_definition *definition = definition_of(run, holder);
    if (definition == NULL) {
        return 0;
    }
    definition->count_unloads(definition->context, &unload_counter);
    return 1;
}

static uint32_t function_index(const weivel_c_library_service *service, const char *name) {
    uint32_t at;
    for (at = 0; name != NULL && at < service->function_count; at++) {
        if (strcmp(service->function_names[at], name) == 0) {
            return at;
        }
    }
    return UINT32_MAX;
}

static int call_step(library_run *run, world_plugin *holder, const weivel_json_value *step) {
    const char *id = weivel_json_as_string(weivel_json_object_get(step, "call"));
    const char *text = weivel_json_as_string(weivel_json_object_get(step, "text"));
    library_holding *holding = id == NULL ? NULL : holding_of(run, holder, id);
    const weivel_c_library_service *service;
    uint32_t function;
    int keep = 0;
    if (holding == NULL || holding->object == NULL || text == NULL) {
        return conformance_world_fail(run->world, "a call step names a service its holder never resolved", id);
    }
    service = holding->object;
    function = function_index(service, weivel_json_as_string(weivel_json_object_get(step, "function")));
    weivel_json_as_bool(weivel_json_object_get(step, "keep"), &keep);
    if (function == UINT32_MAX || holding->kept.body.owner != NULL) {
        return conformance_world_fail(run->world, "a call step names no function of its service, or keeps twice", id);
    }
    service->call(service, function, (const uint8_t *)text, strlen(text), &holding->kept);
    if (!keep) {
        service->release_bytes(&holding->kept.body);
    }
    return 1;
}

static int drop_step(library_run *run, world_plugin *holder, const char *id) {
    library_holding *holding = id == NULL ? NULL : holding_of(run, holder, id);
    const weivel_c_library_service *service;
    if (holding == NULL || holding->object == NULL) {
        return conformance_world_fail(run->world, "a drop step names a service its holder never resolved", id);
    }
    service = holding->object;
    service->release_bytes(&holding->kept.body);
    return 1;
}

static int release_holding_step(library_run *run, world_plugin *holder, const char *id) {
    library_holding *holding = id == NULL ? NULL : holding_of(run, holder, id);
    if (holding == NULL || holding->revoked == 0) {
        return conformance_world_fail(run->world, "a holder lets go of a holding it never heard revoked", id);
    }
    weivel_c_context_release_holding(holder->context, holding->revoked, NULL);
    holding->revoked = 0;
    return 1;
}

static int quarantine_step(library_run *run, world_plugin *holder) {
    weivel_c_completion *completion = run->completions[index_of(holder)];
    if (completion == NULL) {
        return conformance_world_fail(run->world, "a quarantine names a holder whose activate is not pending", NULL);
    }
    run->completions[index_of(holder)] = NULL;
    weivel_c_completion_finish(completion, WEIVEL_C_ENTRY_FAILED, "the vector quarantines this holder");
    return 1;
}

static int read_unloads_step(library_run *run) {
    if (run->unloads_count == LIBRARY_STEPS) {
        return conformance_world_fail(run->world, "the run reads the counter more often than the driver keeps", NULL);
    }
    run->unloads[run->unloads_count++] = unload_counter;
    return 1;
}

static const char *plugin_named_by(const weivel_json_value *step) {
    static const char *const keys[] = {"releaseHolder", "quarantine", "forgetHolder"};
    uint32_t at;
    for (at = 0; at < 3; at++) {
        if (weivel_json_object_get(step, keys[at]) != NULL) {
            return string_at(step, keys[at]);
        }
    }
    return NULL;
}

/** The end of the run of `get` steps `holder` makes straight after the step at `at`. */
static size_t late_gets_end(library_run *run, const world_plugin *holder, size_t at) {
    size_t end = at + 1;
    while (end < weivel_json_array_length(run->steps)) {
        const weivel_json_value *step = weivel_json_array_at(run->steps, end);
        const char *named = string_at(step, "holder");
        if (named == NULL || strcmp(named, holder->name) != 0 || weivel_json_object_get(step, "get") == NULL) {
            break;
        }
        end++;
    }
    return end;
}

static int release_step(library_run *run, world_plugin *plugin) {
    uint32_t finished = 0;
    size_t at = run->next_step - 1;
    run->releasing = plugin;
    run->late_from = at + 1;
    run->late_to = late_gets_end(run, plugin, at);
    run->late_made = 0;
    weivel_c_host_release(run->world->host, plugin->handle, &finished);
    run->releasing = NULL;
    if (run->late_to == run->late_from) {
        return 1;
    }
    if (!run->late_made) {
        return conformance_world_fail(run->world, "a released holder heard nothing of its release to call from",
                                      plugin->name);
    }
    run->next_step = run->late_to;
    return 1;
}

/** In C the release forgot the plugin already, so the step holds the host to having done so. */
static int forget_step(library_run *run, world_plugin *plugin) {
    uint32_t state = 0;
    if (weivel_c_host_plugin_state(run->world->host, plugin->handle, &state) != WEIVEL_C_STATUS_NONE) {
        return conformance_world_fail(run->world, "the host has not forgotten a holder it released", plugin->name);
    }
    return 1;
}

static int register_awaiting_step(library_run *run, const weivel_json_value *step) {
    world_plugin *owner = conformance_world_plugin(run->world, string_at(step, "owner"));
    const char *id = declared_id(run, string_at(step, "registerAwaitingReleases"));
    owner_registration *made;
    if (owner == NULL || owner->context == NULL || id == NULL || run->registration_count == LIBRARY_HOLDINGS) {
        return owner == NULL ? 0
                             : conformance_world_fail(run->world,
                                                      "a registration step is not as the kit's "
                                                      "docs/vectors/layer/library.md has it",
                                                      id);
    }
    made = &run->registrations[run->registration_count++];
    made->owner = owner;
    made->id = id;
    if (weivel_c_context_register_service_awaiting_releases(owner->context, id, owner, NULL, &made->registration,
                                                            NULL) != WEIVEL_C_STATUS_OK) {
        return conformance_world_fail(run->world, "an owner's registration awaiting releases was refused", id);
    }
    return 1;
}

static int host_step(library_run *run, const weivel_json_value *step) {
    world_plugin *plugin;
    if (weivel_json_object_get(step, "load") != NULL) {
        if (run->declaration.import_name == NULL) {
            return conformance_world_fail(run->world, "a load step comes with no declaration", NULL);
        }
        run->load_made = 1;
        run->load_status = weivel_c_host_load_library(run->world->host, run->world->library_path, &run->declaration,
                                                      &run->library, NULL, NULL);
        return 1;
    }
    if (weivel_json_object_get(step, "unload") != NULL) {
        if (run->library == NULL) {
            return conformance_world_fail(run->world, "an unload step comes with no library loaded", NULL);
        }
        weivel_c_host_unload_library(run->world->host, run->library);
        return 1;
    }
    if (weivel_json_object_get(step, "readUnloads") != NULL) {
        return read_unloads_step(run);
    }
    plugin = conformance_world_plugin(run->world, plugin_named_by(step));
    if (plugin == NULL) {
        return 0;
    }
    if (weivel_json_object_get(step, "quarantine") != NULL) {
        return quarantine_step(run, plugin);
    }
    if (weivel_json_object_get(step, "forgetHolder") != NULL) {
        return forget_step(run, plugin);
    }
    return release_step(run, plugin);
}

static int holder_step(library_run *run, const weivel_json_value *step) {
    world_plugin *holder = conformance_world_plugin(run->world, string_at(step, "holder"));
    if (holder == NULL || holder->context == NULL) {
        return holder == NULL ? 0 : conformance_world_fail(run->world, "a step names a holder never opened", NULL);
    }
    if (weivel_json_object_get(step, "get") != NULL) {
        return get_step(run, holder, string_at(step, "get"));
    }
    if (weivel_json_object_get(step, "echo") != NULL) {
        return echo_step(run, holder, string_at(step, "echo"));
    }
    if (weivel_json_object_get(step, "call") != NULL) {
        return call_step(run, holder, step);
    }
    if (weivel_json_object_get(step, "drop") != NULL) {
        return drop_step(run, holder, string_at(step, "drop"));
    }
    if (weivel_json_object_get(step, "countUnloads") != NULL) {
        return count_unloads_step(run, holder);
    }
    return release_holding_step(run, holder, string_at(step, "releaseHolding"));
}

/** Refuses a step whose keys are not exactly those of one step the kit's docs/vectors/layer/library.md names. */
static int check_step(library_run *run, const weivel_json_value *step) {
    static const char *const alone[] = {"load",          "unload",     "readUnloads",
                                        "releaseHolder", "quarantine", "forgetHolder"};
    static const char *const owner_keys[] = {"owner", "registerAwaitingReleases"};
    static const char *const held[] = {"get", "echo", "drop", "countUnloads", "releaseHolding"};
    static const char *const call_keys[] = {"holder", "call", "function", "text", "keep"};
    const char *keys[2] = {"holder", NULL};
    uint32_t at;
    if (weivel_json_object_get(step, "owner") != NULL) {
        return conformance_world_knows_keys(run->world, step, owner_keys, 2, "a step") &&
               (weivel_json_object_size(step) == 2 ||
                conformance_world_fail(run->world,
                                       "an owner's step is not as the kit's docs/vectors/layer/library.md has it",
                                       NULL));
    }
    for (at = 0; at < 6; at++) {
        if (weivel_json_object_get(step, alone[at]) != NULL) {
            return conformance_world_knows_keys(run->world, step, &alone[at], 1, "a step");
        }
    }
    if (weivel_json_object_get(step, "call") != NULL) {
        return conformance_world_knows_keys(run->world, step, call_keys, 5, "a step");
    }
    for (at = 0; at < 5; at++) {
        if (weivel_json_object_get(step, held[at]) != NULL) {
            keys[1] = held[at];
            return conformance_world_knows_keys(run->world, step, keys, 2, "a step") &&
                   (weivel_json_object_size(step) == 2 ||
                    conformance_world_fail(run->world, "a step names other than exactly one kind of step", NULL));
        }
    }
    return conformance_world_fail(run->world, "a step names no kind of step this driver drives", NULL);
}

static int build_and_check(library_run *run, const weivel_json_value *given) {
    static const char *const keys[] = {"declaration", "owners", "holders", "steps"};
    const weivel_json_value *declaration = weivel_json_object_get(given, "declaration");
    const weivel_json_value *owners = weivel_json_object_get(given, "owners");
    const weivel_json_value *holders = weivel_json_object_get(given, "holders");
    const weivel_json_value *steps = weivel_json_object_get(given, "steps");
    size_t at;
    run->declaration = (weivel_standard_library)WEIVEL_INIT(weivel_standard_library);
    if (run->world->library_path == NULL) {
        return conformance_world_fail(run->world, "the vector needs the path of the library its op loads", NULL);
    }
    if (!conformance_world_knows_keys(run->world, given, keys, 4, "the given") ||
        (declaration != NULL && !read_declaration(run, declaration))) {
        return 0;
    }
    for (at = 0; at < weivel_json_array_length(owners); at++) {
        if (!add_owner(run, weivel_json_array_at(owners, at))) {
            return 0;
        }
    }
    for (at = 0; at < weivel_json_array_length(holders); at++) {
        if (!add_holder(run, weivel_json_as_string(weivel_json_array_at(holders, at)))) {
            return 0;
        }
    }
    for (at = 0; at < weivel_json_array_length(steps); at++) {
        if (!check_step(run, weivel_json_array_at(steps, at))) {
            return 0;
        }
    }
    return open_the_holders(run);
}

static int run_step(library_run *run, const weivel_json_value *step) {
    if (weivel_json_object_get(step, "owner") != NULL) {
        return register_awaiting_step(run, step);
    }
    if (weivel_json_object_get(step, "holder") != NULL) {
        return holder_step(run, step);
    }
    return host_step(run, step);
}

static void observe_loaded(void *data, const weivel_json_value *expectation, conformance_text *out) {
    library_run *run = data;
    (void)expectation;
    conformance_text_append(out, run->load_made && run->load_status == WEIVEL_C_STATUS_OK ? "true" : "false");
}

static void observe_callers_mistake(void *data, const weivel_json_value *expectation, conformance_text *out) {
    library_run *run = data;
    (void)expectation;
    conformance_text_append(out, run->load_made && run->load_status == WEIVEL_C_STATUS_MISUSE ? "true" : "false");
}

static void write_booleans(conformance_text *out, const int *values, uint32_t count) {
    uint32_t at;
    conformance_text_append(out, "[");
    for (at = 0; at < count; at++) {
        conformance_text_append(out, at == 0 ? "" : ",");
        conformance_text_append(out, values[at] ? "true" : "false");
    }
    conformance_text_append(out, "]");
}

static void observe_got(void *data, const weivel_json_value *expectation, conformance_text *out) {
    library_run *run = data;
    (void)expectation;
    write_booleans(out, run->got, run->got_count);
}

static void observe_same_object(void *data, const weivel_json_value *expectation, conformance_text *out) {
    library_run *run = data;
    (void)expectation;
    conformance_text_append(out, run->same_object ? "true" : "false");
}

static void observe_echoed(void *data, const weivel_json_value *expectation, conformance_text *out) {
    library_run *run = data;
    const char *texts[LIBRARY_STEPS];
    uint32_t at;
    (void)expectation;
    for (at = 0; at < run->echoed_count; at++) {
        texts[at] = run->echoed[at];
    }
    conformance_write_names(out, texts, run->echoed_count, 0);
}

static int revocation_before(const revocation *left, const revocation *right) {
    int by_holder = strcmp(left->holder, right->holder);
    return by_holder < 0 || (by_holder == 0 && strcmp(left->id, right->id) < 0);
}

static void observe_revoked(void *data, const weivel_json_value *expectation, conformance_text *out) {
    library_run *run = data;
    revocation sorted[LIBRARY_HOLDINGS];
    uint32_t at;
    uint32_t back;
    (void)expectation;
    for (at = 0; at < run->revoked_count; at++) {
        revocation next = run->revoked[at];
        for (back = at; back > 0 && revocation_before(&next, &sorted[back - 1]); back--) {
            sorted[back] = sorted[back - 1];
        }
        sorted[back] = next;
    }
    conformance_text_append(out, "[");
    for (at = 0; at < run->revoked_count; at++) {
        const char *pair[2];
        pair[0] = sorted[at].holder;
        pair[1] = sorted[at].id;
        conformance_text_append(out, at == 0 ? "" : ",");
        conformance_write_names(out, pair, 2, 0);
    }
    conformance_text_append(out, "]");
}

static void observe_holdings_released(void *data, const weivel_json_value *expectation, conformance_text *out) {
    library_run *run = data;
    uint32_t at;
    (void)expectation;
    conformance_text_append(out, "[");
    for (at = 0; at < run->released_count; at++) {
        const char *pair[2];
        pair[0] = run->released[at].holder;
        pair[1] = run->released[at].id;
        conformance_text_append(out, at == 0 ? "" : ",");
        conformance_write_names(out, pair, 2, 0);
    }
    conformance_text_append(out, "]");
}

static void observe_unloads(void *data, const weivel_json_value *expectation, conformance_text *out) {
    library_run *run = data;
    uint32_t at;
    (void)expectation;
    conformance_text_append(out, "[");
    for (at = 0; at < run->unloads_count; at++) {
        conformance_text_append(out, at == 0 ? "" : ",");
        conformance_text_append_integer(out, (int64_t)run->unloads[at]);
    }
    conformance_text_append(out, "]");
}

static void observe_reported(void *data, const weivel_json_value *expectation, conformance_text *out) {
    library_run *run = data;
    const char *texts[CONFORMANCE_REPORTED];
    uint32_t at;
    (void)expectation;
    for (at = 0; at < run->world->reported_count; at++) {
        texts[at] = run->world->reported[at];
    }
    conformance_write_names(out, texts, run->world->reported_count, 0);
}

static const conformance_field LIBRARY_FIELDS[] = {
    {"loaded", observe_loaded}, {"callersMistake", observe_callers_mistake}, {"got", observe_got},
    {"sameObject", observe_same_object}, {"echoed", observe_echoed},        {"revoked", observe_revoked},
    {"unloads", observe_unloads},        {"holdingsReleased", observe_holdings_released},
    {"reported", observe_reported}};

/** Lets go of every bytes answer still kept, so the host's destroy and free find nothing of the run's held. */
static void drop_what_is_kept(library_run *run) {
    uint32_t at;
    for (at = 0; at < run->holding_count; at++) {
        if (run->holdings[at].kept.body.owner != NULL) {
            weivel_c_bytes_release(&run->holdings[at].kept.body);
        }
    }
}

int conformance_drive_library(conformance_world *world, const conformance_vector *vector, conformance_text *observed) {
    library_run run;
    const weivel_json_value *steps = weivel_json_object_get(vector->given, "steps");
    size_t at;
    int driven;
    memset(&run, 0, sizeof run);
    run.world = world;
    run.same_object = 1;
    unload_counter = 0;
    run.steps = steps;
    driven = build_and_check(&run, vector->given);
    world->reported_count = 0;
    for (at = 0; driven && at < weivel_json_array_length(steps); at = run.next_step) {
        run.next_step = at + 1;
        driven = run_step(&run, weivel_json_array_at(steps, at)) && !world->has_problem;
    }
    driven = driven && !world->has_problem &&
             conformance_observe(world, vector->then, LIBRARY_FIELDS, sizeof LIBRARY_FIELDS / sizeof LIBRARY_FIELDS[0],
                                 &run, observed);
    drop_what_is_kept(&run);
    conformance_world_end(world);
    weivel_standard_library_release(&run.declaration);
    return driven;
}
