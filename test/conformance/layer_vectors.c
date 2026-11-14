/**
 * @file
 * Each test reads its vector from the pinned kit at run time, drives it through the host's public surface on the real
 * core, and holds the run to the vector's `then` as a value, then to what LAYER.md states beyond it.
 */

#include "layer_vectors.h"

#include <weivel/c/conformance.h>

#include "check.h"

const char *layer_core_path;
const char *layer_vector_directory;

/** Drives the vector; answers 1 when the run agrees with its `then`, printing both sides when it does not. */
static int witnessed(const char *id, weivel_c_conformance_witness *witness) {
    weivel_c_conformance_options options = WEIVEL_INIT(weivel_c_conformance_options);
    weivel_c_status status;
    options.core_path = layer_core_path;
    options.vector_directory = layer_vector_directory;
    status = weivel_c_conformance_witness_vector(&options, id, witness);
    if (status == WEIVEL_C_STATUS_OK && witness->agrees) {
        return 1;
    }
    fprintf(stderr, "%s: not witnessed (status %u)\n  problem:  %s\n  expected: %s\n  observed: %s\n", id,
            (unsigned)status, witness->problem == NULL ? "none" : witness->problem,
            witness->expected == NULL ? "" : witness->expected, witness->observed == NULL ? "" : witness->observed);
    check_failures++;
    return 0;
}

static void witness_alone(const char *id) {
    weivel_c_conformance_witness witness = WEIVEL_INIT(weivel_c_conformance_witness);
    if (witnessed(id, &witness)) {
        CHECK(witness.sink_failures == 0 && witness.error_lines == 0);
    }
    weivel_c_conformance_witness_release(&witness);
}

/** The throwing listener's every call reaches the sink as a listener's failure, and nothing else does. */
static void witness_a_throwing_listener(const char *id) {
    weivel_c_conformance_witness witness = WEIVEL_INIT(weivel_c_conformance_witness);
    if (witnessed(id, &witness)) {
        CHECK(witness.listener_failures == 1 && witness.sink_failures == 1);
    }
    weivel_c_conformance_witness_release(&witness);
}

const layer_vector_test LAYER_VECTOR_TESTS[] = {
    {"consume/a-throwing-listener-does-not-silence-the-others", witness_a_throwing_listener},
    {"consume/a-refused-verify-quarantines-the-plugin", witness_alone},
    {"want/a-registration-at-the-deadline-loses", witness_alone},
    {"want/a-timed-want-arms-the-timer-at-its-deadline", witness_alone},
    {"want/a-settled-want-disarms-the-timer", witness_alone},
    {"want/a-registration-past-an-unfired-deadline-loses", witness_alone},
    {"lifecycle/an-awaited-want-does-not-hold-back-a-later-plugin", witness_alone},
    {"lifecycle/a-release-deactivates-then-releases-then-forgets", witness_alone},
    {"lifecycle/destroy-releases-a-plugin-whose-activate-is-in-flight", witness_alone}};

const uint32_t LAYER_VECTOR_TEST_COUNT = sizeof LAYER_VECTOR_TESTS / sizeof LAYER_VECTOR_TESTS[0];

int run_layer_vector_tests(void) {
    uint32_t index;
    for (index = 0; index < LAYER_VECTOR_TEST_COUNT; index++) {
        int failures_before = check_failures;
        LAYER_VECTOR_TESTS[index].run(LAYER_VECTOR_TESTS[index].id);
        printf("%s %s\n", check_failures == failures_before ? "witnessed" : "FAILED", LAYER_VECTOR_TESTS[index].id);
    }
    return check_summary("conformance layer vectors");
}
