/**
 * @file
 * Calls on the host raced against each other at the one moment that matters, each thread held still at a gate: the
 * wrapper reads the clock outside every lock before each call while a deadline is armed, so a thread is stopped on its
 * way into the core call the test names by counting its readings.
 */

#include "gates.h"
#include "plugins.h"

/** The plugin every race starts from: active, a distant deadline armed, and a deactivate that waits. */
typedef struct race {
    host_world in;
    test_plugin early;
    test_plugin late;
    test_mutex clock_mutex;
    /** Guarded by `clock_mutex`: the readings left until the one the next armed thread stops at. */
    uint32_t readings_left;
    test_gate reached;
    test_gate free_to_go;
    test_gate dispatch_held;
    test_gate dispatch_free;
    test_thread runner;
    test_thread destroyer;
    test_thread dispatcher;
    weivel_c_status answered;
    weivel_handle registered;
    uint32_t destroy_finished;
} race;

static race racing;

static void stop_at_the_counted_reading(void *more) {
    race *self = more;
    int stopping;
    lock_test_mutex(&self->clock_mutex);
    stopping = self->readings_left > 0 && --self->readings_left == 0;
    unlock_test_mutex(&self->clock_mutex);
    if (stopping) {
        open_gate(&self->reached);
        pass_gate(&self->free_to_go);
    }
}

static void arm_the_stop(race *self, uint32_t readings) {
    lock_test_mutex(&self->clock_mutex);
    self->readings_left = readings;
    unlock_test_mutex(&self->clock_mutex);
}

/** Arms a deadline far away, so every later call reads the clock. */
static uint32_t activate_with_a_distant_deadline(void *data, weivel_c_context *context,
                                                 weivel_c_completion *completion, const char **reason) {
    test_plugin *self = data;
    weivel_c_want_options options = WEIVEL_INIT(weivel_c_want_options);
    weivel_c_want_callback callback = {trace_settlement, self};
    (void)completion;
    (void)reason;
    self->context = context;
    trace_line(&self->in->heard, "activate", self->manifest.import_name, "");
    options.timed = WEIVEL_CHOICE_YES;
    options.timeout_millis = 1000000000;
    CHECK(weivel_c_context_want_service(context, "never:there", &options, &callback, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    return WEIVEL_C_ENTRY_DONE;
}

static void begin_the_race(race *self) {
    test_plugin *only[1];
    memset(self, 0, sizeof *self);
    start_test_mutex(&self->clock_mutex);
    start_gate(&self->reached);
    start_gate(&self->free_to_go);
    start_gate(&self->dispatch_held);
    start_gate(&self->dispatch_free);
    begin_host_world(&self->in);
    self->in.clock_react = stop_at_the_counted_reading;
    self->in.clock_more = self;
    name_acting_plugin(&self->early, &self->in, "early", activate_with_a_distant_deadline, NULL);
    self->early.entry.deactivate = deactivate_and_wait;
    wants_service(&self->early, "never:there");
    name_acting_plugin(&self->late, &self->in, "late", activate_and_answer_done, NULL);
    self->late.entry.deactivate = deactivate_and_answer_done;
    register_test_plugin(&self->early);
    only[0] = &self->early;
    CHECK(activate_in_order(&self->in, only, 1, WEIVEL_EDGES_CAPABILITIES));
}

static void destroy_on_its_own_thread(void *argument) {
    race *self = argument;
    CHECK(weivel_c_host_destroy(self->in.host, &self->destroy_finished) == WEIVEL_C_STATUS_OK);
}

/** Destroys on another thread and waits until that destroy has returned, held by the early plugin's deactivate. */
static void destroy_meanwhile(race *self) {
    CHECK(start_test_thread(&self->destroyer, destroy_on_its_own_thread, self));
    join_test_thread(&self->destroyer);
    CHECK(self->destroy_finished == 0);
}

static void finish_the_destroy(race *self) {
    uint32_t finished = 0;
    CHECK(weivel_c_completion_finish(self->early.deactivation, WEIVEL_C_ENTRY_DONE, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_host_destroy(self->in.host, &finished) == WEIVEL_C_STATUS_OK && finished == 1);
    CHECK(sink_count(&self->in) == 0);
    CHECK(weivel_c_host_free(self->in.host) == WEIVEL_C_STATUS_OK);
}

static weivel_c_status hold_the_dispatch(void *data, weivel_c_core *core) {
    race *self = data;
    (void)core;
    open_gate(&self->dispatch_held);
    pass_gate(&self->dispatch_free);
    return WEIVEL_C_STATUS_OK;
}

static void dispatch_and_hold(void *argument) {
    race *self = argument;
    weivel_c_step step = WEIVEL_INIT(weivel_c_step);
    weivel_c_core *core = NULL;
    uint32_t here = 0;
    step.run = hold_the_dispatch;
    step.data = self;
    CHECK(weivel_c_host_core(self->in.host, &core) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_defer(core, &step, &here) == WEIVEL_C_STATUS_OK && here == 1);
}

static void activate_the_late_plugin(void *argument) {
    race *self = argument;
    weivel_c_activation activation = WEIVEL_INIT(weivel_c_activation);
    weivel_activation_plan plan = WEIVEL_INIT(weivel_activation_plan);
    activation.plugins = &self->late.handle;
    activation.count = 1;
    activation.edge_source = WEIVEL_EDGES_CAPABILITIES;
    activation.listener.hear = hear_outcome;
    activation.listener.data = &self->in.told;
    self->answered = weivel_c_host_activate(self->in.host, &activation, &plan, NULL);
    if (self->answered == WEIVEL_C_STATUS_OK) {
        CHECK(weivel_c_host_plan_release(self->in.host, &plan) == WEIVEL_C_STATUS_OK);
    }
}

/**
 * Holds another thread's dispatch, so the activation starts its plugin on its own thread, and stops it on its way
 * into the open: its plan is the first reading, the open the second. The destroy then runs its first step in the
 * held dispatch, and the open is let go only once that step has walked the plugins the host opened.
 */
static void an_activation_whose_open_races_the_destroys_first_step_releases_its_plugin_without_its_activate(void) {
    uint32_t kind = 0;
    begin_the_race(&racing);
    register_test_plugin(&racing.late);
    CHECK(start_test_thread(&racing.dispatcher, dispatch_and_hold, &racing));
    pass_gate(&racing.dispatch_held);
    arm_the_stop(&racing, 2);
    CHECK(start_test_thread(&racing.runner, activate_the_late_plugin, &racing));
    pass_gate(&racing.reached);
    destroy_meanwhile(&racing);
    open_gate(&racing.dispatch_free);
    join_test_thread(&racing.dispatcher);
    open_gate(&racing.free_to_go);
    join_test_thread(&racing.runner);
    CHECK(racing.answered == WEIVEL_C_STATUS_OK);
    CHECK(place_of(&racing.in.heard, "activate late") < 0);
    CHECK(outcomes_of(&racing.in.told, racing.late.handle, &kind) == 1 && kind == WEIVEL_C_OUTCOME_RELEASED);
    finish_the_destroy(&racing);
}

static void register_the_late_plugin(void *argument) {
    race *self = argument;
    lay_out_manifest(&self->late);
    self->answered = weivel_c_host_register(self->in.host, &self->late.manifest, &self->late.entry,
                                            &self->registered, NULL);
}

static void a_registration_that_races_the_destroy_answers_destroyed_and_keeps_nothing(void) {
    begin_the_race(&racing);
    arm_the_stop(&racing, 1);
    CHECK(start_test_thread(&racing.runner, register_the_late_plugin, &racing));
    pass_gate(&racing.reached);
    destroy_meanwhile(&racing);
    open_gate(&racing.free_to_go);
    join_test_thread(&racing.runner);
    CHECK(racing.answered == WEIVEL_C_STATUS_DESTROYED && racing.registered == 0);
    finish_the_destroy(&racing);
}

static void stop_at_the_scheduling(void *more) {
    race *self = more;
    int stopping;
    lock_test_mutex(&self->clock_mutex);
    stopping = self->readings_left > 0 && --self->readings_left == 0;
    unlock_test_mutex(&self->clock_mutex);
    if (stopping) {
        open_gate(&self->reached);
        pass_gate(&self->free_to_go);
    }
}

static void want_with_a_deadline(void *argument) {
    race *self = argument;
    weivel_c_want_options options = WEIVEL_INIT(weivel_c_want_options);
    weivel_c_want_callback callback = {trace_settlement, &self->late};
    options.timed = WEIVEL_CHOICE_YES;
    options.timeout_millis = 1000;
    self->answered = weivel_c_context_want_service(self->late.context, "never:there", &options, &callback, NULL, NULL);
}

/**
 * Holds a thread inside the wrapper's `follow` hook, where the timer schedules its task, while the destroy finishes on
 * another: a free made then would free the timer and the contexts under it.
 */
static void a_free_while_another_thread_is_still_inside_the_layer_frees_nothing(void) {
    test_plugin *only[1];
    uint32_t finished = 0;
    begin_the_race(&racing);
    racing.in.clock_react = NULL;
    racing.in.schedule_react = stop_at_the_scheduling;
    racing.in.schedule_more = &racing;
    wants_service(&racing.late, "never:there");
    register_test_plugin(&racing.late);
    only[0] = &racing.late;
    CHECK(activate_in_order(&racing.in, only, 1, WEIVEL_EDGES_CAPABILITIES));
    arm_the_stop(&racing, 1);
    CHECK(start_test_thread(&racing.runner, want_with_a_deadline, &racing));
    pass_gate(&racing.reached);
    CHECK(weivel_c_host_destroy(racing.in.host, &finished) == WEIVEL_C_STATUS_OK && finished == 0);
    CHECK(weivel_c_completion_finish(racing.early.deactivation, WEIVEL_C_ENTRY_DONE, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_host_destroy(racing.in.host, &finished) == WEIVEL_C_STATUS_OK && finished == 1);
    CHECK(weivel_c_host_free(racing.in.host) == WEIVEL_C_STATUS_MISUSE);
    open_gate(&racing.free_to_go);
    join_test_thread(&racing.runner);
    CHECK(weivel_c_host_free(racing.in.host) == WEIVEL_C_STATUS_OK);
}

int main(int argument_count, char **arguments) {
    if (argument_count != 2 || !load_tables(arguments[1])) {
        fprintf(stderr, "usage: host_races_test <core-library>\n");
        return 2;
    }
    an_activation_whose_open_races_the_destroys_first_step_releases_its_plugin_without_its_activate();
    a_registration_that_races_the_destroy_answers_destroyed_and_keeps_nothing();
    a_free_while_another_thread_is_still_inside_the_layer_frees_nothing();

    CHECK(weivel_c_core_unload(&real_table) == WEIVEL_C_STATUS_OK);
    return check_summary("host races");
}
