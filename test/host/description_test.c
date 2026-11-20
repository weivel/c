/**
 * @file
 * Descriptions a C library declares for its definitions: the host calls each description's symbol once and hands
 * what it answered to the check its options give, binding nothing of it, and registers each definition as the very
 * pointer its symbol answered only once every description was accepted. A description that is missing, answers NULL
 * or is refused, or one the host was given no check for, refuses the load with nothing registered, the refused one's
 * symbol named; a check that runs out of memory answers that; and a declaration that ends before its descriptions
 * loads as one that declares none. The check here is the test's own, since the host never links the standard whose
 * check a caller would give it.
 */

#include "libraries.h"

#include "../../src/core/binary.h"

static const char *library_path_of_described;
static const char *library_path_of_echo;

enum { MIRROR, WALKER, DESCRIBED_SERVICES };

#define FIRST_DECLARATION_SIZE (offsetof(weivel_standard_library, storage) + sizeof(void *))

/** The described library declared as a caller builds it, each definition with its description. */
typedef struct described {
    weivel_standard_library_function functions[DESCRIBED_SERVICES];
    weivel_standard_library_service services[DESCRIBED_SERVICES];
    const char *descriptions[DESCRIBED_SERVICES];
    weivel_standard_library library;
} described;

static void declare_definition(described *self, uint32_t index, const char *id, const char *symbol,
                               const char *description) {
    self->functions[index].name = "definition";
    self->functions[index].symbol = symbol;
    self->functions[index].kind = WEIVEL_STANDARD_FUNCTION_DEFINITION;
    self->functions[index].context = 0;
    self->services[index].id = id;
    self->services[index].functions = &self->functions[index];
    self->services[index].function_count = 1;
    self->descriptions[index] = description;
}

static void declare_described(described *self, const char *walker_description) {
    memset(self, 0, sizeof *self);
    declare_definition(self, MIRROR, "described-mirror", "described_mirror_definition",
                       "described_mirror_description");
    declare_definition(self, WALKER, "described-walker", "described_walker_definition", walker_description);
    self->library = (weivel_standard_library)WEIVEL_INIT(weivel_standard_library);
    self->library.import_name = "described";
    self->library.version = "1.0.0";
    self->library.services = self->services;
    self->library.service_count = DESCRIBED_SERVICES;
    self->library.descriptions = self->descriptions;
}

/** What the test's check saw, and what it answers. */
typedef struct checking {
    const void *seen[4];
    uint32_t calls;
    /** The call, counted from 1, the check answers `refusal` for; 0 to accept every one. */
    uint32_t refuse_call;
    weivel_c_status refusal;
} checking;

static weivel_c_status check_by_the_test(void *data, const struct weivel_standard_description *description) {
    checking *self = data;
    if (self->calls < 4) {
        self->seen[self->calls] = description;
    }
    self->calls++;
    return self->calls == self->refuse_call ? self->refusal : WEIVEL_C_STATUS_OK;
}

static void begin_checking_world(host_world *in, checking *check) {
    weivel_c_scheduler scheduler;
    weivel_c_host_options options;
    start_host_world(in);
    options = host_options_of(in, &counting_table, &scheduler);
    if (check != NULL) {
        memset(check, 0, sizeof *check);
        options.description_check.check = check_by_the_test;
        options.description_check.data = check;
    }
    CHECK(weivel_c_host_create(&options, &in->host, NULL) == WEIVEL_C_STATUS_OK);
}

static weivel_c_status load(host_world *in, const char *path, weivel_standard_library *declaration,
                            weivel_c_library_load_failure *failure) {
    weivel_c_library *library = NULL;
    weivel_c_status status = weivel_c_host_load_library(in->host, path, declaration, &library, failure, NULL);
    CHECK((status == WEIVEL_C_STATUS_OK) == (library != NULL));
    return status;
}

/** What a symbol of the described library answers, read through the platform's loader while the host holds it. */
static const void *answered_by(const char *symbol) {
    void *binary = NULL;
    const void *answered = NULL;
    weivel_c_binary_function found;
    CHECK(weivel_c_binary_open(library_path_of_described, &binary) == WEIVEL_C_STATUS_OK);
    found = weivel_c_binary_find(binary, symbol);
    CHECK(found != NULL);
    if (found != NULL) {
        answered = ((const void *(*)(void))found)();
    }
    weivel_c_binary_close(binary);
    return answered;
}

static void nothing_registered(host_world *in) {
    CHECK(service_of(in, "described-mirror") == NULL && service_of(in, "described-walker") == NULL);
}

static void each_description_is_checked_and_each_definition_registered_as_its_own_pointer(void) {
    host_world in;
    checking check;
    described declared;
    weivel_c_library_load_failure failure = WEIVEL_INIT(weivel_c_library_load_failure);
    begin_checking_world(&in, &check);
    declare_described(&declared, "described_walker_description");
    CHECK(load(&in, library_path_of_described, &declared.library, &failure) == WEIVEL_C_STATUS_OK);
    CHECK(failure.refused_description == NULL);
    CHECK(check.calls == 2);
    CHECK(check.seen[0] == answered_by("described_mirror_description"));
    CHECK(check.seen[1] == answered_by("described_walker_description"));
    CHECK((const void *)service_of(&in, "described-mirror") == answered_by("described_mirror_definition"));
    CHECK((const void *)service_of(&in, "described-walker") == answered_by("described_walker_definition"));
    CHECK(sink_count(&in) == 0);
    end_host_world(&in);
}

static void a_host_given_no_check_refuses_a_described_library_before_opening_it(void) {
    host_world in;
    described declared;
    weivel_c_library_load_failure failure = WEIVEL_INIT(weivel_c_library_load_failure);
    uint32_t closes = weivel_c_testing_closes_made;
    begin_checking_world(&in, NULL);
    declare_described(&declared, "described_walker_description");
    CHECK(load(&in, library_path_of_described, &declared.library, &failure) == WEIVEL_C_STATUS_MISUSE);
    check_text(failure.refused_description, "described_mirror_description", "the description refused");
    CHECK(weivel_c_testing_closes_made == closes);
    nothing_registered(&in);
    CHECK(sink_count(&in) == 0);
    end_host_world(&in);
}

static void refused_naming(host_world *in, described *declared, weivel_c_status expected, const char *refused) {
    weivel_c_library_load_failure failure = WEIVEL_INIT(weivel_c_library_load_failure);
    uint32_t closes = weivel_c_testing_closes_made;
    CHECK(load(in, library_path_of_described, &declared->library, &failure) == expected);
    if (refused == NULL) {
        CHECK(failure.refused_description == NULL);
    } else {
        check_text(failure.refused_description, refused, "the description refused");
    }
    CHECK(failure.refused_definition == NULL && failure.missing_symbol == NULL);
    CHECK(weivel_c_testing_closes_made == closes + 1);
    nothing_registered(in);
}

static void a_description_the_check_refuses_refuses_the_load_and_closes_the_binary(void) {
    host_world in;
    checking check;
    described declared;
    begin_checking_world(&in, &check);
    declare_described(&declared, "described_walker_description");
    check.refuse_call = 2;
    check.refusal = WEIVEL_C_STATUS_REFUSED_BY_THE_STANDARD;
    refused_naming(&in, &declared, WEIVEL_C_STATUS_MISUSE, "described_walker_description");
    CHECK(check.calls == 2);
    check.calls = 0;
    check.refuse_call = 1;
    check.refusal = WEIVEL_C_STATUS_FAILED;
    refused_naming(&in, &declared, WEIVEL_C_STATUS_MISUSE, "described_mirror_description");
    CHECK(check.calls == 1);
    CHECK(sink_count(&in) == 0);
    end_host_world(&in);
}

static void a_check_out_of_memory_answers_out_of_memory(void) {
    host_world in;
    checking check;
    described declared;
    begin_checking_world(&in, &check);
    declare_described(&declared, "described_walker_description");
    check.refuse_call = 1;
    check.refusal = WEIVEL_C_STATUS_OUT_OF_MEMORY;
    refused_naming(&in, &declared, WEIVEL_C_STATUS_OUT_OF_MEMORY, NULL);
    end_host_world(&in);
}

static void a_missing_description_is_a_missing_symbol_and_a_null_one_is_refused(void) {
    host_world in;
    checking check;
    described declared;
    weivel_c_library_load_failure failure = WEIVEL_INIT(weivel_c_library_load_failure);
    uint32_t closes;
    begin_checking_world(&in, &check);
    declare_described(&declared, "described_absent_description");
    closes = weivel_c_testing_closes_made;
    CHECK(load(&in, library_path_of_described, &declared.library, &failure) == WEIVEL_C_STATUS_MISSING_SYMBOL);
    check_text(failure.missing_symbol, "described_absent_description", "the missing symbol");
    CHECK(weivel_c_testing_closes_made == closes + 1);
    nothing_registered(&in);
    CHECK(check.calls == 1);
    check.calls = 0;
    declare_described(&declared, NULL);
    declared.library.service_count = 1;
    declared.services[MIRROR].id = "echo";
    declared.functions[MIRROR].symbol = "echo_service_definition";
    declared.descriptions[MIRROR] = "echo_null_definition";
    failure = (weivel_c_library_load_failure)WEIVEL_INIT(weivel_c_library_load_failure);
    closes = weivel_c_testing_closes_made;
    CHECK(load(&in, library_path_of_echo, &declared.library, &failure) == WEIVEL_C_STATUS_MISUSE);
    check_text(failure.refused_description, "echo_null_definition", "the description answering NULL");
    CHECK(weivel_c_testing_closes_made == closes + 1 && service_of(&in, "echo") == NULL);
    CHECK(check.calls == 0);
    end_host_world(&in);
}

static void a_description_empty_or_beside_bytes_is_refused_before_opening(void) {
    host_world in;
    checking check;
    described declared;
    weivel_c_library_load_failure failure = WEIVEL_INIT(weivel_c_library_load_failure);
    uint32_t closes = weivel_c_testing_closes_made;
    begin_checking_world(&in, &check);
    declare_described(&declared, "");
    CHECK(load(&in, library_path_of_described, &declared.library, &failure) == WEIVEL_C_STATUS_MISUSE);
    check_text(failure.refused_description, "", "the empty description");
    declare_described(&declared, "described_walker_description");
    declared.functions[WALKER].kind = WEIVEL_STANDARD_FUNCTION_BYTES;
    declared.library.release = "described_absent_release";
    failure = (weivel_c_library_load_failure)WEIVEL_INIT(weivel_c_library_load_failure);
    CHECK(load(&in, library_path_of_described, &declared.library, &failure) == WEIVEL_C_STATUS_MISUSE);
    check_text(failure.refused_description, "described_walker_description", "the description beside bytes");
    CHECK(weivel_c_testing_closes_made == closes && check.calls == 0);
    nothing_registered(&in);
    end_host_world(&in);
}

static void a_declaration_ending_before_its_descriptions_loads_as_one_declaring_none(void) {
    host_world in;
    described declared;
    begin_checking_world(&in, NULL);
    declare_described(&declared, "described_absent_description");
    declared.library.size = (uint32_t)FIRST_DECLARATION_SIZE;
    CHECK(load(&in, library_path_of_described, &declared.library, NULL) == WEIVEL_C_STATUS_OK);
    CHECK((const void *)service_of(&in, "described-walker") == answered_by("described_walker_definition"));
    CHECK(sink_count(&in) == 0);
    end_host_world(&in);
}

int main(int argument_count, char **arguments) {
    if (argument_count != 4 || !load_tables(arguments[1])) {
        fprintf(stderr, "usage: host_description_test <core-library> <described-library> <echo-library>\n");
        return 2;
    }
    library_path_of_described = arguments[2];
    library_path_of_echo = arguments[3];
    each_description_is_checked_and_each_definition_registered_as_its_own_pointer();
    a_host_given_no_check_refuses_a_described_library_before_opening_it();
    a_description_the_check_refuses_refuses_the_load_and_closes_the_binary();
    a_check_out_of_memory_answers_out_of_memory();
    a_missing_description_is_a_missing_symbol_and_a_null_one_is_refused();
    a_description_empty_or_beside_bytes_is_refused_before_opening();
    a_declaration_ending_before_its_descriptions_loads_as_one_declaring_none();
    CHECK(weivel_c_core_unload(&real_table) == WEIVEL_C_STATUS_OK);
    return check_summary("host description");
}
