/**
 * @file
 * C libraries as host services: a declaration value loads a library and registers its services under WEIVEL_HOST,
 * input crosses borrowed, output is answered in place and released once through the declared release symbol, a
 * non-zero status answers its status and body, unloading unregisters and refuses, a declaration of another kind is
 * the caller's mistake, and the destroy unloads every library, telling the sink of one that does not close.
 */

#include "libraries.h"

static const char *library_path_of_probe;
static const char *library_path_of_echo;
static const char *not_a_library_path;
static uint32_t probe_releases;

typedef struct resolving {
    const char *id;
    void *service;
    weivel_c_status called;
} resolving;

static uint32_t activate_and_resolve(void *data, weivel_c_context *context, weivel_c_completion *completion,
                                     const char **reason) {
    test_plugin *self = data;
    resolving *wanted = self->more;
    (void)completion;
    (void)reason;
    self->context = context;
    weivel_c_context_get_service(context, wanted->id, &wanted->service, NULL);
    return WEIVEL_C_ENTRY_DONE;
}

static uint32_t deactivate_and_call(void *data, weivel_c_context *context, weivel_c_completion *completion,
                                    const char **reason) {
    test_plugin *self = data;
    resolving *wanted = self->more;
    weivel_c_library_answer answer;
    const uint8_t input[] = {0, 'b', 'y', 'e'};
    (void)context;
    (void)completion;
    (void)reason;
    wanted->called = call_with(wanted->service, PROBE_ANSWER, input, sizeof input, &answer);
    weivel_c_bytes_release(&answer.body);
    return WEIVEL_C_ENTRY_DONE;
}

static void start_a_consumer(host_world *in, test_plugin *plugin, resolving *wanted,
                             weivel_c_entry_function deactivate) {
    test_plugin *only[1];
    uint32_t kind = 0;
    name_test_plugin(plugin, in, "consumer");
    wants_service(plugin, wanted->id);
    plugin->entry.activate = activate_and_resolve;
    plugin->entry.deactivate = deactivate;
    plugin->more = wanted;
    register_test_plugin(plugin);
    only[0] = plugin;
    CHECK(activate_in_order(in, only, 1, WEIVEL_EDGES_CAPABILITIES));
    CHECK(outcomes_of(&in->told, plugin->handle, &kind) == 1 && kind == WEIVEL_C_OUTCOME_ACTIVE);
}

static void each_declared_service_is_registered_under_the_host_as_the_object_a_consumer_resolves(void) {
    host_world in;
    declared probe;
    test_plugin consumer;
    resolving wanted = {"probe", NULL, WEIVEL_C_STATUS_OK};
    const weivel_c_library_service *service;
    begin_host_world(&in);
    declare_probe(&probe, "probe");
    CHECK(load_declared(&in, library_path_of_probe, &probe) != NULL);
    service = service_of(&in, "probe");
    CHECK(service != NULL && service->size == sizeof *service);
    check_text(service->id, "probe", "a library service's id");
    CHECK(service->function_count == PROBE_FUNCTIONS && service->release_bytes == weivel_c_bytes_release);
    check_text(service->function_names[PROBE_CALL_BACK], "call_back", "a library function's name");
    CHECK(service->function_names[0] != probe.functions[0].name);
    start_a_consumer(&in, &consumer, &wanted, NULL);
    CHECK(wanted.service == service);
    CHECK(sink_count(&in) == 0);
    end_host_world(&in);
}

static void a_call_through_the_service_crosses_into_no_core(void) {
    host_world in;
    declared probe;
    const weivel_c_library_service *service;
    weivel_c_library_answer answer;
    crossings before;
    const uint8_t input[] = {0, 'n', 'o'};
    begin_host_world(&in);
    declare_probe(&probe, "probe");
    load_declared(&in, library_path_of_probe, &probe);
    service = service_of(&in, "probe");
    before = crossed;
    CHECK(call_with(service, PROBE_ANSWER, input, sizeof input, &answer) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_bytes_release(&answer.body) == WEIVEL_C_STATUS_OK);
    CHECK(memcmp(&before, &crossed, sizeof before) == 0);
    end_host_world(&in);
}

static void the_input_is_borrowed_and_the_output_answered_in_place_and_released_once(void) {
    host_world in;
    declared probe;
    const weivel_c_library_service *service;
    weivel_c_library_answer answer;
    const uint8_t input[] = {0, 'a', 'b'};
    const uint8_t *seen = NULL;
    begin_host_world(&in);
    declare_probe(&probe, "probe");
    load_declared(&in, library_path_of_probe, &probe);
    service = service_of(&in, "probe");
    probe_releases = 0;
    attach_counter(service, &probe_releases);
    CHECK(call_with(service, PROBE_INPUT_ADDRESS, input, sizeof input, &answer) == WEIVEL_C_STATUS_OK);
    CHECK(answer.status == 0 && answer.body.length == sizeof seen);
    memcpy(&seen, answer.body.data, sizeof seen);
    CHECK(seen == input);
    CHECK(weivel_c_bytes_release(&answer.body) == WEIVEL_C_STATUS_OK && probe_releases == 1);
    CHECK(call_with(service, PROBE_OWN_ADDRESS, NULL, 0, &answer) == WEIVEL_C_STATUS_OK);
    memcpy(&seen, answer.body.data, sizeof seen);
    CHECK(seen == answer.body.data);
    CHECK(weivel_c_bytes_release(&answer.body) == WEIVEL_C_STATUS_OK && probe_releases == 2);
    CHECK(answer.body.data == NULL && answer.body.length == 0 && answer.body.owner == NULL);
    CHECK(weivel_c_bytes_release(&answer.body) == WEIVEL_C_STATUS_OK && probe_releases == 2);
    CHECK(weivel_c_bytes_release(NULL) == WEIVEL_C_STATUS_MISUSE);
    end_host_world(&in);
}

/** Calls the probe's `answer` with `input`, checks what it answered against the rest, and releases the body. */
static void answers(const weivel_c_library_service *probe, const uint8_t *input, size_t length,
                    weivel_c_status expected, int32_t status, uint32_t utf8) {
    weivel_c_library_answer answer;
    CHECK(call_with(probe, PROBE_ANSWER, input, length, &answer) == expected);
    CHECK(answer.status == status && answer.body_is_utf8 == utf8);
    CHECK(answer.body.length == length - 1 && memcmp(answer.body.data, input + 1, length - 1) == 0);
    CHECK(weivel_c_bytes_release(&answer.body) == WEIVEL_C_STATUS_OK);
}

static void a_status_other_than_zero_answers_the_status_and_its_body_read_as_utf8_where_it_is_one(void) {
    host_world in;
    declared probe;
    const weivel_c_library_service *service;
    const uint8_t why[] = {3, 'w', 'h', 'y'}, smile[] = {9, 0xF0, 0x9F, 0x98, 0x80}, empty[] = {4};
    const uint8_t bytes[] = {5, 0xFF, 0xFE}, overlong[] = {6, 0xC0, 0x80}, surrogate[] = {7, 0xED, 0xA0, 0x80};
    const uint8_t cut[] = {8, 'a', 0xE2, 0x82}, result[] = {0, 0xFF}, long_three[] = {10, 0xE0, 0x9F, 0xBF};
    const uint8_t long_four[] = {11, 0xF0, 0x8F, 0xBF, 0xBF}, beyond[] = {12, 0xF4, 0x90, 0x80, 0x80};
    begin_host_world(&in);
    declare_probe(&probe, "probe");
    load_declared(&in, library_path_of_probe, &probe);
    service = service_of(&in, "probe");
    answers(service, why, sizeof why, WEIVEL_C_STATUS_FAILED, 3, 1);
    answers(service, smile, sizeof smile, WEIVEL_C_STATUS_FAILED, 9, 1);
    answers(service, empty, sizeof empty, WEIVEL_C_STATUS_FAILED, 4, 1);
    answers(service, bytes, sizeof bytes, WEIVEL_C_STATUS_FAILED, 5, 0);
    answers(service, overlong, sizeof overlong, WEIVEL_C_STATUS_FAILED, 6, 0);
    answers(service, surrogate, sizeof surrogate, WEIVEL_C_STATUS_FAILED, 7, 0);
    answers(service, cut, sizeof cut, WEIVEL_C_STATUS_FAILED, 8, 0);
    answers(service, long_three, sizeof long_three, WEIVEL_C_STATUS_FAILED, 10, 0);
    answers(service, long_four, sizeof long_four, WEIVEL_C_STATUS_FAILED, 11, 0);
    answers(service, beyond, sizeof beyond, WEIVEL_C_STATUS_FAILED, 12, 0);
    answers(service, result, sizeof result, WEIVEL_C_STATUS_OK, 0, 0);
    CHECK(sink_count(&in) == 0);
    end_host_world(&in);
}

static void the_echo_library_answers_its_bytes_and_its_refusal(void) {
    host_world in;
    declared echo;
    const weivel_c_library_service *service;
    weivel_c_library_answer answer;
    begin_host_world(&in);
    declare_echo(&echo);
    load_declared(&in, library_path_of_echo, &echo);
    service = service_of(&in, "echo");
    CHECK(call_with(service, ECHO_ECHO, "hello", 5, &answer) == WEIVEL_C_STATUS_OK && body_is(&answer.body, "hello"));
    CHECK(weivel_c_bytes_release(&answer.body) == WEIVEL_C_STATUS_OK);
    CHECK(call_with(service, ECHO_FAIL, NULL, 0, &answer) == WEIVEL_C_STATUS_FAILED);
    CHECK(answer.status == 1 && answer.body_is_utf8 == 1 && body_is(&answer.body, "echo refused on purpose"));
    CHECK(weivel_c_bytes_release(&answer.body) == WEIVEL_C_STATUS_OK);
    CHECK(call_with(service, ECHO_GARBAGE, NULL, 0, &answer) == WEIVEL_C_STATUS_OK && answer.body.length == 2);
    CHECK(weivel_c_bytes_release(&answer.body) == WEIVEL_C_STATUS_OK);
    CHECK(call_with(service, ECHO_FUNCTIONS, NULL, 0, &answer) == WEIVEL_C_STATUS_MISUSE);
    CHECK(call_with(service, ECHO_ECHO, NULL, 3, &answer) == WEIVEL_C_STATUS_MISUSE);
    CHECK(service->call(service, ECHO_ECHO, NULL, 0, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(service->call(NULL, ECHO_ECHO, NULL, 0, &answer) == WEIVEL_C_STATUS_MISUSE);
    end_host_world(&in);
}

static void unloading_unregisters_refuses_and_closes_once_the_last_bytes_are_released(void) {
    host_world in;
    declared probe;
    weivel_c_library *library;
    const weivel_c_library_service *service;
    weivel_c_library_answer held, refused;
    const uint8_t kept[] = {0, 'k', 'e', 'p', 't'};
    uint32_t closes;
    begin_host_world(&in);
    declare_probe(&probe, "probe");
    library = load_declared(&in, library_path_of_probe, &probe);
    service = service_of(&in, "probe");
    probe_releases = 0;
    attach_counter(service, &probe_releases);
    CHECK(call_with(service, PROBE_ANSWER, kept, sizeof kept, &held) == WEIVEL_C_STATUS_OK);
    closes = weivel_c_testing_closes_made;
    CHECK(weivel_c_host_unload_library(in.host, library) == WEIVEL_C_STATUS_OK);
    CHECK(service_of(&in, "probe") == NULL);
    CHECK(call_with(service, PROBE_ANSWER, kept, sizeof kept, &refused) == WEIVEL_C_STATUS_UNLOADED);
    CHECK(refused.body.data == NULL && refused.body.owner == NULL);
    CHECK(weivel_c_testing_closes_made == closes && body_is(&held.body, "kept"));
    CHECK(weivel_c_bytes_release(&held.body) == WEIVEL_C_STATUS_OK);
    CHECK(probe_releases == 1 && weivel_c_testing_closes_made == closes + 1);
    CHECK(weivel_c_host_unload_library(in.host, library) == WEIVEL_C_STATUS_NONE);
    CHECK(weivel_c_host_unload_library(in.host, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(sink_count(&in) == 0);
    end_host_world(&in);
}

typedef struct unloading {
    weivel_c_host *host;
    weivel_c_library *library;
    weivel_c_status unloaded;
    uint32_t closes_made;
} unloading;

static void unload_meanwhile(void *data) {
    unloading *self = data;
    self->unloaded = weivel_c_host_unload_library(self->host, self->library);
    self->closes_made = weivel_c_testing_closes_made;
}

static void a_call_running_as_its_library_is_unloaded_finishes_before_the_library_closes(void) {
    host_world in;
    declared probe;
    const weivel_c_library_service *service;
    weivel_c_library_answer answer, later;
    unloading meanwhile = {NULL, NULL, WEIVEL_C_STATUS_FAILED, 0};
    service_library_callback callback = {unload_meanwhile, &meanwhile};
    const service_library_callback *pointer = &callback;
    uint32_t closes = weivel_c_testing_closes_made;
    begin_host_world(&in);
    declare_probe(&probe, "probe");
    meanwhile.host = in.host;
    meanwhile.library = load_declared(&in, library_path_of_probe, &probe);
    service = service_of(&in, "probe");
    CHECK(call_with(service, PROBE_CALL_BACK, &pointer, sizeof pointer, &answer) == WEIVEL_C_STATUS_OK);
    CHECK(meanwhile.unloaded == WEIVEL_C_STATUS_OK && meanwhile.closes_made == closes);
    CHECK(body_is(&answer.body, "done") && weivel_c_testing_closes_made == closes);
    CHECK(call_with(service, PROBE_ANSWER, NULL, 0, &later) == WEIVEL_C_STATUS_UNLOADED);
    CHECK(weivel_c_bytes_release(&answer.body) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_testing_closes_made == closes + 1);
    end_host_world(&in);
}

static void refused_as_misuse(host_world *in, declared *declaration, const char *wrong_kind) {
    weivel_c_library *library = (weivel_c_library *)declaration;
    weivel_c_library_load_failure failure = WEIVEL_INIT(weivel_c_library_load_failure);
    CHECK(weivel_c_host_load_library(in->host, library_path_of_probe, &declaration->library, &library, &failure,
                                     NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(library == NULL && failure.missing_symbol == NULL);
    if (wrong_kind != NULL) {
        check_text(failure.wrong_kind, wrong_kind, "the function of another kind");
    }
}

static void missing(host_world *in, const char *path, declared *declaration, weivel_c_status expected,
                    const char *symbol) {
    weivel_c_library *library = NULL;
    weivel_c_library_load_failure failure = WEIVEL_INIT(weivel_c_library_load_failure);
    CHECK(weivel_c_host_load_library(in->host, path, &declaration->library, &library, &failure, NULL) == expected);
    CHECK(library == NULL && failure.wrong_kind == NULL);
    if (symbol != NULL) {
        check_text(failure.missing_symbol, symbol, "the symbol a library lacks");
    }
}

static void a_declaration_of_another_kind_is_the_callers_mistake_told_to_nobody_else(void) {
    host_world in;
    declared probe;
    begin_host_world(&in);
    declare_probe(&probe, "probe");
    probe.functions[PROBE_ATTACH].kind = 1;
    refused_as_misuse(&in, &probe, "attach");
    probe.functions[PROBE_ATTACH].kind = 3;
    refused_as_misuse(&in, &probe, "attach");
    declare_probe(&probe, "probe");
    probe.functions[PROBE_ANSWER].symbol = "";
    refused_as_misuse(&in, &probe, NULL);
    declare_probe(&probe, "");
    refused_as_misuse(&in, &probe, NULL);
    declare_probe(&probe, "probe");
    probe.library.size = 4;
    refused_as_misuse(&in, &probe, NULL);
    CHECK(service_of(&in, "probe") == NULL && sink_count(&in) == 0);
    end_host_world(&in);
}

static void a_library_that_is_none_or_lacks_a_symbol_is_answered_as_a_value(void) {
    host_world in;
    declared probe;
    begin_host_world(&in);
    declare_probe(&probe, "probe");
    missing(&in, not_a_library_path, &probe, WEIVEL_C_STATUS_NOT_A_LIBRARY, NULL);
    missing(&in, "", &probe, WEIVEL_C_STATUS_MISUSE, NULL);
    probe.library.release = "no_such_release";
    missing(&in, library_path_of_probe, &probe, WEIVEL_C_STATUS_MISSING_SYMBOL, "no_such_release");
    declare_probe(&probe, "probe");
    probe.functions[PROBE_ANSWER].symbol = "no_such_function";
    missing(&in, library_path_of_probe, &probe, WEIVEL_C_STATUS_MISSING_SYMBOL, "no_such_function");
    CHECK(service_of(&in, "probe") == NULL && sink_count(&in) == 0);
    end_host_world(&in);
}

static void the_destroy_unloads_every_library_after_every_release_telling_the_sink_of_one_that_does_not_close(void) {
    host_world in;
    declared probe, echo;
    test_plugin consumer;
    resolving wanted = {"probe", NULL, WEIVEL_C_STATUS_FAILED};
    weivel_c_library_answer after;
    uint32_t finished = 0, closes;
    begin_host_world(&in);
    declare_probe(&probe, "probe");
    declare_echo(&echo);
    load_declared(&in, library_path_of_probe, &probe);
    load_declared(&in, library_path_of_echo, &echo);
    start_a_consumer(&in, &consumer, &wanted, deactivate_and_call);
    closes = weivel_c_testing_closes_made;
    weivel_c_testing_closes_to_refuse = 1;
    CHECK(weivel_c_host_destroy(in.host, &finished) == WEIVEL_C_STATUS_OK && finished == 1);
    CHECK(wanted.called == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_testing_closes_made == closes + 2 && weivel_c_testing_closes_to_refuse == 0);
    CHECK(sink_count(&in) == 1 && in.sink.items[0].failure.kind == WEIVEL_C_FAILURE_UNLOAD);
    CHECK(call_with(wanted.service, PROBE_ANSWER, NULL, 0, &after) == WEIVEL_C_STATUS_UNLOADED);
    CHECK(weivel_c_host_free(in.host) == WEIVEL_C_STATUS_OK);
}

static void the_host_is_not_freed_while_bytes_its_libraries_answered_are_held(void) {
    host_world in;
    declared probe;
    const weivel_c_library_service *service;
    weivel_c_library_answer held;
    const uint8_t kept[] = {0, 'k', 'e', 'p', 't'};
    uint32_t finished = 0;
    begin_host_world(&in);
    declare_probe(&probe, "probe");
    load_declared(&in, library_path_of_probe, &probe);
    service = service_of(&in, "probe");
    CHECK(call_with(service, PROBE_ANSWER, kept, sizeof kept, &held) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_host_destroy(in.host, &finished) == WEIVEL_C_STATUS_OK && finished == 1);
    CHECK(weivel_c_host_free(in.host) == WEIVEL_C_STATUS_MISUSE);
    CHECK(body_is(&held.body, "kept"));
    CHECK(weivel_c_bytes_release(&held.body) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_host_free(in.host) == WEIVEL_C_STATUS_OK);
    CHECK(sink_count(&in) == 0);
}

int main(int argument_count, char **arguments) {
    if (argument_count != 5 || !load_tables(arguments[1])) {
        fprintf(stderr, "usage: host_library_test <core-library> <service-library> <echo-library> <no-library>\n");
        return 2;
    }
    library_path_of_probe = arguments[2];
    library_path_of_echo = arguments[3];
    not_a_library_path = arguments[4];
    each_declared_service_is_registered_under_the_host_as_the_object_a_consumer_resolves();
    a_call_through_the_service_crosses_into_no_core();
    the_input_is_borrowed_and_the_output_answered_in_place_and_released_once();
    a_status_other_than_zero_answers_the_status_and_its_body_read_as_utf8_where_it_is_one();
    the_echo_library_answers_its_bytes_and_its_refusal();
    unloading_unregisters_refuses_and_closes_once_the_last_bytes_are_released();
    a_call_running_as_its_library_is_unloaded_finishes_before_the_library_closes();
    a_declaration_of_another_kind_is_the_callers_mistake_told_to_nobody_else();
    a_library_that_is_none_or_lacks_a_symbol_is_answered_as_a_value();
    the_destroy_unloads_every_library_after_every_release_telling_the_sink_of_one_that_does_not_close();
    the_host_is_not_freed_while_bytes_its_libraries_answered_are_held();
    CHECK(weivel_c_core_unload(&real_table) == WEIVEL_C_STATUS_OK);
    return check_summary("host library");
}
