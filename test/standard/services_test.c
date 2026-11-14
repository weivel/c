/**
 * @file
 * The config, secrets and paths services as the standard defines them, offered by a host written in C: each is the
 * caller's own definition registered as a host service under the id the standard's binary answers, so a plugin that
 * resolves the config service through its context holds that very object and calls through it; a definition smaller
 * than its first shape is the caller's mistake.
 */

#include <weivel/c/standard.h>

#include "host/plugins.h"

static weivel_status get_the_answer(void *context, const char *key, weivel_standard_setting_answer *answer,
                                    weivel_standard_service_refusal *refusal) {
    (void)context;
    (void)refusal;
    if (strcmp(key, "answer") != 0) {
        return WEIVEL_STATUS_NONE;
    }
    answer->value.kind = WEIVEL_STANDARD_SETTING_NUMBER;
    answer->value.number = 42.0;
    return WEIVEL_STATUS_OK;
}

static void release_nothing(void *context, weivel_standard_setting_answer *answer) {
    (void)context;
    (void)answer;
}

static weivel_standard_config config_answering_42(void) {
    weivel_standard_config config;
    memset(&config, 0, sizeof config);
    config.size = (uint32_t)sizeof config;
    config.get = get_the_answer;
    config.release_answer = release_nothing;
    return config;
}

/** What the consumer resolved and what a call through it answered. */
typedef struct consumed {
    void *service;
    weivel_status got;
    double number;
} consumed;

static uint32_t activate_and_ask_the_config(void *data, weivel_c_context *context, weivel_c_completion *completion,
                                            const char **reason) {
    test_plugin *self = data;
    consumed *seen = self->more;
    weivel_standard_setting_answer answer = WEIVEL_INIT(weivel_standard_setting_answer);
    const weivel_standard_config *config;
    (void)completion;
    (void)reason;
    if (weivel_c_context_get_service(context, weivel_standard_service_config(), &seen->service, NULL) !=
        WEIVEL_C_STATUS_OK) {
        return WEIVEL_C_ENTRY_FAILED;
    }
    config = seen->service;
    seen->got = WEIVEL_STANDARD_OFFERS(weivel_standard_config, config, get)
                    ? config->get(config->context, "answer", &answer, NULL)
                    : WEIVEL_STATUS_MALFORMED;
    seen->number = answer.value.number;
    config->release_answer(config->context, &answer);
    return WEIVEL_C_ENTRY_DONE;
}

static void a_plugin_resolves_the_hosts_config_as_that_object_and_calls_through_it(void) {
    host_world in;
    test_plugin consumer;
    test_plugin *only[1];
    weivel_standard_config config = config_answering_42();
    consumed seen = {NULL, WEIVEL_STATUS_REFUSED, 0.0};
    weivel_handle registration = 0;
    uint32_t kind = 0;
    begin_host_world(&in);
    CHECK(weivel_c_standard_offer_config(in.host, &config, &registration, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(registration != 0);
    name_test_plugin(&consumer, &in, "consumer");
    wants_service(&consumer, weivel_standard_service_config());
    consumer.entry.activate = activate_and_ask_the_config;
    consumer.more = &seen;
    register_test_plugin(&consumer);
    only[0] = &consumer;
    CHECK(activate_in_order(&in, only, 1, WEIVEL_EDGES_CAPABILITIES));
    CHECK(outcomes_of(&in.told, consumer.handle, &kind) == 1 && kind == WEIVEL_C_OUTCOME_ACTIVE);
    CHECK(seen.service == (void *)&config && seen.got == WEIVEL_STATUS_OK && seen.number == 42.0);
    CHECK(sink_count(&in) == 0);
    end_host_world(&in);
}

static const void *resolved_by_the_host(host_world *in, const char *id) {
    weivel_c_context *context = NULL;
    void *service = NULL;
    CHECK(weivel_c_host_context(in->host, &context) == WEIVEL_C_STATUS_OK);
    weivel_c_context_get_service(context, id, &service, NULL);
    return service;
}

static void secrets_and_paths_are_offered_under_the_standards_ids(void) {
    host_world in;
    weivel_standard_secrets secrets;
    weivel_standard_paths paths;
    begin_host_world(&in);
    memset(&secrets, 0, sizeof secrets);
    memset(&paths, 0, sizeof paths);
    secrets.size = (uint32_t)sizeof secrets;
    paths.size = (uint32_t)sizeof paths;
    CHECK(weivel_c_standard_offer_secrets(in.host, &secrets, NULL, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_standard_offer_paths(in.host, &paths, NULL, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(resolved_by_the_host(&in, weivel_standard_service_secrets()) == &secrets);
    CHECK(resolved_by_the_host(&in, weivel_standard_service_paths()) == &paths);
    CHECK(strcmp(weivel_standard_service_secrets(), WEIVEL_STANDARD_SERVICE_SECRETS) == 0);
    CHECK(strcmp(weivel_standard_service_paths(), WEIVEL_STANDARD_SERVICE_PATHS) == 0);
    CHECK(strcmp(weivel_standard_service_config(), WEIVEL_STANDARD_SERVICE_CONFIG) == 0);
    end_host_world(&in);
}

static void a_definition_smaller_than_its_first_shape_is_the_callers_mistake(void) {
    host_world in;
    weivel_standard_config config = config_answering_42();
    weivel_standard_secrets secrets;
    weivel_standard_paths paths;
    begin_host_world(&in);
    memset(&secrets, 0, sizeof secrets);
    memset(&paths, 0, sizeof paths);
    config.size = (uint32_t)offsetof(weivel_standard_config, release_refusal);
    secrets.size = (uint32_t)offsetof(weivel_standard_secrets, release_refusal);
    paths.size = (uint32_t)offsetof(weivel_standard_paths, plugin);
    CHECK(weivel_c_standard_offer_config(in.host, &config, NULL, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_standard_offer_secrets(in.host, &secrets, NULL, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_standard_offer_paths(in.host, &paths, NULL, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_standard_offer_config(in.host, NULL, NULL, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(resolved_by_the_host(&in, weivel_standard_service_config()) == NULL && sink_count(&in) == 0);
    end_host_world(&in);
}

int main(int argument_count, char **arguments) {
    if (argument_count != 2 || !load_tables(arguments[1])) {
        fprintf(stderr, "usage: standard_services_test <core-library>\n");
        return 2;
    }
    a_plugin_resolves_the_hosts_config_as_that_object_and_calls_through_it();
    secrets_and_paths_are_offered_under_the_standards_ids();
    a_definition_smaller_than_its_first_shape_is_the_callers_mistake();
    CHECK(weivel_c_core_unload(&real_table) == WEIVEL_C_STATUS_OK);
    return check_summary("standard services");
}
