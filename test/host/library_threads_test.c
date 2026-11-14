/**
 * @file
 * Calls through a C library's service from several threads while another thread unloads the library: every call is
 * answered its body or refused as unloaded, never anything else, every body released is the library's to the end, and
 * the library's binary is closed exactly once, after the last call and the last release.
 *
 * @implNote What each caller counts is its own and read only after it is joined, so a call the layer made under
 * another thread's feet is a race the thread sanitizer names, and a call into a closed library is a crash.
 */

#include "libraries.h"

static const char *library_path_of_probe;

#define CALLERS 4
#define CALLS 2000

typedef struct crowd {
    host_world in;
    weivel_c_library *library;
    const weivel_c_library_service *service;
    test_thread callers[CALLERS];
    test_thread unloader;
    uint32_t answered[CALLERS];
    uint32_t refused[CALLERS];
    uint32_t other[CALLERS];
    weivel_c_status unloaded;
} crowd;

static crowd everyone;

static void count_one_call(uint32_t caller) {
    const uint8_t input[] = {0, 'x', 'y'};
    weivel_c_library_answer answer;
    weivel_c_status status = call_with(everyone.service, PROBE_ANSWER, input, sizeof input, &answer);
    if (status == WEIVEL_C_STATUS_OK && body_is(&answer.body, "xy") &&
        weivel_c_bytes_release(&answer.body) == WEIVEL_C_STATUS_OK) {
        everyone.answered[caller]++;
    } else if (status == WEIVEL_C_STATUS_UNLOADED && answer.body.owner == NULL) {
        everyone.refused[caller]++;
    } else {
        everyone.other[caller]++;
    }
}

static void call_again_and_again(void *argument) {
    uint32_t caller = (uint32_t)(size_t)argument;
    uint32_t call;
    for (call = 0; call < CALLS; call++) {
        count_one_call(caller);
    }
}

static void unload_after_a_while(void *argument) {
    uint32_t turn;
    (void)argument;
    for (turn = 0; turn < 64; turn++) {
        let_other_threads_run();
    }
    everyone.unloaded = weivel_c_host_unload_library(everyone.in.host, everyone.library);
}

static void calls_from_several_threads_while_another_unloads_are_each_answered_or_refused(void) {
    declared probe;
    uint32_t caller, closes, answered = 0, refused = 0, other = 0;
    begin_host_world(&everyone.in);
    declare_probe(&probe, "probe");
    everyone.library = load_declared(&everyone.in, library_path_of_probe, &probe);
    everyone.service = service_of(&everyone.in, "probe");
    closes = weivel_c_testing_closes_made;
    for (caller = 0; caller < CALLERS; caller++) {
        CHECK(start_test_thread(&everyone.callers[caller], call_again_and_again, (void *)(size_t)caller));
    }
    CHECK(start_test_thread(&everyone.unloader, unload_after_a_while, NULL));
    for (caller = 0; caller < CALLERS; caller++) {
        join_test_thread(&everyone.callers[caller]);
        answered += everyone.answered[caller];
        refused += everyone.refused[caller];
        other += everyone.other[caller];
    }
    join_test_thread(&everyone.unloader);
    CHECK(everyone.unloaded == WEIVEL_C_STATUS_OK && other == 0 && answered + refused == CALLERS * CALLS);
    CHECK(weivel_c_testing_closes_made == closes + 1);
    CHECK(sink_count(&everyone.in) == 0);
    end_host_world(&everyone.in);
    CHECK(weivel_c_testing_closes_made == closes + 1);
}

int main(int argument_count, char **arguments) {
    if (argument_count != 3 || !load_tables(arguments[1])) {
        fprintf(stderr, "usage: host_library_threads_test <core-library> <service-library>\n");
        return 2;
    }
    library_path_of_probe = arguments[2];
    calls_from_several_threads_while_another_unloads_are_each_answered_or_refused();
    CHECK(weivel_c_core_unload(&real_table) == WEIVEL_C_STATUS_OK);
    return check_summary("host library threads");
}
