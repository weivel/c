/**
 * @file
 * The standard's description check as a host's: a declaration the standard read, whose services declare their
 * descriptions, loads once the standard's check passes each of them, and one whose description the check refuses is
 * refused with nothing registered, the refusal naming the fault's path. C binds nothing of a description, so one whose
 * offsets disagree with the library's layout, which the check cannot see, still loads here; only a layer that lays the
 * structs out itself refuses it.
 */

#include <weivel/c/standard.h>
#include <weivel/library.h>

#include "host/libraries.h"

#define DECLARATION_TEXT 1024

static const char DECLARATION[] =
    "{\"importName\": \"described\", \"version\": \"1.0.0\", \"services\": ["
    "{\"id\": \"described-mirror\", \"description\": \"described_mirror_description\", \"functions\": "
    "[{\"name\": \"definition\", \"symbol\": \"described_mirror_definition\", \"kind\": \"definition\"}]}, "
    "{\"id\": \"described-walker\", \"description\": \"%s\", \"functions\": "
    "[{\"name\": \"definition\", \"symbol\": \"described_walker_definition\", \"kind\": \"definition\"}]}]}";

static const char *library_path_of_described;

static void begin_checking_world(host_world *in, weivel_standard_refusal *refusal) {
    weivel_c_scheduler scheduler;
    weivel_c_host_options options;
    start_host_world(in);
    options = host_options_of(in, &counting_table, &scheduler);
    options.description_check.check = weivel_c_standard_check_description;
    options.description_check.data = refusal;
    CHECK(weivel_c_host_create(&options, &in->host, NULL) == WEIVEL_C_STATUS_OK);
}

/**
 * Reads the described library's declaration with its walker described by `walker`, and loads it, holding the
 * description the load refused, a text of the declaration's own, to `refused` before the declaration is released.
 */
static weivel_c_status load_described(host_world *in, const char *walker, const char *refused) {
    char text[DECLARATION_TEXT];
    weivel_standard_library declaration = WEIVEL_INIT(weivel_standard_library);
    weivel_c_library_load_failure failure = WEIVEL_INIT(weivel_c_library_load_failure);
    weivel_c_library *library = NULL;
    weivel_c_status status;
    int length = snprintf(text, sizeof text, DECLARATION, walker);
    CHECK(length > 0 && (size_t)length < sizeof text);
    CHECK(weivel_standard_read_library(text, (size_t)length, &declaration, NULL) == WEIVEL_STATUS_OK);
    status = weivel_c_host_load_library(in->host, library_path_of_described, &declaration, &library, &failure, NULL);
    if (refused == NULL) {
        CHECK(failure.refused_description == NULL);
    } else {
        check_text(failure.refused_description, refused, "the description refused");
    }
    weivel_standard_library_release(&declaration);
    return status;
}

static int is_registered(host_world *in, const char *id) {
    return service_of(in, id) != NULL;
}

static void a_library_whose_descriptions_the_standard_passes_loads(void) {
    host_world in;
    weivel_standard_refusal refusal = WEIVEL_INIT(weivel_standard_refusal);
    begin_checking_world(&in, &refusal);
    CHECK(load_described(&in, "described_walker_description", NULL) == WEIVEL_C_STATUS_OK);
    CHECK(is_registered(&in, "described-mirror") && is_registered(&in, "described-walker"));
    CHECK(refusal.storage == NULL && refusal.path == NULL);
    CHECK(sink_count(&in) == 0);
    end_host_world(&in);
}

static void a_description_the_standard_refuses_refuses_the_load_naming_its_fault(void) {
    host_world in;
    weivel_standard_refusal refusal = WEIVEL_INIT(weivel_standard_refusal);
    begin_checking_world(&in, &refusal);
    CHECK(load_described(&in, "described_broken_count_description", "described_broken_count_description") ==
          WEIVEL_C_STATUS_MISUSE);
    check_text(refusal.path, "types[1].members[3].count", "the fault the standard's check found");
    CHECK(!is_registered(&in, "described-mirror") && !is_registered(&in, "described-walker"));
    weivel_standard_refusal_release(&refusal);
    CHECK(sink_count(&in) == 0);
    end_host_world(&in);
}

static void a_description_whose_offsets_alone_are_wrong_loads_in_c(void) {
    host_world in;
    begin_checking_world(&in, NULL);
    CHECK(load_described(&in, "described_broken_offset_description", NULL) == WEIVEL_C_STATUS_OK);
    CHECK(is_registered(&in, "described-walker"));
    end_host_world(&in);
}

static void the_check_answers_refused_by_the_standard_for_no_description(void) {
    CHECK(weivel_c_standard_check_description(NULL, NULL) == WEIVEL_C_STATUS_REFUSED_BY_THE_STANDARD);
}

int main(int argument_count, char **arguments) {
    if (argument_count != 3 || !load_tables(arguments[1])) {
        fprintf(stderr, "usage: standard_description_test <core-library> <described-library>\n");
        return 2;
    }
    library_path_of_described = arguments[2];
    a_library_whose_descriptions_the_standard_passes_loads();
    a_description_the_standard_refuses_refuses_the_load_naming_its_fault();
    a_description_whose_offsets_alone_are_wrong_loads_in_c();
    the_check_answers_refused_by_the_standard_for_no_description();
    CHECK(weivel_c_core_unload(&real_table) == WEIVEL_C_STATUS_OK);
    return check_summary("standard description");
}
