/**
 * @file
 * Activates that finish later, from several threads, while another thread releases some of their plugins: each
 * started plugin is told exactly one outcome, every finish is answered as a value, and nothing reaches the sink.
 *
 * @implNote What the outcome listener records is guarded by its own mutex; what an entry function records is not,
 * since the host calls each from the dispatch or from the thread that finishes it, so a call made under another
 * thread's feet is a race the thread sanitizer names.
 */

#include "plugins.h"

#define PLUGINS 12
#define FINISHERS 3

typedef struct crowd {
    host_world in;
    test_plugin plugins[PLUGINS];
    char names[PLUGINS][16];
    weivel_c_status finished_as[PLUGINS];
    test_thread finishers[FINISHERS];
    test_thread releaser;
    uint32_t releases_refused;
} crowd;

static crowd everyone;

static void finish_every_third(void *argument) {
    uint32_t first = (uint32_t)(size_t)argument;
    uint32_t index;
    for (index = first; index < PLUGINS; index += FINISHERS) {
        everyone.finished_as[index] =
            weivel_c_completion_finish(everyone.plugins[index].activation, WEIVEL_C_ENTRY_DONE, NULL);
        let_other_threads_run();
    }
}

static void release_every_fourth(void *argument) {
    uint32_t index;
    (void)argument;
    for (index = 0; index < PLUGINS; index += 4) {
        everyone.releases_refused += weivel_c_host_release(everyone.in.host, everyone.plugins[index].handle, NULL) !=
                                     WEIVEL_C_STATUS_OK;
        let_other_threads_run();
    }
}

static void open_everyone(void) {
    test_plugin *all[PLUGINS];
    uint32_t index;
    for (index = 0; index < PLUGINS; index++) {
        snprintf(everyone.names[index], sizeof everyone.names[index], "p%u", (unsigned)index);
        name_acting_plugin(&everyone.plugins[index], &everyone.in, everyone.names[index], activate_and_wait, NULL);
        everyone.plugins[index].entry.deactivate = deactivate_and_answer_done;
        register_test_plugin(&everyone.plugins[index]);
        all[index] = &everyone.plugins[index];
    }
    CHECK(activate_in_order(&everyone.in, all, PLUGINS, WEIVEL_EDGES_CAPABILITIES));
}

static void check_everyone_was_told_once(void) {
    uint32_t index;
    for (index = 0; index < PLUGINS; index++) {
        uint32_t kind = 0;
        weivel_c_status finished = everyone.finished_as[index];
        CHECK(outcomes_of(&everyone.in.told, everyone.plugins[index].handle, &kind) == 1);
        if (index % 4 != 0) {
            CHECK(finished == WEIVEL_C_STATUS_OK && kind == WEIVEL_C_OUTCOME_ACTIVE);
        } else {
            CHECK((finished == WEIVEL_C_STATUS_NONE && kind == WEIVEL_C_OUTCOME_RELEASED) ||
                  (finished == WEIVEL_C_STATUS_OK &&
                   (kind == WEIVEL_C_OUTCOME_ACTIVE || kind == WEIVEL_C_OUTCOME_RELEASED)));
        }
    }
}

static void activates_finished_on_several_threads_while_another_releases_are_each_told_once(void) {
    uint32_t index;
    memset(&everyone, 0, sizeof everyone);
    begin_host_world(&everyone.in);
    open_everyone();
    for (index = 0; index < FINISHERS; index++) {
        CHECK(start_test_thread(&everyone.finishers[index], finish_every_third, (void *)(size_t)index));
    }
    CHECK(start_test_thread(&everyone.releaser, release_every_fourth, NULL));
    for (index = 0; index < FINISHERS; index++) {
        join_test_thread(&everyone.finishers[index]);
    }
    join_test_thread(&everyone.releaser);
    CHECK(everyone.releases_refused == 0);
    check_everyone_was_told_once();
    CHECK(sink_count(&everyone.in) == 0);
    end_host_world(&everyone.in);
}

/** Two plugins, the first active with a deactivate that waits, so a destroy holds while the second is activated. */
typedef struct contest {
    host_world in;
    test_plugin early;
    test_plugin late;
    test_thread activator;
    weivel_c_status activated;
} contest;

static contest contested;

static void activate_late_on_its_own_thread(void *argument) {
    contest *self = argument;
    weivel_c_activation activation = WEIVEL_INIT(weivel_c_activation);
    weivel_activation_plan plan = WEIVEL_INIT(weivel_activation_plan);
    activation.plugins = &self->late.handle;
    activation.count = 1;
    activation.edge_source = WEIVEL_EDGES_CAPABILITIES;
    activation.listener.hear = hear_outcome;
    activation.listener.data = &self->in.told;
    self->activated = weivel_c_host_activate(self->in.host, &activation, &plan, NULL);
    if (self->activated == WEIVEL_C_STATUS_OK) {
        CHECK(weivel_c_host_plan_release(self->in.host, &plan) == WEIVEL_C_STATUS_OK);
    }
}

static void open_the_contest(contest *self) {
    test_plugin *only[1];
    memset(self, 0, sizeof *self);
    begin_host_world(&self->in);
    name_acting_plugin(&self->early, &self->in, "early", activate_and_answer_done, NULL);
    self->early.entry.deactivate = deactivate_and_wait;
    name_acting_plugin(&self->late, &self->in, "late", activate_and_answer_done, NULL);
    self->late.entry.deactivate = deactivate_and_answer_done;
    register_test_plugin(&self->early);
    register_test_plugin(&self->late);
    only[0] = &self->early;
    CHECK(activate_in_order(&self->in, only, 1, WEIVEL_EDGES_CAPABILITIES));
}

/** A late plugin whose activate was called is deactivated, and one an activation answered OK for is told once. */
static void check_the_late_plugin(contest *self) {
    uint32_t kind = 0;
    uint32_t told = outcomes_of(&self->in.told, self->late.handle, &kind);
    if (place_of(&self->in.heard, "activate late") >= 0) {
        CHECK(place_of(&self->in.heard, "deactivate late") >= 0);
    }
    CHECK(told == (self->activated == WEIVEL_C_STATUS_OK ? 1u : 0u));
    CHECK(self->activated == WEIVEL_C_STATUS_OK || self->activated == WEIVEL_C_STATUS_DESTROYED);
}

static void an_activation_racing_a_destroy_leaves_no_plugin_activated_past_it(void) {
    uint32_t finished = 0;
    open_the_contest(&contested);
    CHECK(start_test_thread(&contested.activator, activate_late_on_its_own_thread, &contested));
    CHECK(weivel_c_host_destroy(contested.in.host, &finished) == WEIVEL_C_STATUS_OK);
    join_test_thread(&contested.activator);
    CHECK(weivel_c_completion_finish(contested.early.deactivation, WEIVEL_C_ENTRY_DONE, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_host_destroy(contested.in.host, &finished) == WEIVEL_C_STATUS_OK && finished == 1);
    check_the_late_plugin(&contested);
    CHECK(sink_count(&contested.in) == 0);
    CHECK(weivel_c_host_free(contested.in.host) == WEIVEL_C_STATUS_OK);
}

/** Finishes every activation of the crowd on several threads while the main thread destroys the host. */
static void a_destroy_running_while_activates_settle_tells_each_plugin_once(void) {
    uint32_t index;
    uint32_t finished = 0;
    memset(&everyone, 0, sizeof everyone);
    begin_host_world(&everyone.in);
    open_everyone();
    for (index = 0; index < FINISHERS; index++) {
        CHECK(start_test_thread(&everyone.finishers[index], finish_every_third, (void *)(size_t)index));
    }
    CHECK(weivel_c_host_destroy(everyone.in.host, &finished) == WEIVEL_C_STATUS_OK);
    for (index = 0; index < FINISHERS; index++) {
        join_test_thread(&everyone.finishers[index]);
    }
    CHECK(weivel_c_host_destroy(everyone.in.host, &finished) == WEIVEL_C_STATUS_OK && finished == 1);
    for (index = 0; index < PLUGINS; index++) {
        uint32_t kind = 0;
        weivel_c_status settled = everyone.finished_as[index];
        CHECK(outcomes_of(&everyone.in.told, everyone.plugins[index].handle, &kind) == 1);
        CHECK(settled == WEIVEL_C_STATUS_OK || settled == WEIVEL_C_STATUS_NONE || settled == WEIVEL_C_STATUS_DESTROYED);
    }
    CHECK(sink_count(&everyone.in) == 0);
    CHECK(weivel_c_host_free(everyone.in.host) == WEIVEL_C_STATUS_OK);
}

/** A thread that keeps a dispatch running, step after step, until it is told to stop. */
typedef struct busy_dispatcher {
    host_world *in;
    weivel_c_step step;
    test_mutex mutex;
    /** Guarded by `mutex`. */
    int stopping;
    /** Guarded by `mutex`. */
    uint32_t runs;
} busy_dispatcher;

static busy_dispatcher busy;

static int is_stopping(busy_dispatcher *self) {
    int stopping;
    lock_test_mutex(&self->mutex);
    stopping = self->stopping;
    unlock_test_mutex(&self->mutex);
    return stopping;
}

static weivel_c_status run_again_until_stopped(void *data, weivel_c_core *core) {
    busy_dispatcher *self = data;
    lock_test_mutex(&self->mutex);
    self->runs++;
    unlock_test_mutex(&self->mutex);
    let_other_threads_run();
    if (!is_stopping(self)) {
        CHECK(weivel_c_core_defer(core, &self->step, NULL) == WEIVEL_C_STATUS_OK);
    }
    return WEIVEL_C_STATUS_OK;
}

static void keep_dispatching(void *argument) {
    busy_dispatcher *self = argument;
    weivel_c_core *core = NULL;
    CHECK(weivel_c_host_core(self->in->host, &core) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_defer(core, &self->step, NULL) == WEIVEL_C_STATUS_OK);
}

static int was_activated_before_the_activation_returned(host_world *in, const char *name) {
    char line[HOST_WORLD_TEXT];
    snprintf(line, sizeof line, "activate %s", name);
    return place_of(&in->heard, line) >= 0;
}

/** Waits until the busy thread's step has run once: from then on that thread holds the dispatch until stopped. */
static void wait_until_dispatching(busy_dispatcher *self) {
    uint32_t runs = 0;
    while (runs == 0) {
        lock_test_mutex(&self->mutex);
        runs = self->runs;
        unlock_test_mutex(&self->mutex);
        let_other_threads_run();
    }
}

static void activations_alongside_another_threads_dispatch_start_each_plugin_once(void) {
    test_thread dispatcher;
    test_plugin *one[1];
    uint32_t index;
    memset(&everyone, 0, sizeof everyone);
    memset(&busy, 0, sizeof busy);
    begin_host_world(&everyone.in);
    start_test_mutex(&busy.mutex);
    busy.in = &everyone.in;
    busy.step = (weivel_c_step)WEIVEL_INIT(weivel_c_step);
    busy.step.run = run_again_until_stopped;
    busy.step.data = &busy;
    CHECK(start_test_thread(&dispatcher, keep_dispatching, &busy));
    wait_until_dispatching(&busy);
    for (index = 0; index < PLUGINS; index++) {
        snprintf(everyone.names[index], sizeof everyone.names[index], "p%u", (unsigned)index);
        name_acting_plugin(&everyone.plugins[index], &everyone.in, everyone.names[index], activate_and_answer_done,
                           NULL);
        register_test_plugin(&everyone.plugins[index]);
        one[0] = &everyone.plugins[index];
        CHECK(activate_in_order(&everyone.in, one, 1, WEIVEL_EDGES_CAPABILITIES));
        CHECK(was_activated_before_the_activation_returned(&everyone.in, everyone.names[index]));
    }
    lock_test_mutex(&busy.mutex);
    busy.stopping = 1;
    unlock_test_mutex(&busy.mutex);
    join_test_thread(&dispatcher);
    for (index = 0; index < PLUGINS; index++) {
        uint32_t kind = 0;
        CHECK(outcomes_of(&everyone.in.told, everyone.plugins[index].handle, &kind) == 1 &&
              kind == WEIVEL_C_OUTCOME_ACTIVE);
    }
    CHECK(everyone.in.heard.count == 2 * PLUGINS && sink_count(&everyone.in) == 0);
    end_host_world(&everyone.in);
}

/** Two threads releasing one plugin, and what each was answered. */
typedef struct twin_release {
    host_world in;
    test_plugin plugin;
    test_thread releasers[2];
    weivel_c_status answered[2];
} twin_release;

static twin_release twins;

static void release_the_one_plugin(void *argument) {
    uint32_t which = (uint32_t)(size_t)argument;
    twins.answered[which] = weivel_c_host_release(twins.in.host, twins.plugin.handle, NULL);
}

static void two_releases_of_one_plugin_racing_release_it_once(void) {
    test_plugin *only[1];
    uint32_t index;
    memset(&twins, 0, sizeof twins);
    begin_host_world(&twins.in);
    name_acting_plugin(&twins.plugin, &twins.in, "agenda", activate_and_answer_done, NULL);
    twins.plugin.entry.deactivate = deactivate_and_answer_done;
    register_test_plugin(&twins.plugin);
    only[0] = &twins.plugin;
    CHECK(activate_in_order(&twins.in, only, 1, WEIVEL_EDGES_CAPABILITIES));
    for (index = 0; index < 2; index++) {
        CHECK(start_test_thread(&twins.releasers[index], release_the_one_plugin, (void *)(size_t)index));
    }
    for (index = 0; index < 2; index++) {
        join_test_thread(&twins.releasers[index]);
    }
    CHECK((twins.answered[0] == WEIVEL_C_STATUS_OK) + (twins.answered[1] == WEIVEL_C_STATUS_OK) == 1);
    CHECK(twins.answered[0] == WEIVEL_C_STATUS_NONE || twins.answered[1] == WEIVEL_C_STATUS_NONE);
    CHECK(place_of(&twins.in.heard, "deactivate agenda") >= 0 && twins.in.heard.count == 3);
    CHECK(state_of(&twins.in, &twins.plugin) == 0 && sink_count(&twins.in) == 0);
    end_host_world(&twins.in);
}

int main(int argument_count, char **arguments) {
    uint32_t round;
    if (argument_count != 2 || !load_tables(arguments[1])) {
        fprintf(stderr, "usage: host_threads_test <core-library>\n");
        return 2;
    }
    for (round = 0; round < 20; round++) {
        activates_finished_on_several_threads_while_another_releases_are_each_told_once();
    }
    for (round = 0; round < 200; round++) {
        an_activation_racing_a_destroy_leaves_no_plugin_activated_past_it();
    }
    for (round = 0; round < 20; round++) {
        a_destroy_running_while_activates_settle_tells_each_plugin_once();
        activations_alongside_another_threads_dispatch_start_each_plugin_once();
    }
    for (round = 0; round < 200; round++) {
        two_releases_of_one_plugin_racing_release_it_once();
    }


    CHECK(weivel_c_core_unload(&real_table) == WEIVEL_C_STATUS_OK);
    return check_summary("host threads");
}
