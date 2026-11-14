/**
 * @file
 * Calls through a C library's service from several threads while another thread unloads the library, one of them a
 * call still running in the library across the unload: every call is answered its body or refused as unloaded, never
 * anything else, the running call returns from a library still open, and the library's binary is closed exactly once,
 * after the last call and the last release.
 *
 * @implNote What each caller counts is its own and read only after it is joined, so a call the layer made under
 * another thread's feet is a race the thread sanitizer names, and a call into a closed library is a crash.
 */

#include "gates.h"
#include "libraries.h"

static const char *library_path_of_probe;

#define CALLERS 4
#define MOST_CALLS 10000000

typedef struct crowd {
    host_world in;
    weivel_c_library *library;
    const weivel_c_library_service *service;
    test_thread callers[CALLERS];
    test_thread unloader;
    test_thread holder;
    test_mutex started_mutex;
    /** Guarded by `started_mutex`, as the two flags below. */
    uint32_t started;
    uint32_t holder_inside;
    uint32_t unload_returned;
    uint32_t closes_at_unload;
    weivel_c_status held_call;
    int held_body_read;
    uint32_t answered[CALLERS];
    uint32_t refused[CALLERS];
    uint32_t other[CALLERS];
    weivel_c_status unloaded;
} crowd;

static crowd everyone;

/** Makes one call and counts how it was answered; answers 1 while the caller should go on. */
static int count_one_call(uint32_t caller) {
    const uint8_t input[] = {0, 'x', 'y'};
    weivel_c_library_answer answer;
    weivel_c_status status = call_with(everyone.service, PROBE_ANSWER, input, sizeof input, &answer);
    if (status == WEIVEL_C_STATUS_OK && body_is(&answer.body, "xy") &&
        weivel_c_bytes_release(&answer.body) == WEIVEL_C_STATUS_OK) {
        everyone.answered[caller]++;
        return 1;
    }
    if (status == WEIVEL_C_STATUS_UNLOADED && answer.body.owner == NULL) {
        everyone.refused[caller]++;
    } else {
        everyone.other[caller]++;
    }
    return 0;
}

/** Calls until a call is refused, telling the unloader once the first call was answered. */
static void call_until_refused(void *argument) {
    uint32_t caller = (uint32_t)(size_t)argument;
    uint32_t call;
    int going_on = count_one_call(caller);
    lock_test_mutex(&everyone.started_mutex);
    everyone.started++;
    unlock_test_mutex(&everyone.started_mutex);
    for (call = 1; going_on && call < MOST_CALLS; call++) {
        going_on = count_one_call(caller);
    }
}

static uint32_t read_guarded(const uint32_t *flag) {
    uint32_t value;
    lock_test_mutex(&everyone.started_mutex);
    value = *flag;
    unlock_test_mutex(&everyone.started_mutex);
    return value;
}

static void set_guarded(uint32_t *flag) {
    lock_test_mutex(&everyone.started_mutex);
    *flag = 1;
    unlock_test_mutex(&everyone.started_mutex);
}

/** Runs inside the library's function, and returns only once the unload has returned. */
static void hold_until_unloaded(void *data) {
    (void)data;
    set_guarded(&everyone.holder_inside);
    while (!read_guarded(&everyone.unload_returned)) {
        let_other_threads_run();
    }
}

static void call_across_the_unload(void *argument) {
    service_library_callback callback = {hold_until_unloaded, NULL};
    const service_library_callback *pointer = &callback;
    weivel_c_library_answer answer;
    (void)argument;
    everyone.held_call = call_with(everyone.service, PROBE_CALL_BACK, &pointer, sizeof pointer, &answer);
    everyone.held_body_read = body_is(&answer.body, "done");
    weivel_c_bytes_release(&answer.body);
}

static void unload_while_every_caller_calls(void *argument) {
    (void)argument;
    while (read_guarded(&everyone.started) < CALLERS || !read_guarded(&everyone.holder_inside)) {
        let_other_threads_run();
    }
    everyone.unloaded = weivel_c_host_unload_library(everyone.in.host, everyone.library);
    everyone.closes_at_unload = weivel_c_testing_closes_made;
    set_guarded(&everyone.unload_returned);
}

static void calls_from_several_threads_while_another_unloads_are_each_answered_or_refused(void) {
    declared probe;
    uint32_t caller, closes, answered = 0, refused = 0, other = 0;
    begin_host_world(&everyone.in);
    declare_probe(&probe, "probe");
    everyone.library = load_declared(&everyone.in, library_path_of_probe, &probe);
    everyone.service = service_of(&everyone.in, "probe");
    closes = weivel_c_testing_closes_made;
    start_test_mutex(&everyone.started_mutex);
    for (caller = 0; caller < CALLERS; caller++) {
        CHECK(start_test_thread(&everyone.callers[caller], call_until_refused, (void *)(size_t)caller));
    }
    CHECK(start_test_thread(&everyone.holder, call_across_the_unload, NULL));
    CHECK(start_test_thread(&everyone.unloader, unload_while_every_caller_calls, NULL));
    for (caller = 0; caller < CALLERS; caller++) {
        join_test_thread(&everyone.callers[caller]);
        answered += everyone.answered[caller];
        refused += everyone.refused[caller];
        other += everyone.other[caller];
    }
    join_test_thread(&everyone.holder);
    join_test_thread(&everyone.unloader);
    CHECK(everyone.held_call == WEIVEL_C_STATUS_OK && everyone.held_body_read);
    CHECK(everyone.closes_at_unload == closes);
    CHECK(everyone.unloaded == WEIVEL_C_STATUS_OK && other == 0 && answered >= CALLERS && refused == CALLERS);
    CHECK(weivel_c_testing_closes_made == closes + 1);
    CHECK(sink_count(&everyone.in) == 0);
    end_host_world(&everyone.in);
    CHECK(weivel_c_testing_closes_made == closes + 1);
}

/**
 * A host registration of one id raced against a load declaring it: the registering thread is held still at the clock
 * reading its core call begins with, past the host's check for a live library declaring the id, while the load runs.
 */
typedef struct id_race {
    host_world in;
    test_mutex clock_mutex;
    /** Guarded by `clock_mutex`: 1 while the next reading of the clock is the one the registering thread stops at. */
    uint32_t stop_armed;
    test_gate reached;
    test_gate free_to_go;
    test_thread registering;
    int object;
    weivel_c_status registered;
} id_race;

static id_race racing_for_an_id;

static void stop_at_the_armed_reading(void *more) {
    id_race *self = more;
    uint32_t stopping;
    lock_test_mutex(&self->clock_mutex);
    stopping = self->stop_armed;
    self->stop_armed = 0;
    unlock_test_mutex(&self->clock_mutex);
    if (stopping) {
        open_gate(&self->reached);
        pass_gate(&self->free_to_go);
    }
}

static weivel_c_status settle_quietly(void *data, const weivel_c_settlement *settlement) {
    (void)data;
    (void)settlement;
    return WEIVEL_C_STATUS_OK;
}

/** Arms a distant deadline through the host's own context, so every later core call reads the clock first. */
static void arm_a_distant_deadline(host_world *in) {
    weivel_c_want_options options = WEIVEL_INIT(weivel_c_want_options);
    weivel_c_want_callback callback = {settle_quietly, NULL};
    weivel_c_context *own = NULL;
    options.timed = WEIVEL_CHOICE_YES;
    options.timeout_millis = 1000000000;
    CHECK(weivel_c_host_context(in->host, &own) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_want_service(own, "never:there", &options, &callback, NULL, NULL) == WEIVEL_C_STATUS_OK);
}

static void register_the_hosts_probe(void *argument) {
    id_race *self = argument;
    weivel_handle registration = 0;
    self->registered =
        weivel_c_host_register_service(self->in.host, "probe", &self->object, NULL, &registration, NULL);
}

static void begin_the_race_for_an_id(id_race *self) {
    memset(self, 0, sizeof *self);
    start_test_mutex(&self->clock_mutex);
    start_gate(&self->reached);
    start_gate(&self->free_to_go);
    begin_host_world(&self->in);
    arm_a_distant_deadline(&self->in);
    self->in.clock_react = stop_at_the_armed_reading;
    self->in.clock_more = self;
    lock_test_mutex(&self->clock_mutex);
    self->stop_armed = 1;
    unlock_test_mutex(&self->clock_mutex);
}

static void a_host_service_and_a_load_racing_for_one_id_never_both_stand(void) {
    id_race *self = &racing_for_an_id;
    weivel_c_library_load_failure failure = WEIVEL_INIT(weivel_c_library_load_failure);
    weivel_c_library *library = NULL;
    weivel_c_status loaded;
    declared probe;
    begin_the_race_for_an_id(self);
    declare_probe(&probe, "probe");
    CHECK(start_test_thread(&self->registering, register_the_hosts_probe, self));
    pass_gate(&self->reached);
    loaded = weivel_c_host_load_library(self->in.host, library_path_of_probe, &probe.library, &library, &failure, NULL);
    open_gate(&self->free_to_go);
    join_test_thread(&self->registering);
    CHECK(self->registered == WEIVEL_C_STATUS_OK);
    CHECK(loaded == WEIVEL_C_STATUS_MISUSE && library == NULL);
    check_text(failure.taken_id, "probe", "the id the host's registration claimed");
    CHECK((const void *)service_of(&self->in, "probe") == (const void *)&self->object);
    CHECK(sink_count(&self->in) == 0);
    end_host_world(&self->in);
}

int main(int argument_count, char **arguments) {
    if (argument_count != 3 || !load_tables(arguments[1])) {
        fprintf(stderr, "usage: host_library_threads_test <core-library> <service-library>\n");
        return 2;
    }
    library_path_of_probe = arguments[2];
    calls_from_several_threads_while_another_unloads_are_each_answered_or_refused();
    a_host_service_and_a_load_racing_for_one_id_never_both_stand();
    CHECK(weivel_c_core_unload(&real_table) == WEIVEL_C_STATUS_OK);
    return check_summary("host library threads");
}
