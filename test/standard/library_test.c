/**
 * @file
 * A library declaration read by the standard is exactly what the host loads C libraries as host services from: the
 * standard's reader answers it, the host loads it with the path the caller gives, since a declaration names none, and
 * the caller releases the declaration right after, the host having copied every text it keeps. A declaration of a
 * kind the standard does not know never reaches the host: the reader declines it.
 */

#include <weivel/c/standard.h>
#include <weivel/library.h>

#include "host/libraries.h"

static const char DECLARATION[] =
    "{\"importName\": \"probe\", \"version\": \"1.0.0\", \"release\": \"service_release\", \"services\": [{\"id\": "
    "\"probe\", \"functions\": [{\"name\": \"answer\", \"symbol\": \"service_answer\", \"kind\": \"bytes\"}]}]}";

static const char UNKNOWN_KIND[] =
    "{\"importName\": \"probe\", \"version\": \"1.0.0\", \"release\": \"service_release\", \"services\": [{\"id\": "
    "\"probe\", \"functions\": [{\"name\": \"answer\", \"symbol\": \"service_answer\", \"kind\": \"json\"}]}]}";

static const char *library_path_of_probe;

static void a_declaration_the_standard_read_is_loaded_and_released_right_after(void) {
    host_world in;
    weivel_standard_library declaration = WEIVEL_INIT(weivel_standard_library);
    weivel_c_library *library = NULL;
    const weivel_c_library_service *service;
    weivel_c_library_answer answer;
    const uint8_t input[] = {0, 'o', 'k'};
    begin_host_world(&in);
    CHECK(weivel_standard_read_library(DECLARATION, sizeof DECLARATION - 1, &declaration, NULL) == WEIVEL_STATUS_OK);
    CHECK(weivel_c_host_load_library(in.host, library_path_of_probe, &declaration, &library, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    weivel_standard_library_release(&declaration);
    service = service_of(&in, "probe");
    CHECK(service != NULL && service->function_count == 1);
    if (service != NULL) {
        check_text(service->id, "probe", "the service the declaration names");
        check_text(service->function_names[0], "answer", "the function the declaration names");
        CHECK(call_with(service, 0, input, sizeof input, &answer) == WEIVEL_C_STATUS_OK && body_is(&answer.body, "ok"));
        CHECK(weivel_c_bytes_release(&answer.body) == WEIVEL_C_STATUS_OK);
    }
    CHECK(sink_count(&in) == 0);
    end_host_world(&in);
}

static void a_declaration_of_a_kind_the_standard_does_not_know_never_reaches_the_host(void) {
    weivel_standard_library declaration = WEIVEL_INIT(weivel_standard_library);
    weivel_standard_refusal refusal = WEIVEL_INIT(weivel_standard_refusal);
    CHECK(weivel_c_standard_status(weivel_standard_read_library(UNKNOWN_KIND, sizeof UNKNOWN_KIND - 1, &declaration,
                                                                &refusal)) ==
          WEIVEL_C_STATUS_REFUSED_BY_THE_STANDARD);
    CHECK(declaration.services == NULL && declaration.service_count == 0);
    check_text(refusal.path, "services[0].functions[0].kind", "the field the reader declined");
    weivel_standard_refusal_release(&refusal);
}

int main(int argument_count, char **arguments) {
    if (argument_count != 3 || !load_tables(arguments[1])) {
        fprintf(stderr, "usage: standard_library_test <core-library> <service-library>\n");
        return 2;
    }
    library_path_of_probe = arguments[2];
    a_declaration_the_standard_read_is_loaded_and_released_right_after();
    a_declaration_of_a_kind_the_standard_does_not_know_never_reaches_the_host();
    CHECK(weivel_c_core_unload(&real_table) == WEIVEL_C_STATUS_OK);
    return check_summary("standard library");
}
