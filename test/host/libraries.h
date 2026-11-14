#ifndef WEIVEL_C_TEST_HOST_LIBRARIES_H
#define WEIVEL_C_TEST_HOST_LIBRARIES_H

/**
 * @file
 * What the tests of C libraries as host services share: declarations of the test service library and of the core's
 * echo library built as a caller builds one, and the counters the testing build of the layer keeps of the binaries it
 * closes.
 */

#include <weivel/c/host/library.h>

#include "plugins.h"
#include "service_library.h"

/** The testing build's: how many of the next closes of a library's binary are refused as the platform would. */
extern uint32_t weivel_c_testing_closes_to_refuse;
/** The testing build's: how many closes of a library's binary were made, refused ones included. */
extern uint32_t weivel_c_testing_closes_made;
/** The testing build's: the library of each of the first 16 closes made, in order. */
extern const weivel_c_library *weivel_c_testing_closed_libraries[16];
/** The testing build's: when not 0, how many registrations of a library's services from now is refused, 1 the next. */
extern uint32_t weivel_c_testing_registration_to_refuse;

enum { PROBE_INPUT_ADDRESS, PROBE_OWN_ADDRESS, PROBE_ATTACH, PROBE_ANSWER, PROBE_CALL_BACK, PROBE_FUNCTIONS };
enum { ECHO_ECHO, ECHO_FAIL, ECHO_GARBAGE, ECHO_FUNCTIONS };

/** A declaration as a caller builds one, every text a literal of the test's. */
typedef struct declared {
    weivel_standard_library_function functions[PROBE_FUNCTIONS];
    weivel_standard_library_service service;
    weivel_standard_library library;
} declared;

static inline void declare_function(declared *self, uint32_t index, const char *name, const char *symbol) {
    self->functions[index].name = name;
    self->functions[index].symbol = symbol;
    self->functions[index].kind = WEIVEL_STANDARD_FUNCTION_BYTES;
    self->functions[index].context = 0;
}

static inline void declare_service(declared *self, const char *id, uint32_t count, const char *release) {
    self->service.id = id;
    self->service.functions = self->functions;
    self->service.function_count = count;
    self->library = (weivel_standard_library)WEIVEL_INIT(weivel_standard_library);
    self->library.import_name = id;
    self->library.version = "1.0.0";
    self->library.release = release;
    self->library.services = &self->service;
    self->library.service_count = 1;
}

static inline void declare_probe(declared *self, const char *id) {
    memset(self, 0, sizeof *self);
    declare_function(self, PROBE_INPUT_ADDRESS, "input_address", "service_input_address");
    declare_function(self, PROBE_OWN_ADDRESS, "own_address", "service_own_address");
    declare_function(self, PROBE_ATTACH, "attach", "service_attach");
    declare_function(self, PROBE_ANSWER, "answer", "service_answer");
    declare_function(self, PROBE_CALL_BACK, "call_back", "service_call_back");
    declare_service(self, id, PROBE_FUNCTIONS, "service_release");
}

static inline void declare_echo(declared *self) {
    memset(self, 0, sizeof *self);
    declare_function(self, ECHO_ECHO, "echo", "echo_echo");
    declare_function(self, ECHO_FAIL, "fail", "echo_fail");
    declare_function(self, ECHO_GARBAGE, "garbage", "echo_garbage");
    declare_service(self, "echo", ECHO_FUNCTIONS, "echo_release");
}

static inline weivel_c_library *load_declared(host_world *in, const char *path, declared *declaration) {
    weivel_c_library *library = NULL;
    CHECK(weivel_c_host_load_library(in->host, path, &declaration->library, &library, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    return library;
}

/** The service registered under `id`, as the host's own context gets it; NULL when none is. */
static inline const weivel_c_library_service *service_of(host_world *in, const char *id) {
    weivel_c_context *context = NULL;
    void *service = NULL;
    CHECK(weivel_c_host_context(in->host, &context) == WEIVEL_C_STATUS_OK);
    if (weivel_c_context_get_service(context, id, &service, NULL) != WEIVEL_C_STATUS_OK) {
        return NULL;
    }
    return service;
}

static inline weivel_c_status call_with(const weivel_c_library_service *service, uint32_t function, const void *input,
                                        size_t length, weivel_c_library_answer *answer) {
    *answer = (weivel_c_library_answer)WEIVEL_INIT(weivel_c_library_answer);
    answer->body = (weivel_c_bytes)WEIVEL_INIT(weivel_c_bytes);
    return service->call(service, function, input, length, answer);
}

/** Attaches `counter` to the probe library, so each release of a body it answered increments it. */
static inline void attach_counter(const weivel_c_library_service *probe, uint32_t *counter) {
    weivel_c_library_answer answer;
    CHECK(call_with(probe, PROBE_ATTACH, &counter, sizeof counter, &answer) == WEIVEL_C_STATUS_OK);
    CHECK(answer.body.data == NULL && answer.body.owner == NULL);
}

static inline int body_is(const weivel_c_bytes *body, const char *text) {
    size_t length = strlen(text);
    return body->length == length && (length == 0 || memcmp(body->data, text, length) == 0);
}

#endif
