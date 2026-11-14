/**
 * @file
 * How a load of a C library as host services ends when it does not finish, and which declarations it refuses: a load
 * undone once a service is registered goes through the unload path, so an object a consumer was told of stays
 * readable and answers unloaded, and nothing stays registered; an id the declaration lists twice, or one a
 * registration under the host holds already, a plugin's, another library's or the host's own, is the caller's
 * mistake, refused with the id named before anything is registered and without the host holding what it found; the
 * host registering an id a live library declares is refused the same way; and a declaration of no service or a
 * service of no function is refused as the standard's reader refuses it.
 */

#include "libraries.h"

static const char *library_path_of_probe;
static const char *library_path_of_echo;

/** What a watch of one id heard, and what it does on hearing it registered. */
typedef struct watch_told {
    void *service;
    uint32_t registered;
    uint32_t unregistered;
    void (*react)(struct watch_told *self);
    weivel_c_host *host;
    weivel_c_status reacted;
    weivel_c_library_load_failure failure;
} watch_told;

static weivel_c_status hear_watched(void *data, const weivel_c_service_change *change) {
    watch_told *self = data;
    if (change->change != WEIVEL_CHANGE_REGISTERED) {
        self->unregistered++;
        return WEIVEL_C_STATUS_OK;
    }
    self->service = change->service;
    if (self->registered++ == 0 && self->react != NULL) {
        self->react(self);
    }
    return WEIVEL_C_STATUS_OK;
}

static void watch_as_the_host(host_world *in, const char *id, watch_told *told) {
    weivel_c_context *context = NULL;
    weivel_c_service_watcher watcher;
    watcher.hear = hear_watched;
    watcher.data = told;
    told->host = in->host;
    CHECK(weivel_c_host_context(in->host, &context) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_watch_service(context, id, &watcher, NULL, NULL) == WEIVEL_C_STATUS_OK);
}

static weivel_c_status call_held(const void *held) {
    weivel_c_library_answer answer;
    const uint8_t input[] = {0, 'x'};
    weivel_c_status called = call_with(held, PROBE_ANSWER, input, sizeof input, &answer);
    weivel_c_bytes_release(&answer.body);
    return called;
}

/** Declares the probe's functions under two services, `first` and `second`. */
static void declare_two(declared *probe, weivel_standard_library_service two[2], const char *first,
                        const char *second) {
    declare_probe(probe, first);
    two[0] = probe->service;
    two[1] = probe->service;
    two[1].id = second;
    probe->library.services = two;
    probe->library.service_count = 2;
}

static weivel_c_status load_into(host_world *in, const char *path, declared *probe, weivel_c_library **library,
                                 weivel_c_library_load_failure *failure) {
    *failure = (weivel_c_library_load_failure)WEIVEL_INIT(weivel_c_library_load_failure);
    *library = (weivel_c_library *)probe;
    return weivel_c_host_load_library(in->host, path, &probe->library, library, failure, NULL);
}

static void destroy_on_hearing(watch_told *self) {
    weivel_c_host_destroy(self->host, NULL);
}

static void a_destroy_begun_while_a_load_registers_leaves_the_object_held_readable_and_unloaded(void) {
    host_world in;
    declared probe;
    watch_told told = {0};
    weivel_c_library *library;
    weivel_c_library_load_failure failure;
    uint32_t finished = 0;
    begin_host_world(&in);
    told.react = destroy_on_hearing;
    watch_as_the_host(&in, "probe", &told);
    declare_probe(&probe, "probe");
    CHECK(load_into(&in, library_path_of_probe, &probe, &library, &failure) == WEIVEL_C_STATUS_DESTROYED);
    CHECK(library == NULL && told.service != NULL);
    CHECK(weivel_c_host_destroy(in.host, &finished) == WEIVEL_C_STATUS_OK && finished == 1);
    CHECK(((const weivel_c_library_service *)told.service)->size == sizeof(weivel_c_library_service));
    CHECK(call_held(told.service) == WEIVEL_C_STATUS_UNLOADED);
    CHECK(sink_count(&in) == 0);
    CHECK(weivel_c_host_free(in.host) == WEIVEL_C_STATUS_OK);
}

static void a_later_service_failing_to_register_unregisters_the_ones_before_through_the_unload(void) {
    host_world in;
    declared probe;
    weivel_standard_library_service two[2];
    watch_told told = {0};
    weivel_c_library *library;
    weivel_c_library_load_failure failure;
    const void *held;
    uint32_t closes;
    begin_host_world(&in);
    watch_as_the_host(&in, "first", &told);
    declare_two(&probe, two, "first", "second");
    closes = weivel_c_testing_closes_made;
    weivel_c_testing_registration_to_refuse = 2;
    CHECK(load_into(&in, library_path_of_probe, &probe, &library, &failure) == WEIVEL_C_STATUS_OUT_OF_MEMORY);
    CHECK(library == NULL && weivel_c_testing_registration_to_refuse == 0);
    CHECK(told.registered == 1 && told.unregistered == 1 && told.service != NULL);
    CHECK(call_held(told.service) == WEIVEL_C_STATUS_UNLOADED);
    CHECK(service_of(&in, "first") == NULL && service_of(&in, "second") == NULL);
    CHECK(weivel_c_testing_closes_made == closes + 1 && sink_count(&in) == 0);
    held = told.service;
    CHECK(load_into(&in, library_path_of_probe, &probe, &library, &failure) == WEIVEL_C_STATUS_OK);
    CHECK(service_of(&in, "first") == told.service && told.service != held && told.registered == 2);
    end_host_world(&in);
}

static void refused_naming(host_world *in, const char *path, declared *probe, const char *taken) {
    weivel_c_library *library;
    weivel_c_library_load_failure failure;
    CHECK(load_into(in, path, probe, &library, &failure) == WEIVEL_C_STATUS_MISUSE);
    CHECK(library == NULL && failure.missing_symbol == NULL && failure.wrong_kind == NULL);
    if (taken == NULL) {
        CHECK(failure.taken_id == NULL);
    } else {
        check_text(failure.taken_id, taken, "the id taken already");
    }
}

static void an_id_another_library_registered_is_refused_and_the_first_stays_whole(void) {
    host_world in;
    declared first, second;
    weivel_c_library *library;
    const weivel_c_library_service *service;
    uint32_t closes;
    begin_host_world(&in);
    declare_probe(&first, "probe");
    library = load_declared(&in, library_path_of_probe, &first);
    service = service_of(&in, "probe");
    declare_echo(&second);
    second.service.id = "probe";
    closes = weivel_c_testing_closes_made;
    refused_naming(&in, library_path_of_echo, &second, "probe");
    CHECK(weivel_c_testing_closes_made == closes && service_of(&in, "probe") == service);
    CHECK(weivel_c_host_unload_library(in.host, library) == WEIVEL_C_STATUS_OK);
    CHECK(service_of(&in, "probe") == NULL && sink_count(&in) == 0);
    end_host_world(&in);
}

static void load_the_echo_declaring_second(watch_told *self) {
    static declared echo;
    weivel_c_library *library = (weivel_c_library *)&echo;
    declare_echo(&echo);
    echo.service.id = "second";
    self->failure = (weivel_c_library_load_failure)WEIVEL_INIT(weivel_c_library_load_failure);
    self->reacted = weivel_c_host_load_library(self->host, library_path_of_echo, &echo.library, &library,
                                               &self->failure, NULL);
}

static void an_id_a_library_still_loading_declares_is_refused_with_the_binary_closed(void) {
    host_world in;
    declared probe;
    weivel_standard_library_service two[2];
    watch_told told = {0};
    weivel_c_library *library;
    weivel_c_library_load_failure failure;
    const weivel_c_library_service *second;
    uint32_t closes = weivel_c_testing_closes_made;
    begin_host_world(&in);
    told.react = load_the_echo_declaring_second;
    watch_as_the_host(&in, "first", &told);
    declare_two(&probe, two, "first", "second");
    CHECK(load_into(&in, library_path_of_probe, &probe, &library, &failure) == WEIVEL_C_STATUS_OK);
    CHECK(told.reacted == WEIVEL_C_STATUS_MISUSE && weivel_c_testing_closes_made == closes + 1);
    check_text(told.failure.taken_id, "second", "the id a loading library declares");
    second = service_of(&in, "second");
    CHECK(second != NULL && second->function_count == PROBE_FUNCTIONS && sink_count(&in) == 0);
    end_host_world(&in);
}

static void an_id_the_declaration_lists_twice_is_refused_before_anything_opens(void) {
    host_world in;
    declared probe;
    weivel_standard_library_service two[2];
    uint32_t closes = weivel_c_testing_closes_made;
    begin_host_world(&in);
    declare_two(&probe, two, "twice", "twice");
    refused_naming(&in, library_path_of_probe, &probe, "twice");
    CHECK(service_of(&in, "twice") == NULL && weivel_c_testing_closes_made == closes && sink_count(&in) == 0);
    end_host_world(&in);
}

static void an_id_a_plugin_or_the_host_holds_is_refused_and_keeps_its_registration(void) {
    host_world in;
    declared probe;
    test_plugin owner;
    test_plugin *only[1];
    acting act = {"owner:thing", WEIVEL_C_STATUS_FAILED};
    int own = 0;
    begin_host_world(&in);
    name_acting_plugin(&owner, &in, "owner", activate_and_register, &act);
    offers_service(&owner, "owner:thing");
    register_test_plugin(&owner);
    only[0] = &owner;
    CHECK(activate_in_order(&in, only, 1, WEIVEL_EDGES_CAPABILITIES) && act.answered == WEIVEL_C_STATUS_OK);
    declare_probe(&probe, "owner:thing");
    refused_naming(&in, library_path_of_probe, &probe, "owner:thing");
    CHECK(service_of(&in, "owner:thing") == (const void *)&registered_object);
    CHECK(weivel_c_host_register_service(in.host, "own", &own, NULL, NULL, NULL) == WEIVEL_C_STATUS_OK);
    declare_probe(&probe, "own");
    refused_naming(&in, library_path_of_probe, &probe, "own");
    CHECK(service_of(&in, "own") == (const void *)&own && sink_count(&in) == 0);
    end_host_world(&in);
}

static void a_declaration_of_no_service_or_a_service_of_no_function_is_refused(void) {
    host_world in;
    declared probe;
    begin_host_world(&in);
    declare_probe(&probe, "probe");
    probe.library.service_count = 0;
    refused_naming(&in, library_path_of_probe, &probe, NULL);
    declare_probe(&probe, "probe");
    probe.service.function_count = 0;
    refused_naming(&in, library_path_of_probe, &probe, NULL);
    CHECK(service_of(&in, "probe") == NULL && sink_count(&in) == 0);
    end_host_world(&in);
}

static uint32_t revocations_heard;

static weivel_c_status count_revocation(void *data, const weivel_c_plugin_event *event) {
    (void)data;
    if (event->kind == WEIVEL_C_PLUGIN_EVENT_REVOKED) {
        revocations_heard++;
    }
    return WEIVEL_C_STATUS_OK;
}

static void listen_as_the_host(host_world *in) {
    weivel_c_context *context = NULL;
    weivel_c_plugin_listener listener;
    uint64_t listening = 0;
    listener.hear = count_revocation;
    listener.data = NULL;
    revocations_heard = 0;
    CHECK(weivel_c_host_context(in->host, &context) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_add_listener(context, &listener, &listening) == WEIVEL_C_STATUS_OK);
}

static void a_refused_load_leaves_the_host_holding_nothing_it_is_later_revoked(void) {
    host_world in;
    declared first, second;
    weivel_c_library *library;
    weivel_handle own_registration = 0;
    int own = 0;
    begin_host_world(&in);
    listen_as_the_host(&in);
    declare_probe(&first, "probe");
    library = load_declared(&in, library_path_of_probe, &first);
    declare_echo(&second);
    second.service.id = "probe";
    refused_naming(&in, library_path_of_echo, &second, "probe");
    CHECK(weivel_c_host_unload_library(in.host, library) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_host_register_service(in.host, "own", &own, NULL, &own_registration, NULL) == WEIVEL_C_STATUS_OK);
    declare_probe(&first, "own");
    refused_naming(&in, library_path_of_probe, &first, "own");
    CHECK(weivel_c_host_unregister_service(in.host, own_registration, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(revocations_heard == 0 && sink_count(&in) == 0);
    end_host_world(&in);
}

static void the_host_registering_an_id_a_live_library_declares_is_refused(void) {
    host_world in;
    declared probe;
    weivel_c_library *library;
    weivel_handle registration = 7;
    int own = 0;
    begin_host_world(&in);
    declare_probe(&probe, "probe");
    library = load_declared(&in, library_path_of_probe, &probe);
    CHECK(weivel_c_host_register_service(in.host, "probe", &own, NULL, &registration, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(registration == 0 && service_of(&in, "probe") != (const void *)&own);
    CHECK(weivel_c_host_unload_library(in.host, library) == WEIVEL_C_STATUS_OK);
    CHECK(sink_count(&in) == 0);
    CHECK(weivel_c_host_register_service(in.host, "probe", &own, NULL, NULL, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(service_of(&in, "probe") == (const void *)&own);
    end_host_world(&in);
}

static void a_failure_given_again_no_longer_names_the_id_it_named(void) {
    host_world in;
    declared probe;
    weivel_standard_library_service two[2];
    weivel_c_library *library = NULL;
    weivel_c_library_load_failure failure = WEIVEL_INIT(weivel_c_library_load_failure);
    begin_host_world(&in);
    declare_two(&probe, two, "twice", "twice");
    CHECK(weivel_c_host_load_library(in.host, library_path_of_probe, &probe.library, &library, &failure, NULL) ==
          WEIVEL_C_STATUS_MISUSE);
    check_text(failure.taken_id, "twice", "the id listed twice");
    declare_probe(&probe, "probe");
    probe.functions[PROBE_ANSWER].symbol = "no_such_symbol";
    CHECK(weivel_c_host_load_library(in.host, library_path_of_probe, &probe.library, &library, &failure, NULL) ==
          WEIVEL_C_STATUS_MISSING_SYMBOL);
    check_text(failure.missing_symbol, "no_such_symbol", "the symbol the library lacks");
    CHECK(failure.taken_id == NULL && failure.wrong_kind == NULL && library == NULL);
    end_host_world(&in);
}

int main(int argument_count, char **arguments) {
    if (argument_count != 4 || !load_tables(arguments[1])) {
        fprintf(stderr, "usage: host_library_loading_test <core-library> <service-library> <echo-library>\n");
        return 2;
    }
    library_path_of_probe = arguments[2];
    library_path_of_echo = arguments[3];
    a_destroy_begun_while_a_load_registers_leaves_the_object_held_readable_and_unloaded();
    a_later_service_failing_to_register_unregisters_the_ones_before_through_the_unload();
    an_id_another_library_registered_is_refused_and_the_first_stays_whole();
    an_id_a_library_still_loading_declares_is_refused_with_the_binary_closed();
    an_id_the_declaration_lists_twice_is_refused_before_anything_opens();
    an_id_a_plugin_or_the_host_holds_is_refused_and_keeps_its_registration();
    a_declaration_of_no_service_or_a_service_of_no_function_is_refused();
    a_refused_load_leaves_the_host_holding_nothing_it_is_later_revoked();
    the_host_registering_an_id_a_live_library_declares_is_refused();
    a_failure_given_again_no_longer_names_the_id_it_named();
    CHECK(weivel_c_core_unload(&real_table) == WEIVEL_C_STATUS_OK);
    return check_summary("host library loading");
}
