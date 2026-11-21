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
const char *layer_library_path;
const char *layer_described_path;

/** Drives the vector; answers 1 when the run agrees with its `then`, printing both sides when it does not. */
static int witnessed(const char *id, weivel_c_conformance_witness *witness) {
    weivel_c_conformance_options options = WEIVEL_INIT(weivel_c_conformance_options);
    weivel_c_status status;
    options.core_path = layer_core_path;
    options.vector_directory = layer_vector_directory;
    options.library_path = layer_library_path;
    options.described_path = layer_described_path;
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
    {"lifecycle/destroy-releases-a-plugin-whose-activate-is-in-flight", witness_alone},
    {"lifecycle/events-of-an-activate-wait-for-every-activate-of-the-plan", witness_alone},
    {"lifecycle/an-activate-that-settles-at-once-is-verified-before-the-next-starts", witness_alone},
    {"lifecycle/an-activate-refused-at-once-is-quarantined-before-the-next-starts", witness_alone},
    {"library/a-definition-is-the-providers-own-pointer", witness_alone},
    {"library/a-null-definition-fails-the-load", witness_alone},
    {"library/a-definition-below-its-size-fails-the-load", witness_alone},
    {"library/a-definition-at-its-size-loads", witness_alone},
    {"library/an-unload-revokes-every-holding-of-a-definition", witness_alone},
    {"library/a-binary-stays-open-while-a-holding-lives", witness_alone},
    {"library/a-binary-closes-once-every-definition-service-is-released", witness_alone},
    {"library/a-binary-with-no-holding-closes-at-the-unload", witness_alone},
    {"library/a-holding-ended-with-its-holder-closes-the-binary", witness_alone},
    {"library/a-held-bytes-answer-keeps-the-binary-open-past-the-last-holding", witness_alone},
    {"library/an-owner-hears-the-release-its-own-release-queued", witness_alone},
    {"library/a-release-event-after-its-owner-was-quarantined-reaches-no-one", witness_alone},
    {"library/a-forgotten-holder-ends-its-late-holding", witness_alone},
    {"description/a-checked-description-registers-the-providers-own-pointer", witness_alone},
    {"description/a-broken-count-description-fails-the-load", witness_alone},
    {"description/a-definition-declared-without-its-description-loads-as-before", witness_alone},
    {"description/a-definition-described-without-its-nested-description-fails-the-load", witness_alone}};

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
