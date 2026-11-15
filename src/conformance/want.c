/**
 * @file
 * The `want` op: a consumer asks at time 0 on the driver's clock and the driver runs time to the horizon, registering
 * the provider at `registerAtMillis`. The scheduler runs the tasks due on the way there unless
 * `schedulerRunsBeforeRegistering` is false, when only the clock moves, so the registration meets a deadline the
 * layer's timer has not fired.
 */

#include <stdio.h>
#include <string.h>

#include "observe.h"
#include "ops.h"

typedef struct want_given {
    const char *id;
    int registers;
    int64_t register_at;
    int timed;
    int64_t timeout;
    int64_t horizon;
    int runs_before_registering;
} want_given;

/** How the want ended, when it did: served or refused, at the clock's time then. */
typedef struct want_run {
    conformance_world *world;
    int ended;
    int served;
    int timed_out;
    int64_t at;
    uint32_t code;
    char subject[128];
    int subject_given;
    uint32_t has_fix;
} want_run;

static uint32_t activate_and_answer_done(void *data, weivel_c_context *context, weivel_c_completion *completion,
                                         const char **reason) {
    world_plugin *plugin = data;
    (void)completion;
    (void)reason;
    plugin->context = context;
    return WEIVEL_C_ENTRY_DONE;
}

static int read_optional_number(const weivel_json_value *value, int64_t *number) {
    return weivel_json_kind_of(value) != WEIVEL_JSON_NULL && weivel_json_as_int64(value, number);
}

static int read_given(conformance_world *world, const weivel_json_value *given, want_given *read) {
    static const char *const keys[] = {"service", "registerAtMillis", "timeoutMillis", "horizonMillis",
                                       "schedulerRunsBeforeRegistering"};
    static const char *const service_keys[] = {"id"};
    const weivel_json_value *service = weivel_json_object_get(given, "service");
    int runs = 1;
    if (!conformance_world_knows_keys(world, given, keys, 5, "the given") ||
        !conformance_world_knows_keys(world, service, service_keys, 1, "the service")) {
        return 0;
    }
    read->id = weivel_json_as_string(weivel_json_object_get(service, "id"));
    read->registers = read_optional_number(weivel_json_object_get(given, "registerAtMillis"), &read->register_at);
    read->timed = read_optional_number(weivel_json_object_get(given, "timeoutMillis"), &read->timeout);
    if (read->id == NULL || !weivel_json_as_int64(weivel_json_object_get(given, "horizonMillis"), &read->horizon)) {
        return conformance_world_fail(world, "a want vector lacks a service id or a horizon", NULL);
    }
    weivel_json_as_bool(weivel_json_object_get(given, "schedulerRunsBeforeRegistering"), &runs);
    read->runs_before_registering = runs;
    return 1;
}

void conformance_want_core_options(const conformance_vector *vector, weivel_core_options *options,
                                   const char **well_known) {
    *well_known = weivel_json_as_string(weivel_json_object_get(weivel_json_object_get(vector->given, "service"), "id"));
    if (*well_known != NULL) {
        options->well_known_services.items = well_known;
        options->well_known_services.count = 1;
    }
}

static weivel_c_status follow_the_want(void *data, const weivel_c_settlement *settlement) {
    want_run *run = data;
    run->ended = 1;
    run->at = run->world->time.now;
    run->served = settlement->service != NULL;
    run->timed_out = settlement->service == NULL && settlement->refusal.code == WEIVEL_REFUSAL_TIMED_OUT;
    run->code = settlement->refusal.code;
    run->subject_given = settlement->refusal.subject != NULL;
    snprintf(run->subject, sizeof run->subject, "%s", run->subject_given ? settlement->refusal.subject : "");
    run->has_fix = settlement->refusal.has_fix;
    return WEIVEL_C_STATUS_OK;
}

static weivel_c_status ignore_outcome(void *data, const weivel_c_outcome *outcome) {
    (void)data;
    (void)outcome;
    return WEIVEL_C_STATUS_OK;
}

static int open_both(conformance_world *world, world_plugin *consumer, world_plugin *provider) {
    weivel_handle handles[2];
    weivel_c_activation activation = WEIVEL_INIT(weivel_c_activation);
    weivel_activation_plan plan = WEIVEL_INIT(weivel_activation_plan);
    handles[0] = consumer->handle;
    handles[1] = provider->handle;
    activation.plugins = handles;
    activation.count = 2;
    activation.edge_source = WEIVEL_EDGES_CAPABILITIES;
    activation.listener.hear = ignore_outcome;
    if (weivel_c_host_activate(world->host, &activation, &plan, NULL) != WEIVEL_C_STATUS_OK) {
        return conformance_world_fail(world, "the host did not activate the consumer and the provider", NULL);
    }
    weivel_c_host_plan_release(world->host, &plan);
    if (consumer->context == NULL || provider->context == NULL) {
        return conformance_world_fail(world, "the host did not hand the consumer and the provider their contexts",
                                      NULL);
    }
    return 1;
}

static int build_both(conformance_world *world, const char *id, world_plugin **consumer, world_plugin **provider) {
    *consumer = conformance_world_add(world, "consumer", activate_and_answer_done);
    *provider = conformance_world_add(world, "provider", activate_and_answer_done);
    if (*consumer == NULL || *provider == NULL) {
        return 0;
    }
    (*consumer)->wants[0] = (weivel_service_want)WEIVEL_INIT(weivel_service_want);
    (*consumer)->wants[0].id = id;
    (*consumer)->want_count = 1;
    (*provider)->offers[0] = (weivel_service_offer)WEIVEL_INIT(weivel_service_offer);
    (*provider)->offers[0].id = id;
    (*provider)->offer_count = 1;
    return conformance_world_register(world, *consumer) && conformance_world_register(world, *provider) &&
           open_both(world, *consumer, *provider);
}

static int ask(want_run *run, world_plugin *consumer, const want_given *given) {
    weivel_c_want_options options = WEIVEL_INIT(weivel_c_want_options);
    weivel_c_want_callback callback = {follow_the_want, run};
    options.timed = given->timed ? WEIVEL_CHOICE_YES : WEIVEL_CHOICE_NO;
    options.timeout_millis = given->timed ? given->timeout : 0;
    if (weivel_c_context_want_service(consumer->context, given->id, &options, &callback, NULL, NULL) !=
        WEIVEL_C_STATUS_OK) {
        return conformance_world_fail(run->world, "the consumer's want was not made", given->id);
    }
    return 1;
}

static int register_at(want_run *run, world_plugin *provider, const want_given *given) {
    if (!given->registers) {
        return 1;
    }
    if (given->runs_before_registering) {
        conformance_world_run_until(run->world, given->register_at);
    } else {
        conformance_world_move_clock_to(run->world, given->register_at);
    }
    if (weivel_c_context_register_service(provider->context, given->id, provider, NULL, NULL, NULL) !=
        WEIVEL_C_STATUS_OK) {
        return conformance_world_fail(run->world, "the provider's registration was not made", given->id);
    }
    return 1;
}

static void observe_settled(void *data, const weivel_json_value *expectation, conformance_text *out) {
    (void)expectation;
    conformance_text_append(out, ((want_run *)data)->served ? "true" : "false");
}

static void observe_timed_out(void *data, const weivel_json_value *expectation, conformance_text *out) {
    (void)expectation;
    conformance_text_append(out, ((want_run *)data)->timed_out ? "true" : "false");
}

static void observe_at(void *data, const weivel_json_value *expectation, conformance_text *out) {
    want_run *run = data;
    (void)expectation;
    if (run->ended) {
        conformance_text_append_integer(out, run->at);
    } else {
        conformance_text_append(out, "null");
    }
}

static void observe_refusal(void *data, const weivel_json_value *expectation, conformance_text *out) {
    want_run *run = data;
    (void)expectation;
    if (!run->ended || run->served) {
        conformance_text_append(out, "null");
        return;
    }
    conformance_write_refusal(out, run->code, run->subject_given ? run->subject : NULL, run->has_fix);
}

static void observe_timer(void *data, const weivel_json_value *expectation, conformance_text *out) {
    conformance_world *world = ((want_run *)data)->world;
    uint32_t index;
    (void)expectation;
    conformance_text_append(out, "[");
    for (index = 0; index < world->move_count; index++) {
        conformance_text_append(out, index == 0 ? "[" : ",[");
        conformance_text_append_integer(out, world->moves[index].at);
        conformance_text_append(out, ",");
        if (world->moves[index].armed) {
            conformance_text_append_integer(out, world->moves[index].deadline);
        } else {
            conformance_text_append(out, "null");
        }
        conformance_text_append(out, "]");
    }
    conformance_text_append(out, "]");
}

static void observe_fired(void *data, const weivel_json_value *expectation, conformance_text *out) {
    conformance_world *world = ((want_run *)data)->world;
    uint32_t index;
    (void)expectation;
    conformance_text_append(out, "[");
    for (index = 0; index < world->time.fired_count; index++) {
        conformance_text_append(out, index == 0 ? "" : ",");
        conformance_text_append_integer(out, world->time.fired[index]);
    }
    conformance_text_append(out, "]");
}

static const conformance_field WANT_FIELDS[] = {{"settled", observe_settled}, {"timedOut", observe_timed_out},
                                                {"atMillis", observe_at},     {"refusal", observe_refusal},
                                                {"timer", observe_timer},     {"fired", observe_fired}};

int conformance_drive_want(conformance_world *world, const conformance_vector *vector, conformance_text *observed) {
    want_given given;
    want_run run;
    world_plugin *consumer = NULL;
    world_plugin *provider = NULL;
    int driven;
    memset(&given, 0, sizeof given);
    memset(&run, 0, sizeof run);
    run.world = world;
    driven = read_given(world, vector->given, &given) && build_both(world, given.id, &consumer, &provider) &&
             ask(&run, consumer, &given) && register_at(&run, provider, &given);
    if (driven) {
        conformance_world_run_until(world, given.horizon);
        driven = conformance_observe(world, vector->then, WANT_FIELDS, sizeof WANT_FIELDS / sizeof WANT_FIELDS[0],
                                     &run, observed);
    }
    conformance_world_end(world);
    return driven;
}
