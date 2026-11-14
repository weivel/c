/**
 * @file
 * Release in its six steps and destroy in its four, at the top level and deferred inside a dispatch, with a
 * deactivate that finishes later, against the real core.
 */

#include "plugins.h"

/** Registers `calendar:events` and starts a want of `mail:inbox` without waiting for it. */
static uint32_t activate_the_calendar(void *data, weivel_c_context *context, weivel_c_completion *completion,
                                      const char **reason) {
    test_plugin *self = data;
    weivel_c_want_callback callback = {trace_settlement, self};
    (void)completion;
    (void)reason;
    self->context = context;
    trace_line(&self->in->heard, "activate", self->manifest.import_name, "");
    CHECK(weivel_c_context_register_service(context, "calendar:events", &registered_object, NULL, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_want_service(context, "mail:inbox", NULL, &callback, NULL, NULL) == WEIVEL_C_STATUS_OK);
    return WEIVEL_C_ENTRY_DONE;
}

/** Gets its own service while it is still whole. */
static uint32_t deactivate_the_calendar(void *data, weivel_c_context *context, weivel_c_completion *completion,
                                        const char **reason) {
    test_plugin *self = data;
    (void)completion;
    (void)reason;
    trace_line(&self->in->heard, "deactivate", self->manifest.import_name, "");
    traced_get(self, context, "calendar:events");
    return WEIVEL_C_ENTRY_DONE;
}

typedef struct calendar_and_agenda {
    host_world in;
    test_plugin calendar;
    test_plugin agenda;
    acting awaiting;
} calendar_and_agenda;

static void open_calendar_and_agenda(calendar_and_agenda *with) {
    test_plugin *both[2];
    begin_host_world(&with->in);
    name_acting_plugin(&with->calendar, &with->in, "calendar", activate_the_calendar, NULL);
    with->calendar.entry.deactivate = deactivate_the_calendar;
    offers_service(&with->calendar, "calendar:events");
    wants_service(&with->calendar, "mail:inbox");
    with->awaiting = (acting){"calendar:events", WEIVEL_C_STATUS_FAILED};
    name_acting_plugin(&with->agenda, &with->in, "agenda", activate_and_await_a_want, &with->awaiting);
    wants_service(&with->agenda, "calendar:events");
    register_test_plugin(&with->calendar);
    register_test_plugin(&with->agenda);
    both[0] = &with->calendar;
    both[1] = &with->agenda;
    CHECK(activate_in_order(&with->in, both, 2, WEIVEL_EDGES_CAPABILITIES));
}

/** Mirrors the layer vector lifecycle/a-release-deactivates-then-releases-then-forgets. */
static void a_release_deactivates_then_releases_then_forgets(void) {
    calendar_and_agenda with;
    uint32_t finished = 0;
    open_calendar_and_agenda(&with);
    CHECK(weivel_c_host_release(with.in.host, with.calendar.handle, &finished) == WEIVEL_C_STATUS_OK);
    CHECK(finished == 1);
    traced_get(&with.agenda, with.agenda.context, "calendar:events");
    CHECK(is_before(&with.in.heard, "deactivate calendar", "got calendar calendar:events"));
    CHECK(is_before(&with.in.heard, "got calendar calendar:events", "rejected calendar mail:inbox stopped"));
    CHECK(is_before(&with.in.heard, "rejected calendar mail:inbox stopped", "missed agenda calendar:events"));
    CHECK(state_of(&with.in, &with.agenda) == WEIVEL_C_PLUGIN_ACTIVE && state_of(&with.in, &with.calendar) == 0);
    CHECK(ledger_status_of(&with.in, &with.calendar, NULL) == 0);
    CHECK(with.agenda.activation == NULL && sink_count(&with.in) == 0);
    end_host_world(&with.in);
}

/** A release made inside a dispatch, and what stood when it returned. */
typedef struct releasing_inside {
    host_world *in;
    test_plugin *plugin;
    weivel_c_status answered;
    uint32_t finished;
    uint32_t state_on_return;
    uint32_t ledger_on_return;
} releasing_inside;

static weivel_c_status release_inside(void *data, weivel_c_core *core) {
    releasing_inside *self = data;
    (void)core;
    self->answered = weivel_c_host_release(self->in->host, self->plugin->handle, &self->finished);
    trace_line(&self->in->heard, "returned", "release", "");
    self->state_on_return = state_of(self->in, self->plugin);
    self->ledger_on_return = ledger_status_of(self->in, self->plugin, NULL);
    return WEIVEL_C_STATUS_OK;
}

static void run_in_a_dispatch(host_world *in, weivel_c_status (*run)(void *data, weivel_c_core *core), void *data) {
    weivel_c_step step = WEIVEL_INIT(weivel_c_step);
    weivel_c_core *core = NULL;
    uint32_t here = 0;
    step.run = run;
    step.data = data;
    CHECK(weivel_c_host_core(in->host, &core) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_defer(core, &step, &here) == WEIVEL_C_STATUS_OK && here == 1);
}

static void a_release_inside_a_dispatch_defers_its_last_steps_behind_the_events_it_queued(void) {
    calendar_and_agenda with;
    releasing_inside inside;
    open_calendar_and_agenda(&with);
    inside = (releasing_inside){&with.in, &with.calendar, WEIVEL_C_STATUS_FAILED, 9, 0, 0};
    run_in_a_dispatch(&with.in, release_inside, &inside);
    CHECK(inside.answered == WEIVEL_C_STATUS_OK && inside.finished == 0);
    CHECK(inside.state_on_return == WEIVEL_C_PLUGIN_RELEASING);
    CHECK(inside.ledger_on_return == WEIVEL_PLUGIN_STATUS_RELEASED);
    CHECK(is_before(&with.in.heard, "deactivate calendar", "returned release"));
    CHECK(is_before(&with.in.heard, "returned release", "rejected calendar mail:inbox stopped"));
    CHECK(state_of(&with.in, &with.calendar) == 0 && ledger_status_of(&with.in, &with.calendar, NULL) == 0);
    CHECK(sink_count(&with.in) == 0);
    end_host_world(&with.in);
}

static void finish_the_deactivation(void *argument) {
    test_plugin *self = argument;
    CHECK(weivel_c_completion_finish(self->deactivation, WEIVEL_C_ENTRY_DONE, NULL) == WEIVEL_C_STATUS_OK);
}

static void a_deactivate_that_finishes_later_moves_the_core_release_and_every_step_after_it(void) {
    host_world in;
    test_plugin plugin;
    test_plugin *only[1];
    test_thread finisher;
    weivel_c_want_callback callback;
    uint32_t finished = 9;
    begin_host_world(&in);
    name_acting_plugin(&plugin, &in, "calendar", activate_and_answer_done, NULL);
    plugin.entry.deactivate = deactivate_and_wait;
    wants_service(&plugin, "mail:inbox");
    register_test_plugin(&plugin);
    only[0] = &plugin;
    CHECK(activate_in_order(&in, only, 1, WEIVEL_EDGES_CAPABILITIES));
    callback = (weivel_c_want_callback){trace_settlement, &plugin};
    CHECK(weivel_c_context_want_service(plugin.context, "mail:inbox", NULL, &callback, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_host_release(in.host, plugin.handle, &finished) == WEIVEL_C_STATUS_OK && finished == 0);
    CHECK(state_of(&in, &plugin) == WEIVEL_C_PLUGIN_RELEASING);
    CHECK(ledger_status_of(&in, &plugin, NULL) == WEIVEL_PLUGIN_STATUS_ACTIVE);
    CHECK(place_of(&in.heard, "rejected calendar mail:inbox stopped") < 0);
    CHECK(start_test_thread(&finisher, finish_the_deactivation, &plugin));
    join_test_thread(&finisher);
    CHECK(is_before(&in.heard, "deactivate calendar", "rejected calendar mail:inbox stopped"));
    CHECK(state_of(&in, &plugin) == 0 && ledger_status_of(&in, &plugin, NULL) == 0);
    CHECK(weivel_c_completion_finish(plugin.deactivation, WEIVEL_C_ENTRY_DONE, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(sink_count(&in) == 0);
    end_host_world(&in);
}

/** Mirrors the layer vector lifecycle/destroy-releases-a-plugin-whose-activate-is-in-flight. */
static void destroy_releases_a_plugin_whose_activate_is_in_flight(void) {
    host_world in;
    test_plugin agenda;
    test_plugin *only[1];
    acting awaiting = {"calendar:events", WEIVEL_C_STATUS_FAILED};
    uint32_t finished = 0;
    uint32_t kind = 0;
    uint32_t loud;
    begin_host_world(&in);
    name_acting_plugin(&agenda, &in, "agenda", activate_and_await_a_want, &awaiting);
    agenda.entry.deactivate = deactivate_and_answer_done;
    wants_service(&agenda, "calendar:events");
    register_test_plugin(&agenda);
    only[0] = &agenda;
    CHECK(activate_in_order(&in, only, 1, WEIVEL_EDGES_CAPABILITIES));
    loud = sink_count(&in) + in.log.count;
    CHECK(weivel_c_host_destroy(in.host, &finished) == WEIVEL_C_STATUS_OK && finished == 1);
    CHECK(weivel_c_host_verify(in.host, agenda.handle, NULL) == WEIVEL_C_STATUS_DESTROYED);
    CHECK(is_before(&in.heard, "activate agenda", "deactivate agenda"));
    CHECK(is_before(&in.heard, "deactivate agenda", "rejected agenda calendar:events stopped"));
    CHECK(outcomes_of(&in.told, agenda.handle, &kind) == 1 && kind == WEIVEL_C_OUTCOME_RELEASED);
    CHECK(sink_count(&in) + in.log.count == loud);
    CHECK(weivel_c_host_destroy(in.host, &finished) == WEIVEL_C_STATUS_OK && finished == 1);
    CHECK(weivel_c_host_free(in.host) == WEIVEL_C_STATUS_OK);
}

static void after_a_destroy_every_call_answers_quietly_that_the_host_is_destroyed(void) {
    host_world in;
    test_plugin plugin;
    test_plugin *only[1];
    weivel_c_activation activation = WEIVEL_INIT(weivel_c_activation);
    weivel_activation_plan plan = WEIVEL_INIT(weivel_activation_plan);
    weivel_c_context *context = NULL;
    weivel_c_core *core = NULL;
    weivel_handle handle = 0;
    uint32_t state = 0;
    begin_host_world(&in);
    name_acting_plugin(&plugin, &in, "agenda", activate_and_wait, NULL);
    register_test_plugin(&plugin);
    only[0] = &plugin;
    CHECK(activate_in_order(&in, only, 1, WEIVEL_EDGES_CAPABILITIES));
    CHECK(weivel_c_host_destroy(in.host, NULL) == WEIVEL_C_STATUS_OK);
    activation.listener.hear = hear_outcome;
    CHECK(weivel_c_host_activate(in.host, &activation, &plan, NULL) == WEIVEL_C_STATUS_DESTROYED);
    CHECK(weivel_c_host_register(in.host, &plugin.manifest, &plugin.entry, &handle, NULL) ==
          WEIVEL_C_STATUS_DESTROYED);
    CHECK(weivel_c_host_register_service(in.host, "clock", &plan, NULL, NULL, NULL) == WEIVEL_C_STATUS_DESTROYED);
    CHECK(weivel_c_host_release(in.host, plugin.handle, NULL) == WEIVEL_C_STATUS_DESTROYED);
    CHECK(weivel_c_host_plugin_state(in.host, plugin.handle, &state) == WEIVEL_C_STATUS_DESTROYED);
    CHECK(weivel_c_host_context(in.host, &context) == WEIVEL_C_STATUS_DESTROYED);
    CHECK(weivel_c_host_core(in.host, &core) == WEIVEL_C_STATUS_DESTROYED);
    CHECK(weivel_c_completion_finish(plugin.activation, WEIVEL_C_ENTRY_DONE, NULL) == WEIVEL_C_STATUS_DESTROYED);
    CHECK(weivel_c_context_log(plugin.context, WEIVEL_LOG_ERROR, "after") == WEIVEL_C_STATUS_OK);
    CHECK(sink_count(&in) == 0 && in.log.count == 1);
    CHECK(weivel_c_host_free(in.host) == WEIVEL_C_STATUS_OK);
}

/** Three plugins opened in a line, the middle one quarantined, each tracing its deactivate. */
static void destroy_releases_the_last_opened_first_a_quarantined_one_included(void) {
    host_world in;
    test_plugin first;
    test_plugin middle;
    test_plugin last;
    test_plugin *all[3];
    begin_host_world(&in);
    name_acting_plugin(&first, &in, "first", activate_and_answer_done, NULL);
    name_acting_plugin(&middle, &in, "middle", activate_and_fail, NULL);
    name_acting_plugin(&last, &in, "last", activate_and_wait, NULL);
    first.entry.deactivate = deactivate_and_answer_done;
    middle.entry.deactivate = deactivate_and_answer_done;
    last.entry.deactivate = deactivate_and_answer_done;
    register_test_plugin(&first);
    register_test_plugin(&middle);
    register_test_plugin(&last);
    all[0] = &first;
    all[1] = &middle;
    all[2] = &last;
    CHECK(activate_in_order(&in, all, 3, WEIVEL_EDGES_CAPABILITIES));
    CHECK(state_of(&in, &middle) == WEIVEL_C_PLUGIN_QUARANTINED);
    end_host_world(&in);
    CHECK(is_before(&in.heard, "deactivate last", "deactivate middle"));
    CHECK(is_before(&in.heard, "deactivate middle", "deactivate first"));
}

typedef struct destroying_inside {
    host_world *in;
    uint32_t finished;
    uint32_t state_on_return;
} destroying_inside;

static weivel_c_status destroy_inside(void *data, weivel_c_core *core) {
    destroying_inside *self = data;
    (void)core;
    CHECK(weivel_c_host_destroy(self->in->host, &self->finished) == WEIVEL_C_STATUS_OK);
    trace_line(&self->in->heard, "returned", "destroy", "");
    return WEIVEL_C_STATUS_OK;
}

static void a_destroy_inside_a_dispatch_runs_behind_what_is_queued_and_finishes_there(void) {
    host_world in;
    test_plugin plugin;
    test_plugin *only[1];
    destroying_inside inside = {NULL, 9, 0};
    uint32_t finished = 0;
    begin_host_world(&in);
    name_acting_plugin(&plugin, &in, "agenda", activate_and_wait, NULL);
    plugin.entry.deactivate = deactivate_and_answer_done;
    register_test_plugin(&plugin);
    only[0] = &plugin;
    CHECK(activate_in_order(&in, only, 1, WEIVEL_EDGES_CAPABILITIES));
    inside.in = &in;
    run_in_a_dispatch(&in, destroy_inside, &inside);
    CHECK(inside.finished == 0);
    CHECK(is_before(&in.heard, "returned destroy", "deactivate agenda"));
    CHECK(weivel_c_host_destroy(in.host, &finished) == WEIVEL_C_STATUS_OK && finished == 1);
    CHECK(weivel_c_host_free(in.host) == WEIVEL_C_STATUS_OK);
}

static void a_pending_deactivate_holds_the_destroy_for_as_long_as_it_takes(void) {
    host_world in;
    test_plugin plugin;
    test_plugin *only[1];
    uint32_t finished = 9;
    begin_host_world(&in);
    name_acting_plugin(&plugin, &in, "agenda", activate_and_answer_done, NULL);
    plugin.entry.deactivate = deactivate_and_wait;
    register_test_plugin(&plugin);
    only[0] = &plugin;
    CHECK(activate_in_order(&in, only, 1, WEIVEL_EDGES_CAPABILITIES));
    CHECK(weivel_c_host_destroy(in.host, &finished) == WEIVEL_C_STATUS_OK && finished == 0);
    CHECK(weivel_c_host_free(in.host) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_host_destroy(in.host, &finished) == WEIVEL_C_STATUS_OK && finished == 0);
    CHECK(weivel_c_completion_finish(plugin.deactivation, WEIVEL_C_ENTRY_FAILED, "it would not stop") ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_host_destroy(in.host, &finished) == WEIVEL_C_STATUS_OK && finished == 1);
    CHECK(sink_count(&in) == 1 && in.sink.items[0].failure.kind == WEIVEL_C_FAILURE_DEACTIVATE);
    CHECK(strstr(in.sink.items[0].text, "it would not stop") != NULL);
    CHECK(weivel_c_host_free(in.host) == WEIVEL_C_STATUS_OK);
}

static void a_plugin_that_calls_its_context_after_it_was_forgotten_is_refused_and_reads_nothing_freed(void) {
    host_world in;
    test_plugin plugin;
    test_plugin *only[1];
    const weivel_manifest *manifest = NULL;
    void *service = NULL;
    begin_host_world(&in);
    name_acting_plugin(&plugin, &in, "agenda", activate_and_answer_done, NULL);
    register_test_plugin(&plugin);
    only[0] = &plugin;
    CHECK(activate_in_order(&in, only, 1, WEIVEL_EDGES_CAPABILITIES));
    CHECK(weivel_c_host_release(in.host, plugin.handle, NULL) == WEIVEL_C_STATUS_OK);
    forget_crossings();
    CHECK(weivel_c_context_get_service(plugin.context, "agenda:x", &service, NULL) == WEIVEL_C_STATUS_REFUSED);
    CHECK(weivel_c_context_register_service(plugin.context, "agenda:x", &service, NULL, NULL, NULL) ==
          WEIVEL_C_STATUS_REFUSED);
    CHECK(crossed.next_event == 0);
    CHECK(weivel_c_context_manifest(plugin.context, &manifest) == WEIVEL_C_STATUS_OK && manifest == &plugin.manifest);
    end_host_world(&in);
}

/** Calls the host from code it runs: each would wait for ever on a lock of the host's held around that code. */
static void call_the_host(void *more) {
    host_world *in = more;
    weivel_c_context *context = NULL;
    uint32_t state = 0;
    CHECK(weivel_c_host_plugin_state(in->host, 1, &state) != WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_host_context(in->host, &context) != WEIVEL_C_STATUS_MISUSE);
}

static uint32_t activate_calling_the_host(void *data, weivel_c_context *context, weivel_c_completion *completion,
                                          const char **reason) {
    test_plugin *self = data;
    (void)completion;
    (void)context;
    call_the_host(self->in);
    *reason = "on purpose";
    return WEIVEL_C_ENTRY_FAILED;
}

static uint32_t deactivate_calling_the_host_and_failing(void *data, weivel_c_context *context,
                                                       weivel_c_completion *completion, const char **reason) {
    test_plugin *self = data;
    (void)completion;
    (void)context;
    call_the_host(self->in);
    *reason = "on purpose";
    return WEIVEL_C_ENTRY_FAILED;
}

static void no_entry_listener_log_or_sink_runs_under_a_lock_of_the_hosts(void) {
    host_world in;
    test_plugin plugin;
    test_plugin *only[1];
    weivel_core_options core_options = WEIVEL_INIT(weivel_core_options);
    core_options.minimum_log_level = WEIVEL_LOG_INFO;
    begin_host_world_with(&in, &counting_table, &core_options);
    in.log.react = call_the_host;
    in.log.more = &in;
    in.sink_react = call_the_host;
    in.sink_more = &in;
    name_acting_plugin(&plugin, &in, "agenda", activate_calling_the_host, NULL);
    plugin.entry.deactivate = deactivate_calling_the_host_and_failing;
    register_test_plugin(&plugin);
    only[0] = &plugin;
    CHECK(activate_in_order(&in, only, 1, WEIVEL_EDGES_CAPABILITIES));
    CHECK(state_of(&in, &plugin) == WEIVEL_C_PLUGIN_QUARANTINED && in.log.count >= 1);
    CHECK(weivel_c_host_release(in.host, plugin.handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(sink_count(&in) == 1);
    end_host_world(&in);
}

int main(int argument_count, char **arguments) {
    if (argument_count != 2 || !load_tables(arguments[1])) {
        fprintf(stderr, "usage: host_lifecycle_test <core-library>\n");
        return 2;
    }
    a_release_deactivates_then_releases_then_forgets();
    a_release_inside_a_dispatch_defers_its_last_steps_behind_the_events_it_queued();
    a_deactivate_that_finishes_later_moves_the_core_release_and_every_step_after_it();
    destroy_releases_a_plugin_whose_activate_is_in_flight();
    after_a_destroy_every_call_answers_quietly_that_the_host_is_destroyed();
    destroy_releases_the_last_opened_first_a_quarantined_one_included();
    a_destroy_inside_a_dispatch_runs_behind_what_is_queued_and_finishes_there();
    a_pending_deactivate_holds_the_destroy_for_as_long_as_it_takes();
    a_plugin_that_calls_its_context_after_it_was_forgotten_is_refused_and_reads_nothing_freed();
    no_entry_listener_log_or_sink_runs_under_a_lock_of_the_hosts();
    CHECK(weivel_c_core_unload(&real_table) == WEIVEL_C_STATUS_OK);
    return check_summary("host lifecycle");
}
