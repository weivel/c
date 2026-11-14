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

/** Runs the timer's task before its deadline, as a scheduler waking early does: the timer then follows it again. */
static void run_the_task_early(void *argument) {
    race *self = argument;
    weivel_c_task_callback callback;
    void *data;
    lock_test_mutex(&self->in.schedule_mutex);
    callback = self->in.scheduled_callback;
    data = self->in.scheduled_data;
    unlock_test_mutex(&self->in.schedule_mutex);
    callback(data);
}

/** Arms, through the host's own context, a deadline nearer than the early plugin's, which its release leaves alone. */
static void arm_a_nearer_deadline_of_the_hosts(race *self) {
    weivel_c_want_options options = WEIVEL_INIT(weivel_c_want_options);
    weivel_c_want_callback callback = {trace_settlement, &self->early};
    weivel_c_context *own = NULL;
    options.timed = WEIVEL_CHOICE_YES;
    options.timeout_millis = 1000;
    CHECK(weivel_c_host_context(self->in.host, &own) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_want_service(own, "never:there", &options, &callback, NULL, NULL) == WEIVEL_C_STATUS_OK);
}

/**
 * Holds a thread inside the wrapper's `follow` hook, where the timer schedules its task again after it ran early, while
 * the destroy finishes on another: a free made then would free the timer and the contexts under it. That `follow`
 * tells a deadline no call drained, and the destroy's release moves no deadline, so no event waits on the held thread.
 */
static void a_free_while_another_thread_is_still_inside_the_layer_frees_nothing(void) {
    uint32_t finished = 0;
    begin_the_race(&racing);
    arm_a_nearer_deadline_of_the_hosts(&racing);
    racing.in.clock_react = NULL;
    racing.in.schedule_react = stop_at_the_scheduling;
    racing.in.schedule_more = &racing;
    arm_the_stop(&racing, 1);
    CHECK(start_test_thread(&racing.runner, run_the_task_early, &racing));
    pass_gate(&racing.reached);
    CHECK(weivel_c_host_destroy(racing.in.host, &finished) == WEIVEL_C_STATUS_OK && finished == 0);
    CHECK(weivel_c_completion_finish(racing.early.deactivation, WEIVEL_C_ENTRY_DONE, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_host_destroy(racing.in.host, &finished) == WEIVEL_C_STATUS_OK && finished == 1);
    CHECK(weivel_c_host_free(racing.in.host) == WEIVEL_C_STATUS_MISUSE);
    open_gate(&racing.free_to_go);
    join_test_thread(&racing.runner);
    CHECK(weivel_c_host_free(racing.in.host) == WEIVEL_C_STATUS_OK);
}

/** How many times `line` was traced. */
static uint32_t times_traced(trace *self, const char *line) {
    uint32_t found = 0;
    uint32_t index;
    lock_test_mutex(&self->mutex);
    for (index = 0; index < self->count && index < HOST_WORLD_LINES; index++) {
        found += strcmp(self->lines[index], line) == 0;
    }
    unlock_test_mutex(&self->mutex);
    return found;
}

static void hold_another_threads_dispatch(race *self) {
    CHECK(start_test_thread(&self->dispatcher, dispatch_and_hold, self));
    pass_gate(&self->dispatch_held);
}

static void let_the_held_dispatch_go(race *self) {
    open_gate(&self->dispatch_free);
    join_test_thread(&self->dispatcher);
}

static void destroy_and_finish(race *self) {
    uint32_t finished = 9;
    CHECK(weivel_c_host_destroy(self->in.host, &finished) == WEIVEL_C_STATUS_OK && finished == 0);
    finish_the_destroy(self);
}

static void an_activation_alongside_another_threads_dispatch_starts_its_plugin_once_before_it_returns(void) {
    test_plugin *only[1];
    uint32_t kind = 0;
    begin_the_race(&racing);
    register_test_plugin(&racing.late);
    hold_another_threads_dispatch(&racing);
    only[0] = &racing.late;
    CHECK(activate_in_order(&racing.in, only, 1, WEIVEL_EDGES_CAPABILITIES));
    CHECK(times_traced(&racing.in.heard, "activate late") == 1);
    let_the_held_dispatch_go(&racing);
    CHECK(times_traced(&racing.in.heard, "activate late") == 1);
    CHECK(outcomes_of(&racing.in.told, racing.late.handle, &kind) == 1 && kind == WEIVEL_C_OUTCOME_ACTIVE);
    destroy_and_finish(&racing);
}

/** Fails its first activate, and registers `flaky:service` in every later one. */
static uint32_t fail_then_register(void *data, weivel_c_context *context, weivel_c_completion *completion,
                                   const char **reason) {
    test_plugin *self = data;
    weivel_handle *registration = self->more;
    (void)completion;
    self->context = context;
    trace_line(&self->in->heard, "activate", self->manifest.import_name, "");
    if (*registration == 0) {
        *registration = 1;
        *reason = "the first try fails";
        return WEIVEL_C_ENTRY_FAILED;
    }
    CHECK(weivel_c_context_register_service(context, "flaky:service", &registered_object, NULL, registration, NULL) ==
          WEIVEL_C_STATUS_OK);
    return WEIVEL_C_ENTRY_DONE;
}

/**
 * Quarantines a plugin and activates it again while another thread's dispatch is held, so the ending of the
 * quarantine's entries still waits in that dispatch when the second activate has registered its service.
 */
static void the_ending_of_a_quarantine_ends_no_entry_of_a_later_activation(void) {
    test_plugin flaky;
    test_plugin *only[1];
    weivel_handle registration = 0;
    begin_the_race(&racing);
    name_acting_plugin(&flaky, &racing.in, "flaky", fail_then_register, NULL);
    flaky.more = &registration;
    offers_service(&flaky, "flaky:service");
    register_test_plugin(&flaky);
    hold_another_threads_dispatch(&racing);
    only[0] = &flaky;
    CHECK(activate_in_order(&racing.in, only, 1, WEIVEL_EDGES_CAPABILITIES));
    CHECK(state_of(&racing.in, &flaky) == WEIVEL_C_PLUGIN_QUARANTINED);
    CHECK(activate_in_order(&racing.in, only, 1, WEIVEL_EDGES_CAPABILITIES));
    CHECK(state_of(&racing.in, &flaky) == WEIVEL_C_PLUGIN_ACTIVE && registration > 1);
    let_the_held_dispatch_go(&racing);
    CHECK(weivel_c_context_unregister_service(flaky.context, registration, NULL) == WEIVEL_C_STATUS_OK);
    destroy_and_finish(&racing);
}

static void register_through_the_late_plugins_context(void *argument) {
    race *self = argument;
    self->answered = weivel_c_context_register_service(self->late.context, "late:x", &registered_object, NULL, NULL,
                                                       NULL);
}

/**
 * Stops a plugin's own call on its way into the core, after any check made before the layer lock is taken, while
 * the plugin is released and forgotten on another thread: the call must find its context detached under that lock.
 */
static void a_call_whose_plugin_is_forgotten_on_its_way_in_answers_detached(void) {
    test_plugin *only[1];
    uint32_t finished = 0;
    begin_the_race(&racing);
    register_test_plugin(&racing.late);
    only[0] = &racing.late;
    CHECK(activate_in_order(&racing.in, only, 1, WEIVEL_EDGES_CAPABILITIES));
    arm_the_stop(&racing, 1);
    CHECK(start_test_thread(&racing.runner, register_through_the_late_plugins_context, &racing));
    pass_gate(&racing.reached);
    CHECK(weivel_c_host_release(racing.in.host, racing.late.handle, &finished) == WEIVEL_C_STATUS_OK && finished);
    open_gate(&racing.free_to_go);
    join_test_thread(&racing.runner);
    CHECK(racing.answered == WEIVEL_C_STATUS_DETACHED);
    destroy_and_finish(&racing);
}

static void finish_the_late_activation(void *argument) {
    race *self = argument;
    self->answered = weivel_c_completion_finish(self->late.activation, WEIVEL_C_ENTRY_DONE, NULL);
}

/**
 * Stops a settling on its way into the verify, once the release no longer answers that activation, while the destroy
 * releases its plugin and would end: the settling's outcome must still be queued before the core is destroyed.
 */
static void an_activate_settling_while_the_destroy_ends_is_still_told_its_outcome(void) {
    test_plugin *only[1];
    uint32_t finished = 9;
    uint32_t kind = 0;
    begin_the_race(&racing);
    racing.late.entry.activate = activate_and_wait;
    register_test_plugin(&racing.late);
    only[0] = &racing.late;
    CHECK(activate_in_order(&racing.in, only, 1, WEIVEL_EDGES_CAPABILITIES));
    arm_the_stop(&racing, 1);
    CHECK(start_test_thread(&racing.runner, finish_the_late_activation, &racing));
    pass_gate(&racing.reached);
    CHECK(weivel_c_host_destroy(racing.in.host, &finished) == WEIVEL_C_STATUS_OK && finished == 0);
    CHECK(weivel_c_completion_finish(racing.early.deactivation, WEIVEL_C_ENTRY_DONE, NULL) == WEIVEL_C_STATUS_OK);
    open_gate(&racing.free_to_go);
    join_test_thread(&racing.runner);
    CHECK(racing.answered == WEIVEL_C_STATUS_OK);
    CHECK(outcomes_of(&racing.in.told, racing.late.handle, &kind) == 1 && kind == WEIVEL_C_OUTCOME_RELEASED);
    CHECK(weivel_c_host_destroy(racing.in.host, &finished) == WEIVEL_C_STATUS_OK && finished == 1);
    CHECK(sink_count(&racing.in) == 0);
    CHECK(weivel_c_host_free(racing.in.host) == WEIVEL_C_STATUS_OK);
}

/**
 * A plugin released or quarantined on one thread, held inside the `follow` hook its ending call's own deadline sets
 * off, while another thread's dispatch is held and the plugin's own code reaches for an id it never declared: the
 * reach refusal is queued after the ending call, so the plugin's listeners must not hear it.
 */
typedef struct ending_race {
    host_world in;
    test_plugin held;
    test_mutex mutex;
    /** Guarded by `mutex`: the schedulings left until the one the ending thread stops at. */
    uint32_t schedulings_left;
    test_gate reached;
    test_gate free_to_go;
    test_gate dispatch_held;
    test_gate dispatch_free;
    test_thread dispatcher;
    test_thread ending;
    /** Written by the held dispatch's thread, read once it is joined. */
    uint32_t reach_refusals;
    uint32_t pending_activate;
} ending_race;

static ending_race ending_racing;

static void stop_at_the_counted_scheduling(void *more) {
    ending_race *self = more;
    int stopping;
    lock_test_mutex(&self->mutex);
    stopping = self->schedulings_left > 0 && --self->schedulings_left == 0;
    unlock_test_mutex(&self->mutex);
    if (stopping) {
        open_gate(&self->reached);
        pass_gate(&self->free_to_go);
    }
}

static weivel_c_status count_the_reach_refusals(void *data, const weivel_c_plugin_event *event) {
    ending_race *self = data;
    self->reach_refusals += event->kind == WEIVEL_C_PLUGIN_EVENT_REACH_REFUSED;
    return WEIVEL_C_STATUS_OK;
}

/** Listens for what the core reports about the plugin, and wants within a deadline nearer than the host's. */
static uint32_t activate_listening_and_wanting(void *data, weivel_c_context *context, weivel_c_completion *completion,
                                               const char **reason) {
    test_plugin *self = data;
    ending_race *race_of = self->more;
    weivel_c_plugin_listener listener = {count_the_reach_refusals, race_of};
    weivel_c_want_options options = WEIVEL_INIT(weivel_c_want_options);
    weivel_c_want_callback callback = {trace_settlement, self};
    uint64_t listening = 0;
    (void)reason;
    self->context = context;
    self->activation = completion;
    CHECK(weivel_c_context_add_listener(context, &listener, &listening) == WEIVEL_C_STATUS_OK);
    options.timed = WEIVEL_CHOICE_YES;
    options.timeout_millis = 1000;
    CHECK(weivel_c_context_want_service(context, "never:there", &options, &callback, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    return race_of->pending_activate ? WEIVEL_C_ENTRY_PENDING : WEIVEL_C_ENTRY_DONE;
}

static weivel_c_status hold_the_ending_dispatch(void *data, weivel_c_core *core) {
    ending_race *self = data;
    (void)core;
    open_gate(&self->dispatch_held);
    pass_gate(&self->dispatch_free);
    return WEIVEL_C_STATUS_OK;
}

static void dispatch_and_hold_the_ending(void *argument) {
    ending_race *self = argument;
    weivel_c_step step = WEIVEL_INIT(weivel_c_step);
    weivel_c_core *core = NULL;
    uint32_t here = 0;
    step.run = hold_the_ending_dispatch;
    step.data = self;
    CHECK(weivel_c_host_core(self->in.host, &core) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_defer(core, &step, &here) == WEIVEL_C_STATUS_OK && here == 1);
}

static void release_the_held_plugin(void *argument) {
    ending_race *self = argument;
    uint32_t finished = 9;
    CHECK(weivel_c_host_release(self->in.host, self->held.handle, &finished) == WEIVEL_C_STATUS_OK && finished == 0);
}

static void fail_the_held_plugins_activate(void *argument) {
    ending_race *self = argument;
    CHECK(weivel_c_completion_finish(self->held.activation, WEIVEL_C_ENTRY_FAILED, "it gave up") ==
          WEIVEL_C_STATUS_OK);
}

/** Arms a distant deadline of the host's own, and activates the held plugin, whose want arms a nearer one. */
static void begin_the_ending_race(ending_race *self, uint32_t pending_activate) {
    test_plugin *only[1];
    weivel_c_want_options options = WEIVEL_INIT(weivel_c_want_options);
    weivel_c_want_callback callback = {trace_settlement, &self->held};
    weivel_c_context *own = NULL;
    memset(self, 0, sizeof *self);
    start_test_mutex(&self->mutex);
    start_gate(&self->reached);
    start_gate(&self->free_to_go);
    start_gate(&self->dispatch_held);
    start_gate(&self->dispatch_free);
    self->pending_activate = pending_activate;
    begin_host_world(&self->in);
    name_acting_plugin(&self->held, &self->in, "held", activate_listening_and_wanting, NULL);
    self->held.more = self;
    options.timed = WEIVEL_CHOICE_YES;
    options.timeout_millis = 1000000000;
    CHECK(weivel_c_host_context(self->in.host, &own) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_want_service(own, "far:away", &options, &callback, NULL, NULL) == WEIVEL_C_STATUS_OK);
    wants_service(&self->held, "never:there");
    register_test_plugin(&self->held);
    only[0] = &self->held;
    CHECK(activate_in_order(&self->in, only, 1, WEIVEL_EDGES_CAPABILITIES));
}

/** The plugin's own code, on this thread, reaching for an id it never declared: a refusal of reach naming it. */
static void reach_for_an_undeclared_id(ending_race *self) {
    void *service = NULL;
    CHECK(weivel_c_context_get_service(self->held.context, "never:declared", &service, NULL) ==
          WEIVEL_C_STATUS_REFUSED);
}

static void run_the_ending_race(ending_race *self, void (*end_the_plugin)(void *argument)) {
    self->in.schedule_react = stop_at_the_counted_scheduling;
    self->in.schedule_more = self;
    lock_test_mutex(&self->mutex);
    self->schedulings_left = 1;
    unlock_test_mutex(&self->mutex);
    CHECK(start_test_thread(&self->dispatcher, dispatch_and_hold_the_ending, self));
    pass_gate(&self->dispatch_held);
    CHECK(start_test_thread(&self->ending, end_the_plugin, self));
    pass_gate(&self->reached);
    reach_for_an_undeclared_id(self);
    open_gate(&self->free_to_go);
    join_test_thread(&self->ending);
    open_gate(&self->dispatch_free);
    join_test_thread(&self->dispatcher);
    CHECK(self->reach_refusals == 0);
    CHECK(sink_count(&self->in) == 0);
    end_host_world(&self->in);
}

static void a_released_plugin_hears_nothing_queued_after_its_release(void) {
    begin_the_ending_race(&ending_racing, 0);
    run_the_ending_race(&ending_racing, release_the_held_plugin);
}

static void a_quarantined_plugin_hears_nothing_queued_after_its_quarantine(void) {
    begin_the_ending_race(&ending_racing, 1);
    run_the_ending_race(&ending_racing, fail_the_held_plugins_activate);
}

int main(int argument_count, char **arguments) {
    if (argument_count != 2 || !load_tables(arguments[1])) {
        fprintf(stderr, "usage: host_races_test <core-library>\n");
        return 2;
    }
    a_released_plugin_hears_nothing_queued_after_its_release();
    a_quarantined_plugin_hears_nothing_queued_after_its_quarantine();
    an_activation_whose_open_races_the_destroys_first_step_releases_its_plugin_without_its_activate();
    a_registration_that_races_the_destroy_answers_destroyed_and_keeps_nothing();
    a_free_while_another_thread_is_still_inside_the_layer_frees_nothing();
    an_activation_alongside_another_threads_dispatch_starts_its_plugin_once_before_it_returns();
    the_ending_of_a_quarantine_ends_no_entry_of_a_later_activation();
    a_call_whose_plugin_is_forgotten_on_its_way_in_answers_detached();
    an_activate_settling_while_the_destroy_ends_is_still_told_its_outcome();



    CHECK(weivel_c_core_unload(&real_table) == WEIVEL_C_STATUS_OK);
    return check_summary("host races");
}
