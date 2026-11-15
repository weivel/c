/**
 * @file
 * Definition services and the registrations that await their releases: a definition loads without a release symbol
 * and is registered as the very pointer its symbol answered; one beside another function, given context facts,
 * answering NULL or below its floor is the caller's mistake told to nobody, leaving nothing registered; an owner
 * hears its release event once every holding, a watch's among them, has been released; a holding still live, or
 * released already, is refused; a load undone after one definition registered closes its binary once that
 * definition's release is heard; a holder the destroy releases ends its holding, so the binary closes then; and a
 * holder that never lets go, or a definition still registered at the destroy, keeps the binary open until the free.
 */

#include "libraries.h"

#include "../../src/core/binary.h"

static const char *library_path_of_echo;

enum { ECHO_DEFINITION, ECHO_OTHER_DEFINITION, ECHO_BYTES, DECLARED_SERVICES };

/** The echo library declared as a caller builds it: two definitions and, optionally, a bytes service. */
typedef struct definitions {
    weivel_standard_library_function functions[DECLARED_SERVICES];
    weivel_standard_library_service services[DECLARED_SERVICES];
    weivel_standard_library library;
} definitions;

static void declare_one(definitions *self, uint32_t index, const char *id, const char *symbol, uint32_t kind) {
    self->functions[index].name = kind == WEIVEL_STANDARD_FUNCTION_DEFINITION ? "definition" : "echo";
    self->functions[index].symbol = symbol;
    self->functions[index].kind = kind;
    self->functions[index].context = 0;
    self->services[index].id = id;
    self->services[index].functions = &self->functions[index];
    self->services[index].function_count = 1;
}

static void declare_definitions(definitions *self, const char *first_symbol, uint32_t count) {
    memset(self, 0, sizeof *self);
    declare_one(self, ECHO_DEFINITION, "echo", first_symbol, WEIVEL_STANDARD_FUNCTION_DEFINITION);
    declare_one(self, ECHO_OTHER_DEFINITION, "echo-other", "echo_other_service_definition",
                WEIVEL_STANDARD_FUNCTION_DEFINITION);
    declare_one(self, ECHO_BYTES, "echo-bytes", "echo_echo", WEIVEL_STANDARD_FUNCTION_BYTES);
    self->library = (weivel_standard_library)WEIVEL_INIT(weivel_standard_library);
    self->library.import_name = "echo";
    self->library.version = "1.0.0";
    self->library.services = self->services;
    self->library.service_count = count;
}

/** Declares the bytes service first and the definition `symbol` after it, as the vectors that refuse a load do. */
static void declare_bytes_then(definitions *self, const char *symbol) {
    declare_definitions(self, symbol, 0);
    declare_one(self, 0, "echo-bytes", "echo_echo", WEIVEL_STANDARD_FUNCTION_BYTES);
    declare_one(self, 1, "echo", symbol, WEIVEL_STANDARD_FUNCTION_DEFINITION);
    self->library.release = "echo_release";
    self->library.service_count = 2;
}

static weivel_c_status load_definitions(host_world *in, definitions *declared, weivel_c_library **library,
                                        weivel_c_library_load_failure *failure) {
    *library = NULL;
    return weivel_c_host_load_library(in->host, library_path_of_echo, &declared->library, library, failure, NULL);
}

/** What the echo library's own symbol answers, read through the platform's loader while the host holds it open. */
static const void *answered_by(const char *symbol) {
    void *binary = NULL;
    const void *answered = NULL;
    weivel_c_binary_function found;
    CHECK(weivel_c_binary_open(library_path_of_echo, &binary) == WEIVEL_C_STATUS_OK);
    found = weivel_c_binary_find(binary, symbol);
    CHECK(found != NULL);
    if (found != NULL) {
        answered = ((const void *(*)(void))found)();
    }
    weivel_c_binary_close(binary);
    return answered;
}

typedef struct echo_definition_view {
    uint32_t size;
    void *context;
    int32_t (*echo)(void *context, const uint8_t *input, size_t input_length, uint8_t **output,
                    size_t *output_length);
    void (*release)(void *context, uint8_t *buffer);
} echo_definition_view;

static void a_library_of_definitions_loads_without_a_release_and_registers_each_symbols_own_pointer(void) {
    host_world in;
    definitions declared;
    weivel_c_library *library;
    const echo_definition_view *definition;
    uint8_t *output = NULL;
    size_t length = 0;
    crossings before;
    begin_host_world(&in);
    declare_definitions(&declared, "echo_service_definition", 2);
    CHECK(load_definitions(&in, &declared, &library, NULL) == WEIVEL_C_STATUS_OK && library != NULL);
    definition = (const echo_definition_view *)service_of(&in, "echo");
    CHECK(definition != NULL && (const void *)definition == answered_by("echo_service_definition"));
    CHECK((const void *)service_of(&in, "echo-other") == answered_by("echo_other_service_definition"));
    before = crossed;
    CHECK(definition->echo(definition->context, (const uint8_t *)"ping", 4, &output, &length) == 0);
    CHECK(length == 4 && memcmp(output, "ping", 4) == 0);
    definition->release(definition->context, output);
    CHECK(memcmp(&before, &crossed, sizeof before) == 0);
    CHECK(sink_count(&in) == 0);
    end_host_world(&in);
}

static void refused_naming_the_function(host_world *in, definitions *declared, const char *wrong_kind) {
    weivel_c_library *library;
    weivel_c_library_load_failure failure = WEIVEL_INIT(weivel_c_library_load_failure);
    CHECK(load_definitions(in, declared, &library, &failure) == WEIVEL_C_STATUS_MISUSE && library == NULL);
    if (wrong_kind == NULL) {
        CHECK(failure.wrong_kind == NULL);
    } else {
        check_text(failure.wrong_kind, wrong_kind, "the function the host does not load as declared");
    }
}

static void a_definition_beside_another_function_or_given_context_facts_is_the_callers_mistake(void) {
    host_world in;
    definitions declared;
    weivel_standard_library_function two[2];
    uint32_t closes = weivel_c_testing_closes_made;
    begin_host_world(&in);
    declare_definitions(&declared, "echo_service_definition", 1);
    two[0] = declared.functions[ECHO_DEFINITION];
    two[1] = declared.functions[ECHO_BYTES];
    declared.library.release = "echo_release";
    declared.services[ECHO_DEFINITION].functions = two;
    declared.services[ECHO_DEFINITION].function_count = 2;
    refused_naming_the_function(&in, &declared, "definition");
    two[0] = declared.functions[ECHO_BYTES];
    two[1] = declared.functions[ECHO_DEFINITION];
    refused_naming_the_function(&in, &declared, "definition");
    declare_definitions(&declared, "echo_service_definition", 1);
    declared.functions[ECHO_DEFINITION].context = WEIVEL_STANDARD_CONTEXT_HOME;
    refused_naming_the_function(&in, &declared, "definition");
    declare_bytes_then(&declared, "echo_service_definition");
    declared.library.release = NULL;
    refused_naming_the_function(&in, &declared, NULL);
    CHECK(weivel_c_testing_closes_made == closes);
    CHECK(service_of(&in, "echo") == NULL && service_of(&in, "echo-bytes") == NULL && sink_count(&in) == 0);
    end_host_world(&in);
}

static void refused_as_a_definition(host_world *in, const char *symbol) {
    definitions declared;
    weivel_c_library *library;
    weivel_c_library_load_failure failure = WEIVEL_INIT(weivel_c_library_load_failure);
    uint32_t closes = weivel_c_testing_closes_made;
    declare_bytes_then(&declared, symbol);
    CHECK(load_definitions(in, &declared, &library, &failure) == WEIVEL_C_STATUS_MISUSE && library == NULL);
    check_text(failure.refused_definition, symbol, "the definition refused");
    CHECK(failure.wrong_kind == NULL && failure.missing_symbol == NULL);
    CHECK(weivel_c_testing_closes_made == closes + 1);
    CHECK(service_of(in, "echo-bytes") == NULL && service_of(in, "echo") == NULL);
}

static void a_definition_answering_null_or_a_struct_below_its_floor_is_refused_naming_its_symbol(void) {
    host_world in;
    definitions declared;
    weivel_c_library *library;
    weivel_c_library_load_failure failure = WEIVEL_INIT(weivel_c_library_load_failure);
    begin_host_world(&in);
    refused_as_a_definition(&in, "echo_null_definition");
    refused_as_a_definition(&in, "echo_one_byte_short_definition");
    declare_definitions(&declared, "echo_floor_definition", 1);
    CHECK(load_definitions(&in, &declared, &library, &failure) == WEIVEL_C_STATUS_OK);
    CHECK(failure.refused_definition == NULL && service_of(&in, "echo") != NULL);
    CHECK(sink_count(&in) == 0);
    end_host_world(&in);
}

/** What one plugin of the awaiting test heard: the handles of its revocations, and of its own release events. */
typedef struct awaiting_plugin {
    test_plugin plugin;
    weivel_handle revoked;
    uint32_t revocations;
    weivel_handle released;
    uint32_t releases;
    weivel_handle registration;
    void *resolved;
} awaiting_plugin;

static int notes_object;

static weivel_c_status hear_awaiting(void *data, const weivel_c_plugin_event *event) {
    awaiting_plugin *self = data;
    if (event->kind == WEIVEL_C_PLUGIN_EVENT_REVOKED) {
        self->revoked = event->handle;
        self->revocations++;
    } else if (event->kind == WEIVEL_C_PLUGIN_EVENT_HOLDINGS_RELEASED) {
        CHECK(event->id == NULL && event->object == NULL);
        self->released = event->handle;
        self->releases++;
    }
    return WEIVEL_C_STATUS_OK;
}

static void listen(awaiting_plugin *self, weivel_c_context *context) {
    weivel_c_plugin_listener listener = {hear_awaiting, NULL};
    uint64_t registration = 0;
    listener.data = self;
    CHECK(weivel_c_context_add_listener(context, &listener, &registration) == WEIVEL_C_STATUS_OK);
}

static weivel_c_status hear_registered(void *data, const weivel_c_service_change *change) {
    awaiting_plugin *self = data;
    if (change->change == WEIVEL_CHANGE_REGISTERED) {
        self->resolved = change->service;
    }
    return WEIVEL_C_STATUS_OK;
}

static uint32_t activate_watching(void *data, weivel_c_context *context, weivel_c_completion *completion,
                                  const char **reason) {
    awaiting_plugin *self = data;
    weivel_c_service_watcher watcher = {hear_registered, NULL};
    (void)completion;
    (void)reason;
    watcher.data = self;
    self->plugin.context = context;
    listen(self, context);
    CHECK(weivel_c_context_watch_service(context, "owner:notes", &watcher, NULL, NULL) == WEIVEL_C_STATUS_OK);
    return WEIVEL_C_ENTRY_DONE;
}

static uint32_t activate_registering_awaiting(void *data, weivel_c_context *context, weivel_c_completion *completion,
                                              const char **reason) {
    awaiting_plugin *self = data;
    (void)completion;
    (void)reason;
    self->plugin.context = context;
    listen(self, context);
    CHECK(weivel_c_context_register_service_awaiting_releases(context, "owner:notes", &notes_object, NULL,
                                                              &self->registration, NULL) == WEIVEL_C_STATUS_OK);
    return WEIVEL_C_ENTRY_DONE;
}

static uint32_t activate_getting(void *data, weivel_c_context *context, weivel_c_completion *completion,
                                 const char **reason) {
    awaiting_plugin *self = data;
    (void)completion;
    (void)reason;
    self->plugin.context = context;
    listen(self, context);
    CHECK(weivel_c_context_get_service(context, "owner:notes", &self->resolved, NULL) == WEIVEL_C_STATUS_OK);
    return WEIVEL_C_ENTRY_DONE;
}

static void start_awaiting(host_world *in, awaiting_plugin *self, const char *name, weivel_c_entry_function activate,
                           int offers) {
    memset(self, 0, sizeof *self);
    name_test_plugin(&self->plugin, in, name);
    if (offers) {
        offers_service(&self->plugin, "owner:notes");
    } else {
        wants_service(&self->plugin, "owner:notes");
    }
    self->plugin.entry.activate = activate;
    self->plugin.entry.data = self;
    register_test_plugin(&self->plugin);
}

static void an_owner_hears_its_release_event_once_every_holding_a_watchs_among_them_is_released(void) {
    host_world in;
    awaiting_plugin watcher, owner, reader;
    test_plugin *plan[3];
    weivel_c_refusal refusal = WEIVEL_INIT(weivel_c_refusal);
    begin_host_world(&in);
    start_awaiting(&in, &watcher, "watcher", activate_watching, 0);
    start_awaiting(&in, &owner, "owner", activate_registering_awaiting, 1);
    start_awaiting(&in, &reader, "reader", activate_getting, 0);
    plan[0] = &watcher.plugin;
    plan[1] = &owner.plugin;
    plan[2] = &reader.plugin;
    CHECK(activate_in_order(&in, plan, 3, WEIVEL_EDGES_CAPABILITIES));
    CHECK(watcher.resolved == &notes_object && reader.resolved == &notes_object);
    CHECK(weivel_c_context_release_holding(reader.plugin.context, owner.registration, &refusal) ==
          WEIVEL_C_STATUS_REFUSED);
    CHECK(refusal.code == WEIVEL_REFUSAL_STALE_HANDLE);
    weivel_c_refusal_release(&refusal);
    CHECK(weivel_c_context_unregister_service(owner.plugin.context, owner.registration, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(watcher.revocations == 1 && watcher.revoked == owner.registration);
    CHECK(reader.revocations == 1 && reader.revoked == owner.registration && owner.releases == 0);
    CHECK(weivel_c_context_release_holding(reader.plugin.context, reader.revoked, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(owner.releases == 0);
    CHECK(weivel_c_context_release_holding(reader.plugin.context, reader.revoked, NULL) == WEIVEL_C_STATUS_REFUSED);
    CHECK(weivel_c_context_release_holding(watcher.plugin.context, watcher.revoked, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(owner.releases == 1 && owner.released == owner.registration);
    CHECK(watcher.releases == 0 && reader.releases == 0);
    CHECK(sink_count(&in) == 0);
    end_host_world(&in);
}

static void a_load_undone_after_a_definition_registered_closes_once_its_release_is_heard(void) {
    host_world in;
    definitions declared;
    weivel_c_library *library;
    uint32_t closes = weivel_c_testing_closes_made;
    begin_host_world(&in);
    declare_definitions(&declared, "echo_service_definition", 2);
    weivel_c_testing_registration_to_refuse = 2;
    CHECK(load_definitions(&in, &declared, &library, NULL) == WEIVEL_C_STATUS_OUT_OF_MEMORY && library == NULL);
    CHECK(weivel_c_testing_closes_made == closes + 1);
    CHECK(service_of(&in, "echo") == NULL && service_of(&in, "echo-other") == NULL && sink_count(&in) == 0);
    end_host_world(&in);
}

static uint32_t activate_getting_echo(void *data, weivel_c_context *context, weivel_c_completion *completion,
                                      const char **reason) {
    test_plugin *self = data;
    void *service = NULL;
    (void)completion;
    (void)reason;
    self->context = context;
    CHECK(weivel_c_context_get_service(context, "echo", &service, NULL) == WEIVEL_C_STATUS_OK);
    return WEIVEL_C_ENTRY_DONE;
}

static void a_holder_the_destroy_releases_ends_its_holding_and_closes_the_binary(void) {
    host_world in;
    definitions declared;
    weivel_c_library *library;
    test_plugin holder;
    test_plugin *plan[1];
    uint32_t finished = 0;
    uint32_t closes;
    begin_host_world(&in);
    declare_definitions(&declared, "echo_service_definition", 1);
    CHECK(load_definitions(&in, &declared, &library, NULL) == WEIVEL_C_STATUS_OK);
    name_test_plugin(&holder, &in, "holder");
    wants_service(&holder, "echo");
    holder.entry.activate = activate_getting_echo;
    register_test_plugin(&holder);
    plan[0] = &holder;
    CHECK(activate_in_order(&in, plan, 1, WEIVEL_EDGES_CAPABILITIES));
    closes = weivel_c_testing_closes_made;
    CHECK(weivel_c_host_unload_library(in.host, library) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_testing_closes_made == closes);
    CHECK(weivel_c_host_destroy(in.host, &finished) == WEIVEL_C_STATUS_OK && finished == 1);
    CHECK(weivel_c_testing_closes_made == closes + 1);
    CHECK(sink_count(&in) == 0);
    CHECK(weivel_c_host_free(in.host) == WEIVEL_C_STATUS_OK);
}

static void a_holder_that_never_lets_go_keeps_the_binary_open_past_the_destroy_until_the_free(void) {
    host_world in;
    definitions declared;
    weivel_c_library *library;
    uint32_t finished = 0;
    uint32_t closes;
    begin_host_world(&in);
    declare_definitions(&declared, "echo_service_definition", 1);
    CHECK(load_definitions(&in, &declared, &library, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(service_of(&in, "echo") != NULL);
    closes = weivel_c_testing_closes_made;
    CHECK(weivel_c_host_unload_library(in.host, library) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_host_destroy(in.host, &finished) == WEIVEL_C_STATUS_OK && finished == 1);
    CHECK(weivel_c_testing_closes_made == closes);
    CHECK(weivel_c_host_free(in.host) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_testing_closes_made == closes + 1);
}

static void a_library_whose_definition_is_still_registered_at_the_destroy_closes_at_the_free(void) {
    host_world in;
    definitions declared;
    weivel_c_library *library;
    test_plugin holder;
    test_plugin *plan[1];
    uint32_t finished = 0;
    uint32_t closes;
    begin_host_world(&in);
    declare_definitions(&declared, "echo_service_definition", 1);
    CHECK(load_definitions(&in, &declared, &library, NULL) == WEIVEL_C_STATUS_OK);
    name_test_plugin(&holder, &in, "holder");
    wants_service(&holder, "echo");
    holder.entry.activate = activate_getting_echo;
    register_test_plugin(&holder);
    plan[0] = &holder;
    CHECK(activate_in_order(&in, plan, 1, WEIVEL_EDGES_CAPABILITIES));
    closes = weivel_c_testing_closes_made;
    CHECK(weivel_c_host_destroy(in.host, &finished) == WEIVEL_C_STATUS_OK && finished == 1);
    CHECK(weivel_c_testing_closes_made == closes);
    CHECK(weivel_c_host_free(in.host) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_testing_closes_made == closes + 1);
}

int main(int argument_count, char **arguments) {
    if (argument_count != 3 || !load_tables(arguments[1])) {
        fprintf(stderr, "usage: host_definition_test <core-library> <echo-library>\n");
        return 2;
    }
    library_path_of_echo = arguments[2];
    a_library_of_definitions_loads_without_a_release_and_registers_each_symbols_own_pointer();
    a_definition_beside_another_function_or_given_context_facts_is_the_callers_mistake();
    a_definition_answering_null_or_a_struct_below_its_floor_is_refused_naming_its_symbol();
    an_owner_hears_its_release_event_once_every_holding_a_watchs_among_them_is_released();
    a_load_undone_after_a_definition_registered_closes_once_its_release_is_heard();
    a_holder_the_destroy_releases_ends_its_holding_and_closes_the_binary();
    a_holder_that_never_lets_go_keeps_the_binary_open_past_the_destroy_until_the_free();
    a_library_whose_definition_is_still_registered_at_the_destroy_closes_at_the_free();
    CHECK(weivel_c_core_unload(&real_table) == WEIVEL_C_STATUS_OK);
    return check_summary("host definition");
}
