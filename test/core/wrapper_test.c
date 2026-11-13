#include "fixture.h"

#if !defined(WEIVEL_C_USING_SHARED)
#include "../../src/core/events.h"
#endif

static const char *const TWO_SERVICES[] = {"p:a", "p:b"};

/** A plugin whose two registered services the host watches, so releasing it queues several events in one call. */
static weivel_handle open_a_watched_provider(weivel_c_core *core) {
    weivel_handle plugin = open_plugin(core, "p", TWO_SERVICES, 2, NULL, 0);
    weivel_handle handle = 0;
    CHECK(weivel_c_core_watch_service(core, WEIVEL_HOST, "p:a", &handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_watch_service(core, WEIVEL_HOST, "p:b", &handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_watch_service(core, WEIVEL_HOST, "c", &handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_register_service(core, plugin, "p:a", &handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_register_service(core, plugin, "p:b", &handle, NULL) == WEIVEL_C_STATUS_OK);
    return plugin;
}

static int is_empty_refusal(const weivel_c_refusal *refusal) {
    return refusal->code == WEIVEL_REFUSAL_NONE && refusal->detail == NULL && refusal->fix == NULL &&
           refusal->subject == NULL && refusal->has_fix == 0 && refusal->storage == NULL;
}

static int is_in_drained_order(const hearing *listener, uint64_t first_sequence) {
    uint32_t index;
    for (index = 0; index < listener->count; index++) {
        if (listener->items[index].sequence != first_sequence + index) {
            return 0;
        }
    }
    return 1;
}

static void a_call_that_reports_nothing_waiting_crosses_no_further(void) {
    hearing listener = {0};
    telling sink = {0};
    weivel_c_core *core = make_core(NULL, &listener, &sink);
    uint32_t condition = 7;
    forget_crossings();
    CHECK(weivel_c_core_handle_state(core, 0x123456789u, &condition, NULL) == WEIVEL_C_STATUS_REFUSED);
    CHECK(crossed.handle_state == 1);
    CHECK(crossed.next_event == 0);
    CHECK(listener.count == 0 && sink.count == 0);
    end_core(core);
}

static void drains_once_for_each_event_the_core_reports_waiting(void) {
    hearing listener = {0};
    telling sink = {0};
    weivel_c_core *core = make_core(NULL, &listener, &sink);
    weivel_handle plugin = open_a_watched_provider(core);
    listener.count = 0;
    forget_crossings();
    CHECK(weivel_c_core_release_plugin(core, plugin, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(crossed.events >= 2);
    CHECK(crossed.next_event == crossed.events);
    CHECK(listener.count == crossed.events);
    CHECK(count_heard(&listener, WEIVEL_EVENT_WATCH) == 2);
    CHECK(sink.count == 0);
    end_core(core);
}

static void stops_at_none_when_the_core_holds_fewer_events_than_it_reported(void) {
    hearing listener = {0};
    telling sink = {0};
    weivel_c_core *core = make_core(NULL, &listener, &sink);
    uint32_t condition = 0;
    forget_crossings();
    phantom_events = 1;
    CHECK(weivel_c_core_handle_state(core, 0x123456789u, &condition, NULL) == WEIVEL_C_STATUS_REFUSED);
    CHECK(crossed.next_event == 1);
    CHECK(crossed.events == 0);
    CHECK(listener.count == 0 && sink.count == 0);
    end_core(core);
}

static void releases_each_event_before_it_drains_the_next(void) {
    hearing listener = {0};
    weivel_c_core *core = make_core(NULL, &listener, NULL);
    weivel_handle plugin = open_a_watched_provider(core);
    forget_crossings();
    CHECK(weivel_c_core_release_plugin(core, plugin, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(crossed.events >= 2);
    CHECK(crossed.most_unreleased == 1);
    CHECK(crossed.unreleased == 0);
    CHECK(crossed.event_release == crossed.events);
    CHECK(crossed.unsized_events == 0);
    end_core(core);
}

/** The text the core handed the layer in the last event of each kind, and where it lay in the core's memory. */
typedef struct core_text {
    const char *address;
    char copy[FIXTURE_TEXT];
} core_text;

static core_text core_texts[WEIVEL_EVENT_DEADLINE + 1];

static void remember_text(weivel_event *event) {
    const char *text = event->kind == WEIVEL_EVENT_LOG       ? event->data.log.text
                       : event->kind == WEIVEL_EVENT_REFUSAL ? event->data.refusal.detail
                       : event->kind == WEIVEL_EVENT_REACH_REFUSED ? event->data.reach.id
                                                                   : NULL;
    if (text != NULL) {
        core_texts[event->kind].address = text;
        snprintf(core_texts[event->kind].copy, FIXTURE_TEXT, "%s", text);
    }
}

static void check_own_copy(const char *text, uint32_t kind) {
    CHECK(text != NULL && text != core_texts[kind].address);
    check_text(text, core_texts[kind].copy, "the layer's copy of a text");
}

static weivel_c_status check_texts_are_copies(hearing *self, weivel_c_core *core, const weivel_c_event *event) {
    (void)self;
    (void)core;
    if (event->kind == WEIVEL_EVENT_LOG) {
        check_own_copy(event->data.log.text, WEIVEL_EVENT_LOG);
    } else if (event->kind == WEIVEL_EVENT_REFUSAL) {
        check_own_copy(event->data.refused.refusal.detail, WEIVEL_EVENT_REFUSAL);
        check_text(event->data.refused.subject, "elsewhere:x", "a refusal event's subject");
    } else if (event->kind == WEIVEL_EVENT_REACH_REFUSED) {
        check_own_copy(event->data.reach.id, WEIVEL_EVENT_REACH_REFUSED);
        check_text(event->data.reach.subject, "p:undeclared", "a reach refusal's subject");
        CHECK(event->data.reach.refusal.detail != NULL && event->data.reach.kind == WEIVEL_REACH_SERVICE);
    }
    return WEIVEL_C_STATUS_OK;
}

static void texts_inside_an_event_are_the_layers_own_copies(void) {
    hearing listener = {0};
    weivel_core_options core_options = WEIVEL_INIT(weivel_core_options);
    weivel_service_want consumed = want_of("p:a");
    weivel_c_core *core;
    weivel_handle plugin;
    weivel_handle handle = 0;
    forget_crossings();
    memset(core_texts, 0, sizeof core_texts);
    bend_event = remember_text;
    listener.react = check_texts_are_copies;
    core_options.minimum_log_level = WEIVEL_LOG_DEBUG;
    core = make_core(&core_options, &listener, NULL);
    CHECK(count_heard(&listener, WEIVEL_EVENT_LOG) >= 1);
    plugin = open_plugin(core, "c", NULL, 0, &consumed, 1);
    CHECK(weivel_c_core_register_service(core, plugin, "elsewhere:x", &handle, NULL) == WEIVEL_C_STATUS_REFUSED);
    CHECK(weivel_c_core_get_service(core, plugin, "p:undeclared", &handle, NULL) == WEIVEL_C_STATUS_REFUSED);
    CHECK(count_heard(&listener, WEIVEL_EVENT_REFUSAL) == 1);
    CHECK(count_heard(&listener, WEIVEL_EVENT_REACH_REFUSED) == 1);
    end_core(core);
}

/** A newer core's number: the layer's, then fields this layer was not compiled with. */
typedef struct wider_number {
    weivel_number number;
    double later;
    uint64_t later_still;
} wider_number;

static wider_number wider_constraints[2];

static void widen_constraints(weivel_event *event) {
    if (event->kind == WEIVEL_EVENT_CAPACITY_WANTED) {
        event->data.capacity.constraints.items = &wider_constraints[0].number;
        event->data.capacity.constraints.count = 2;
    }
}

/** Where the core's own constraints lay in the last capacity event it handed the layer. */
static weivel_numbers core_constraints;

static void remember_constraints(weivel_event *event) {
    if (event->kind == WEIVEL_EVENT_CAPACITY_WANTED) {
        core_constraints = event->data.capacity.constraints;
    }
}

static weivel_c_status check_constraints_are_copies(hearing *self, weivel_c_core *core,
                                                    const weivel_c_event *event) {
    (void)core;
    if (event->kind == WEIVEL_EVENT_CAPACITY_WANTED && self->more != NULL) {
        const weivel_numbers *constraints = &event->data.capacity.constraints;
        uint32_t *checked = self->more;
        CHECK(event->data.capacity.question == WEIVEL_CAPACITY_ATTRIBUTES);
        CHECK(constraints->count == 1 && core_constraints.count == 1);
        if (constraints->count == 1) {
            CHECK(constraints->items != core_constraints.items);
            CHECK(constraints->items[0].size == sizeof(weivel_number));
            check_text(constraints->items[0].name, "speed", "the constraint's name");
            CHECK(constraints->items[0].value == 5.0);
        }
        *checked += 1;
        self->more = NULL;
    }
    return WEIVEL_C_STATUS_OK;
}

/** Makes a constrained want of one plugin on another's registered service, so the core asks a capacity question. */
static void ask_a_capacity_question(weivel_c_core *core) {
    static const char *const PROVIDED[] = {"p:a"};
    weivel_number constraint = WEIVEL_INIT(weivel_number);
    weivel_service_want consumed = want_of("p:a");
    weivel_want_options options = WEIVEL_INIT(weivel_want_options);
    weivel_handle provider;
    weivel_handle consumer;
    weivel_handle handle = 0;
    constraint.name = "speed";
    constraint.value = 5.0;
    consumed.constraints.items = &constraint;
    consumed.constraints.count = 1;
    options.poll_millis = 10;
    provider = open_plugin(core, "p", PROVIDED, 1, NULL, 0);
    consumer = open_plugin(core, "c", NULL, 0, &consumed, 1);
    CHECK(weivel_c_core_register_service(core, provider, "p:a", &handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_want_service(core, consumer, "p:a", &options, &handle, NULL) == WEIVEL_C_STATUS_OK);
}

static weivel_core_options options_with_a_speed_constraint(weivel_constraint_entry *entry) {
    weivel_core_options core_options = WEIVEL_INIT(weivel_core_options);
    *entry = (weivel_constraint_entry)WEIVEL_INIT(weivel_constraint_entry);
    entry->constraint_name = "speed";
    entry->attribute_name = "speed";
    entry->bound = WEIVEL_BOUND_LOWER;
    core_options.constraint_vocabulary.items = entry;
    core_options.constraint_vocabulary.count = 1;
    return core_options;
}

static void a_capacity_events_constraints_are_the_layers_own_copies(void) {
    hearing listener = {0};
    uint32_t checked = 0;
    weivel_constraint_entry entry;
    weivel_core_options core_options = options_with_a_speed_constraint(&entry);
    weivel_c_core *core;
    forget_crossings();
    memset(&core_constraints, 0, sizeof core_constraints);
    bend_event = remember_constraints;
    listener.react = check_constraints_are_copies;
    listener.more = &checked;
    core = make_core(&core_options, &listener, NULL);
    ask_a_capacity_question(core);
    CHECK(checked == 1);
    end_core(core);
}

static weivel_c_status check_two_wide_constraints(hearing *self, weivel_c_core *core, const weivel_c_event *event) {
    (void)core;
    if (event->kind == WEIVEL_EVENT_CAPACITY_WANTED && self->more != NULL) {
        const weivel_numbers *constraints = &event->data.capacity.constraints;
        uint32_t *checked = self->more;
        CHECK(constraints->count == 2);
        if (constraints->count == 2) {
            CHECK(constraints->items[0].size == sizeof(weivel_number));
            CHECK(constraints->items[1].size == sizeof(weivel_number));
            CHECK(constraints->items != &wider_constraints[0].number);
            CHECK(constraints->items[0].name != wider_constraints[0].number.name);
            check_text(constraints->items[0].name, "first", "the first constraint");
            check_text(constraints->items[1].name, "second", "the second constraint");
            CHECK(constraints->items[0].value == 1.5 && constraints->items[1].value == 2.5);
        }
        *checked += 1;
        self->more = NULL;
    }
    return WEIVEL_C_STATUS_OK;
}

static void steps_through_an_array_at_the_stride_its_first_element_declares(void) {
    hearing listener = {0};
    uint32_t checked = 0;
    weivel_constraint_entry entry;
    weivel_core_options core_options = options_with_a_speed_constraint(&entry);
    weivel_c_core *core;
    forget_crossings();
    memset(wider_constraints, 0, sizeof wider_constraints);
    wider_constraints[0].number = (weivel_number){(uint32_t)sizeof(wider_number), "first", 1.5};
    wider_constraints[0].later = 99.0;
    wider_constraints[1].number = (weivel_number){(uint32_t)sizeof(wider_number), "second", 2.5};
    bend_event = widen_constraints;
    listener.react = check_two_wide_constraints;
    listener.more = &checked;
    core = make_core(&core_options, &listener, NULL);
    ask_a_capacity_question(core);
    CHECK(checked == 1);
    end_core(core);
}

static void a_refused_call_is_answered_with_the_refusal_the_core_queued_for_it(void) {
    hearing listener = {0};
    telling sink = {0};
    weivel_c_refusal refusal = WEIVEL_INIT(weivel_c_refusal);
    weivel_c_core *core = make_core(NULL, &listener, &sink);
    weivel_handle plugin = open_plugin(core, "p", NULL, 0, NULL, 0);
    weivel_handle handle = 7;
    CHECK(weivel_c_core_register_service(core, plugin, "elsewhere:x", &handle, &refusal) ==
          WEIVEL_C_STATUS_REFUSED);
    CHECK(handle == 0);
    CHECK(refusal.code == WEIVEL_REFUSAL_OUTSIDE_NAMESPACE);
    CHECK(refusal.detail != NULL && refusal.detail[0] != '\0');
    CHECK(refusal.fix != NULL && refusal.has_fix == (uint32_t)(refusal.fix[0] != '\0'));
    check_text(refusal.subject, "elsewhere:x", "the refusal's subject");
    CHECK(refusal.storage != NULL);
    CHECK(listener.count == 1 && listener.items[0].kind == WEIVEL_EVENT_REFUSAL);
    CHECK(listener.items[0].code == WEIVEL_REFUSAL_OUTSIDE_NAMESPACE);
    CHECK(weivel_c_core_register_service(core, plugin, "p:fine", &handle, &refusal) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_refusal_release(&refusal) == WEIVEL_C_STATUS_OK);
    CHECK(is_empty_refusal(&refusal));
    CHECK(weivel_c_refusal_release(&refusal) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_refusal_release(NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_core_register_service(core, plugin, "p:fine", &handle, &refusal) == WEIVEL_C_STATUS_OK);
    CHECK(is_empty_refusal(&refusal));
    CHECK(sink.count == 0);
    end_core(core);
}

static void a_refusal_of_reach_is_answered_once_and_its_repeat_with_the_status_alone(void) {
    hearing listener = {0};
    weivel_c_refusal refusal = WEIVEL_INIT(weivel_c_refusal);
    weivel_service_want consumed = want_of("p:a");
    weivel_c_core *core = make_core(NULL, &listener, NULL);
    weivel_handle plugin = open_plugin(core, "c", NULL, 0, &consumed, 1);
    weivel_handle handle = 7;
    CHECK(weivel_c_core_get_service(core, plugin, "p:undeclared", &handle, &refusal) == WEIVEL_C_STATUS_REFUSED);
    CHECK(refusal.code == WEIVEL_REFUSAL_UNDECLARED);
    check_text(refusal.subject, "p:undeclared", "the reach refusal's subject");
    CHECK(refusal.detail != NULL);
    CHECK(count_heard(&listener, WEIVEL_EVENT_REACH_REFUSED) == 1);
    CHECK(weivel_c_refusal_release(&refusal) == WEIVEL_C_STATUS_OK);
    forget_crossings();
    listener.count = 0;
    CHECK(weivel_c_core_get_service(core, plugin, "p:undeclared", &handle, &refusal) == WEIVEL_C_STATUS_REFUSED);
    CHECK(is_empty_refusal(&refusal));
    CHECK(crossed.next_event == 0 && listener.count == 0);
    end_core(core);
}

static void a_late_registration_and_a_late_watch_are_answered_with_the_status_alone(void) {
    static const char *const PROVIDED[] = {"p:late"};
    hearing listener = {0};
    weivel_c_refusal refusal = WEIVEL_INIT(weivel_c_refusal);
    weivel_core_options core_options = WEIVEL_INIT(weivel_core_options);
    weivel_c_core *core;
    weivel_handle plugin;
    weivel_handle handle = 7;
    core_options.minimum_log_level = WEIVEL_LOG_DEBUG;
    core = make_core(&core_options, &listener, NULL);
    plugin = open_plugin(core, "p", PROVIDED, 1, NULL, 0);
    CHECK(weivel_c_core_release_plugin(core, plugin, NULL) == WEIVEL_C_STATUS_OK);
    listener.count = 0;
    CHECK(weivel_c_core_register_service(core, plugin, "p:late", &handle, &refusal) == WEIVEL_C_STATUS_REFUSED);
    CHECK(is_empty_refusal(&refusal));
    CHECK(count_heard(&listener, WEIVEL_EVENT_LOG) >= 1);
    CHECK(count_heard(&listener, WEIVEL_EVENT_REFUSAL) == 0);
    CHECK(weivel_c_core_watch_service(core, plugin, "p:late", &handle, &refusal) == WEIVEL_C_STATUS_REFUSED);
    CHECK(is_empty_refusal(&refusal));
    end_core(core);
}

static void a_handle_that_names_nothing_is_answered_with_the_status_alone(void) {
    hearing listener = {0};
    weivel_c_refusal refusal = WEIVEL_INIT(weivel_c_refusal);
    weivel_c_core *core = make_core(NULL, &listener, NULL);
    uint32_t condition = 7;
    CHECK(weivel_c_core_handle_state(core, 0x123456789u, &condition, &refusal) == WEIVEL_C_STATUS_REFUSED);
    CHECK(is_empty_refusal(&refusal));
    CHECK(condition == 0 && listener.count == 0);
    end_core(core);
}

static void malformed_from_a_call_the_layer_built_is_reported_as_the_layers_defect(void) {
    hearing listener = {0};
    telling sink = {0};
    weivel_c_refusal refusal = WEIVEL_INIT(weivel_c_refusal);
    weivel_c_core *core = make_core(NULL, &listener, &sink);
    uint32_t condition = 0;
    forget_crossings();
    withhold_condition = 1;
    CHECK(weivel_c_core_handle_state(core, 0x123456789u, &condition, &refusal) == WEIVEL_C_STATUS_LAYER_DEFECT);
    CHECK(is_empty_refusal(&refusal));
    CHECK(sink.count == 1);
    CHECK(sink.items[0].failure.kind == WEIVEL_C_FAILURE_LAYER_DEFECT);
    CHECK(sink.items[0].failure.status == WEIVEL_C_STATUS_LAYER_DEFECT);
    CHECK(strstr(sink.items[0].text, "malformed") != NULL);
    CHECK(strstr(sink.items[0].text, "weivel_handle_state") != NULL);
    CHECK(listener.count == 1 && listener.items[0].code == WEIVEL_REFUSAL_MALFORMED_CALL);
    end_core(core);
}

static void malformed_for_a_struct_the_caller_built_is_the_callers_mistake(void) {
    telling sink = {0};
    weivel_c_refusal refusal = WEIVEL_INIT(weivel_c_refusal);
    weivel_manifest manifest = WEIVEL_INIT(weivel_manifest);
    weivel_c_core *core = make_core(NULL, NULL, &sink);
    weivel_handle plugin = 7;
    manifest.size = sizeof manifest.size;
    CHECK(weivel_c_core_register_manifest(core, &manifest, &plugin, &refusal) == WEIVEL_C_STATUS_MISUSE);
    CHECK(refusal.code == WEIVEL_REFUSAL_MALFORMED_CALL);
    CHECK(refusal.detail != NULL && refusal.detail[0] != '\0');
    CHECK(sink.count == 0);
    CHECK(weivel_c_refusal_release(&refusal) == WEIVEL_C_STATUS_OK);
    end_core(core);
}

/** What a listener that calls back in left behind. */
typedef struct reentry {
    int entered;
    uint32_t heard_before_the_call;
    uint32_t heard_after_the_call;
    weivel_c_step step;
    weivel_c_status second_deferral;
    uint32_t heard_when_the_step_ran;
    uint32_t steps_run;
    const hearing *listener;
} reentry;

static int is_first_unregistration(const weivel_c_event *event, reentry *more) {
    return event->kind == WEIVEL_EVENT_WATCH && event->data.watch.change == WEIVEL_CHANGE_UNREGISTERED &&
           !more->entered;
}

static weivel_c_status register_inside_a_listener(hearing *self, weivel_c_core *core, const weivel_c_event *event) {
    reentry *more = self->more;
    weivel_handle handle = 0;
    if (is_first_unregistration(event, more)) {
        more->entered = 1;
        more->heard_before_the_call = self->count;
        CHECK(weivel_c_core_register_service(core, WEIVEL_HOST, "c", &handle, NULL) == WEIVEL_C_STATUS_OK);
        more->heard_after_the_call = self->count;
    }
    return WEIVEL_C_STATUS_OK;
}

static void a_call_inside_a_listener_joins_the_end_and_never_re_enters_the_dispatcher(void) {
    hearing listener = {0};
    reentry more = {0};
    weivel_c_core *core = make_core(NULL, &listener, NULL);
    weivel_handle plugin = open_a_watched_provider(core);
    uint64_t first_sequence;
    listener.count = 0;
    listener.react = register_inside_a_listener;
    listener.more = &more;
    CHECK(weivel_c_core_release_plugin(core, plugin, NULL) == WEIVEL_C_STATUS_OK);
    first_sequence = listener.items[0].sequence;
    CHECK(more.entered == 1);
    CHECK(more.heard_after_the_call == more.heard_before_the_call);
    CHECK(listener.deepest == 1);
    CHECK(is_in_drained_order(&listener, first_sequence));
    CHECK(count_heard(&listener, WEIVEL_EVENT_WATCH) == 3);
    CHECK(listener.items[listener.count - 1].kind == WEIVEL_EVENT_WATCH);
    CHECK(listener.items[listener.count - 1].change == WEIVEL_CHANGE_REGISTERED);
    end_core(core);
}

static weivel_c_status count_what_was_heard(void *data, weivel_c_core *core) {
    reentry *more = data;
    (void)core;
    more->heard_when_the_step_ran = more->listener->count;
    more->steps_run++;
    return WEIVEL_C_STATUS_OK;
}

static weivel_c_status defer_inside_a_listener(hearing *self, weivel_c_core *core, const weivel_c_event *event) {
    reentry *more = self->more;
    weivel_handle handle = 0;
    if (is_first_unregistration(event, more)) {
        more->entered = 1;
        CHECK(weivel_c_core_defer(core, &more->step, NULL) == WEIVEL_C_STATUS_OK);
        more->second_deferral = weivel_c_core_defer(core, &more->step, NULL);
        CHECK(more->steps_run == 0);
        CHECK(weivel_c_core_register_service(core, WEIVEL_HOST, "c", &handle, NULL) == WEIVEL_C_STATUS_OK);
    }
    return WEIVEL_C_STATUS_OK;
}

static void a_deferred_step_runs_in_queue_order_behind_what_was_already_drained(void) {
    hearing listener = {0};
    reentry more = {0};
    weivel_c_core *core = make_core(NULL, &listener, NULL);
    weivel_handle plugin = open_a_watched_provider(core);
    uint32_t drained_by_the_release;
    more.step = (weivel_c_step)WEIVEL_INIT(weivel_c_step);
    more.step.run = count_what_was_heard;
    more.step.data = &more;
    more.listener = &listener;
    listener.count = 0;
    listener.react = defer_inside_a_listener;
    listener.more = &more;
    forget_crossings();
    CHECK(weivel_c_core_release_plugin(core, plugin, NULL) == WEIVEL_C_STATUS_OK);
    drained_by_the_release = crossed.events - 1;
    CHECK(more.steps_run == 1);
    CHECK(more.second_deferral == WEIVEL_C_STATUS_MISUSE);
    CHECK(drained_by_the_release >= 2);
    CHECK(more.heard_when_the_step_ran == drained_by_the_release);
    CHECK(listener.count == drained_by_the_release + 1);
    CHECK(more.step.place.waiting == 0);
    end_core(core);
}

/** What a step that calls back in saw of its own call's events. */
typedef struct calling_step {
    const hearing *listener;
    uint32_t heard_when_the_call_returned;
    uint32_t runs;
} calling_step;

static weivel_c_status register_inside_a_step(void *data, weivel_c_core *core) {
    calling_step *self = data;
    weivel_handle handle = 0;
    CHECK(weivel_c_core_register_service(core, WEIVEL_HOST, "c", &handle, NULL) == WEIVEL_C_STATUS_OK);
    self->heard_when_the_call_returned = self->listener->count;
    self->runs++;
    return WEIVEL_C_STATUS_OK;
}

static void a_step_runs_as_the_dispatcher_so_a_call_inside_it_joins_the_end(void) {
    hearing listener = {0};
    calling_step record = {0};
    weivel_c_step step = WEIVEL_INIT(weivel_c_step);
    weivel_handle handle = 0;
    weivel_c_core *core = make_core(NULL, &listener, NULL);
    CHECK(weivel_c_core_watch_service(core, WEIVEL_HOST, "c", &handle, NULL) == WEIVEL_C_STATUS_OK);
    record.listener = &listener;
    step.run = register_inside_a_step;
    step.data = &record;
    CHECK(weivel_c_core_defer(core, &step, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(record.runs == 1);
    CHECK(record.heard_when_the_call_returned == 0);
    CHECK(listener.count == 1 && listener.deepest == 1);
    end_core(core);
}

static void a_step_deferred_while_no_dispatch_runs_has_run_when_the_call_returns(void) {
    hearing listener = {0};
    reentry more = {0};
    weivel_c_step small = WEIVEL_INIT(weivel_c_step);
    weivel_c_core *core = make_core(NULL, &listener, NULL);
    more.step = (weivel_c_step)WEIVEL_INIT(weivel_c_step);
    more.step.run = count_what_was_heard;
    more.step.data = &more;
    more.listener = &listener;
    CHECK(weivel_c_core_defer(core, &more.step, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(more.steps_run == 1);
    CHECK(weivel_c_core_defer(core, &more.step, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(more.steps_run == 2);
    CHECK(weivel_c_core_defer(core, NULL, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_core_defer(core, &small, NULL) == WEIVEL_C_STATUS_MISUSE);
    small.run = count_what_was_heard;
    small.size -= 1;
    CHECK(weivel_c_core_defer(core, &small, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(more.steps_run == 2);
    end_core(core);
}

static weivel_c_status fail_on_a_watch(hearing *self, weivel_c_core *core, const weivel_c_event *event) {
    (void)self;
    (void)core;
    return event->kind == WEIVEL_EVENT_WATCH ? WEIVEL_C_STATUS_FAILED : WEIVEL_C_STATUS_OK;
}

static weivel_c_status fail_as_out_of_memory(void *data, weivel_c_core *core) {
    (void)data;
    (void)core;
    return WEIVEL_C_STATUS_OUT_OF_MEMORY;
}

static void a_failing_listener_goes_to_the_sink_and_every_later_event_is_still_heard(void) {
    hearing failing = {0};
    hearing other = {0};
    telling sink = {0};
    weivel_c_listener second = {hear, &other};
    weivel_c_step step = WEIVEL_INIT(weivel_c_step);
    uint64_t registration = 0;
    weivel_c_core *core = make_core(NULL, &failing, &sink);
    weivel_handle plugin;
    CHECK(weivel_c_core_add_listener(core, &second, &registration) == WEIVEL_C_STATUS_OK);
    plugin = open_a_watched_provider(core);
    failing.count = 0;
    other.count = 0;
    failing.react = fail_on_a_watch;
    forget_crossings();
    CHECK(weivel_c_core_release_plugin(core, plugin, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(sink.count == 2);
    CHECK(sink.items[0].failure.kind == WEIVEL_C_FAILURE_LISTENER);
    CHECK(sink.items[0].failure.status == WEIVEL_C_STATUS_FAILED);
    CHECK(sink.items[0].failure.event_kind == WEIVEL_EVENT_WATCH);
    CHECK(sink.items[0].failure.sequence != 0 && sink.items[0].failure.sequence < sink.items[1].failure.sequence);
    CHECK(failing.count == crossed.events && other.count == crossed.events);
    step.run = fail_as_out_of_memory;
    CHECK(weivel_c_core_defer(core, &step, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(sink.count == 3);
    CHECK(sink.items[2].failure.kind == WEIVEL_C_FAILURE_STEP);
    CHECK(sink.items[2].failure.status == WEIVEL_C_STATUS_OUT_OF_MEMORY);
    CHECK(sink.items[2].failure.event_kind == WEIVEL_EVENT_NONE);
    end_core(core);
}

static weivel_c_status add_a_listener_then_leave(hearing *self, weivel_c_core *core, const weivel_c_event *event) {
    weivel_c_listener added = {hear, self->more};
    uint64_t registration = 0;
    (void)event;
    if (self->more != NULL) {
        CHECK(weivel_c_core_add_listener(core, &added, &registration) == WEIVEL_C_STATUS_OK);
        self->more = NULL;
    }
    return WEIVEL_C_STATUS_OK;
}

static void listeners_hear_in_the_order_added_until_removed(void) {
    hearing first = {0};
    hearing second = {0};
    hearing late = {0};
    weivel_c_listener listener = {hear, &first};
    weivel_c_listener without = {NULL, NULL};
    uint64_t first_registration = 0;
    uint64_t second_registration = 0;
    weivel_handle handle = 0;
    weivel_c_core *core = make_core(NULL, NULL, NULL);
    CHECK(weivel_c_core_add_listener(core, &listener, &first_registration) == WEIVEL_C_STATUS_OK);
    listener.data = &second;
    CHECK(weivel_c_core_add_listener(core, &listener, &second_registration) == WEIVEL_C_STATUS_OK);
    CHECK(first_registration != 0 && second_registration != 0 && first_registration != second_registration);
    CHECK(weivel_c_core_add_listener(core, &without, &handle) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_core_add_listener(core, &listener, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_core_watch_service(core, WEIVEL_HOST, "c", &handle, NULL) == WEIVEL_C_STATUS_OK);
    first.react = add_a_listener_then_leave;
    first.more = &late;
    CHECK(weivel_c_core_register_service(core, WEIVEL_HOST, "c", &handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(first.count == 1 && second.count == 1 && late.count == 1);
    CHECK(weivel_c_core_remove_listener(core, first_registration) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_remove_listener(core, first_registration) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_core_unregister(core, handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(first.count == 1);
    CHECK(second.count >= 2 && late.count == second.count);
    end_core(core);
}

/** A timer's stand-in: a clock the test sets, and a record of each deadline it was told to follow. */
typedef struct clockwork {
    uint32_t follows;
    int64_t followed_at;
    uint32_t followed_set;
    uint32_t clock_reads;
    int64_t now;
    weivel_c_status answer;
    /** When set, each hook takes this core's layer lock, which waits for ever if the hook already runs under it. */
    weivel_c_core *locked_from_inside;
    uint32_t locks_taken;
} clockwork;

static void take_the_layer_lock(clockwork *self) {
    if (self->locked_from_inside != NULL) {
        CHECK(weivel_c_core_remove_listener(self->locked_from_inside, UINT64_MAX) == WEIVEL_C_STATUS_MISUSE);
        self->locks_taken++;
    }
}

static weivel_c_status follow(void *data, int64_t at, uint32_t set) {
    clockwork *self = data;
    self->follows++;
    self->followed_at = at;
    self->followed_set = set;
    take_the_layer_lock(self);
    return self->answer;
}

static int64_t read_clock(void *data) {
    clockwork *self = data;
    self->clock_reads++;
    take_the_layer_lock(self);
    return self->now;
}

static weivel_c_deadline_hooks hooks_of(clockwork *self) {
    weivel_c_deadline_hooks hooks = WEIVEL_INIT(weivel_c_deadline_hooks);
    hooks.now = read_clock;
    hooks.follow = follow;
    hooks.data = self;
    return hooks;
}

static weivel_handle want_for_a_hundred_milliseconds(weivel_c_core *core, weivel_handle consumer) {
    weivel_want_options options = WEIVEL_INIT(weivel_want_options);
    weivel_handle want = 0;
    options.timed = WEIVEL_CHOICE_YES;
    options.now = 0;
    options.timeout_millis = 100;
    CHECK(weivel_c_core_want_service(core, consumer, "p:a", &options, &want, NULL) == WEIVEL_C_STATUS_OK);
    return want;
}

/** What a listener that makes a timed want saw of the deadline hook around its own call. */
typedef struct drain_time {
    clockwork *clock;
    weivel_handle consumer;
    uint32_t follows_after_the_call;
    uint32_t deadlines_heard_by_then;
    int wanted;
} drain_time;

static weivel_c_status want_inside_a_listener(hearing *self, weivel_c_core *core, const weivel_c_event *event) {
    drain_time *more = self->more;
    if (event->kind == WEIVEL_EVENT_WATCH && !more->wanted) {
        more->wanted = 1;
        want_for_a_hundred_milliseconds(core, more->consumer);
        more->follows_after_the_call = more->clock->follows;
        more->deadlines_heard_by_then = count_heard(self, WEIVEL_EVENT_DEADLINE);
    }
    return WEIVEL_C_STATUS_OK;
}

static void a_deadline_is_followed_after_its_drain_and_before_its_dispatch(void) {
    hearing listener = {0};
    clockwork clock = {0};
    drain_time more = {0};
    weivel_c_deadline_hooks hooks = hooks_of(&clock);
    weivel_service_want consumed = want_of("p:a");
    weivel_handle handle = 0;
    weivel_c_core *core = make_core(NULL, &listener, NULL);
    more.clock = &clock;
    more.consumer = open_plugin(core, "c", NULL, 0, &consumed, 1);
    listener.react = want_inside_a_listener;
    listener.more = &more;
    CHECK(weivel_c_core_set_deadline_hooks(core, &hooks) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_watch_service(core, WEIVEL_HOST, "c", &handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(clock.follows == 0 && clock.clock_reads == 0);
    CHECK(weivel_c_core_register_service(core, WEIVEL_HOST, "c", &handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(more.wanted == 1);
    CHECK(more.follows_after_the_call == 1);
    CHECK(more.deadlines_heard_by_then == 0);
    CHECK(clock.followed_set == 1 && clock.followed_at == 100);
    CHECK(count_heard(&listener, WEIVEL_EVENT_DEADLINE) == 1);
    CHECK(weivel_c_core_set_deadline_hooks(core, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_unregister(core, handle, NULL) == WEIVEL_C_STATUS_OK);
    hooks.size -= 1;
    CHECK(weivel_c_core_set_deadline_hooks(core, &hooks) == WEIVEL_C_STATUS_MISUSE);
    end_core(core);
}

/** What a listener saw of the registration a due deadline came before. */
typedef struct order_of_time {
    weivel_c_status found_when_timed_out;
    uint32_t timed_out;
} order_of_time;

static weivel_c_status look_for_the_service_on_a_time_out(hearing *self, weivel_c_core *core,
                                                          const weivel_c_event *event) {
    order_of_time *more = self->more;
    weivel_handle found = 0;
    if (event->kind == WEIVEL_EVENT_WANT_SETTLED && event->data.want.refusal.code == WEIVEL_REFUSAL_TIMED_OUT) {
        more->timed_out++;
        more->found_when_timed_out = weivel_c_core_get_service(core, WEIVEL_HOST, "p:a", &found, NULL);
    }
    return WEIVEL_C_STATUS_OK;
}

static void a_due_deadline_is_handed_to_the_core_before_the_call_and_never_checked_again(void) {
    static const char *const PROVIDED[] = {"p:a"};
    hearing listener = {0};
    clockwork clock = {0};
    order_of_time more = {WEIVEL_C_STATUS_FAILED, 0};
    weivel_c_deadline_hooks hooks = hooks_of(&clock);
    weivel_service_want consumed = want_of("p:a");
    weivel_c_core *core = make_core(NULL, &listener, NULL);
    weivel_handle provider = open_plugin(core, "p", PROVIDED, 1, NULL, 0);
    weivel_handle consumer = open_plugin(core, "c", NULL, 0, &consumed, 1);
    weivel_handle handle = 0;
    CHECK(weivel_c_core_set_deadline_hooks(core, &hooks) == WEIVEL_C_STATUS_OK);
    want_for_a_hundred_milliseconds(core, consumer);
    CHECK(clock.clock_reads == 0);
    listener.count = 0;
    listener.react = look_for_the_service_on_a_time_out;
    listener.more = &more;
    forget_crossings();
    clock.now = 150;
    CHECK(weivel_c_core_register_service(core, provider, "p:a", &handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(clock.clock_reads == 1);
    CHECK(crossed.advance == 1 && crossed.advanced_to == 150);
    CHECK(count_heard(&listener, WEIVEL_EVENT_WANT_SETTLED) == 1);
    CHECK(listener.items[0].kind == WEIVEL_EVENT_WANT_SETTLED);
    CHECK(listener.items[0].code == WEIVEL_REFUSAL_TIMED_OUT && listener.items[0].implementation == 0);
    CHECK(more.timed_out == 1 && more.found_when_timed_out == WEIVEL_C_STATUS_NONE);
    CHECK(clock.followed_set == 0);
    CHECK(weivel_c_core_get_service(core, WEIVEL_HOST, "p:a", &handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(clock.clock_reads == 1);
    end_core(core);
}

static void an_advance_the_caller_makes_is_not_preceded_by_the_check(void) {
    clockwork clock = {0};
    weivel_c_deadline_hooks hooks = hooks_of(&clock);
    weivel_service_want consumed = want_of("p:a");
    weivel_c_core *core = make_core(NULL, NULL, NULL);
    weivel_handle consumer = open_plugin(core, "c", NULL, 0, &consumed, 1);
    CHECK(weivel_c_core_set_deadline_hooks(core, &hooks) == WEIVEL_C_STATUS_OK);
    want_for_a_hundred_milliseconds(core, consumer);
    forget_crossings();
    clock.now = 150;
    CHECK(weivel_c_core_advance(core, 50, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(clock.clock_reads == 0);
    CHECK(crossed.advance == 1 && crossed.advanced_to == 50);
    end_core(core);
}

static void a_failing_deadline_hook_goes_to_the_sink_and_the_call_is_still_made(void) {
    telling sink = {0};
    clockwork clock = {0};
    weivel_c_deadline_hooks hooks = hooks_of(&clock);
    weivel_service_want consumed = want_of("p:a");
    weivel_c_core *core = make_core(NULL, NULL, &sink);
    weivel_handle consumer = open_plugin(core, "c", NULL, 0, &consumed, 1);
    weivel_handle want;
    uint32_t condition = 0;
    clock.answer = WEIVEL_C_STATUS_FAILED;
    CHECK(weivel_c_core_set_deadline_hooks(core, &hooks) == WEIVEL_C_STATUS_OK);
    want = want_for_a_hundred_milliseconds(core, consumer);
    CHECK(clock.follows == 1 && sink.count == 1);
    CHECK(sink.items[0].failure.kind == WEIVEL_C_FAILURE_DEADLINE_HOOK);
    CHECK(sink.items[0].failure.status == WEIVEL_C_STATUS_FAILED);
    CHECK(weivel_c_core_handle_state(core, want, &condition, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(condition == WEIVEL_HANDLE_WAITING);
    end_core(core);
}

static void the_deadline_hooks_run_outside_the_layer_lock(void) {
    static const char *const PROVIDED[] = {"p:a"};
    clockwork clock = {0};
    weivel_c_deadline_hooks hooks = hooks_of(&clock);
    weivel_service_want consumed = want_of("p:a");
    weivel_c_core *core = make_core(NULL, NULL, NULL);
    weivel_handle provider = open_plugin(core, "p", PROVIDED, 1, NULL, 0);
    weivel_handle consumer = open_plugin(core, "c", NULL, 0, &consumed, 1);
    weivel_handle handle = 0;
    clock.locked_from_inside = core;
    CHECK(weivel_c_core_set_deadline_hooks(core, &hooks) == WEIVEL_C_STATUS_OK);
    want_for_a_hundred_milliseconds(core, consumer);
    CHECK(clock.follows == 1 && clock.locks_taken == 1);
    clock.now = 150;
    CHECK(weivel_c_core_register_service(core, provider, "p:a", &handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(clock.clock_reads == 1 && clock.follows == 2 && clock.locks_taken == 3);
    end_core(core);
}

static void hooks_set_while_a_deadline_is_recorded_are_told_it_at_once(void) {
    clockwork clock = {0};
    clockwork later = {0};
    weivel_c_deadline_hooks hooks = hooks_of(&clock);
    weivel_service_want consumed = want_of("p:a");
    weivel_c_core *core = make_core(NULL, NULL, NULL);
    weivel_handle consumer = open_plugin(core, "c", NULL, 0, &consumed, 1);
    CHECK(weivel_c_core_set_deadline_hooks(core, &hooks) == WEIVEL_C_STATUS_OK);
    CHECK(clock.follows == 0);
    CHECK(weivel_c_core_set_deadline_hooks(core, NULL) == WEIVEL_C_STATUS_OK);
    want_for_a_hundred_milliseconds(core, consumer);
    CHECK(clock.follows == 0);
    hooks = hooks_of(&later);
    CHECK(weivel_c_core_set_deadline_hooks(core, &hooks) == WEIVEL_C_STATUS_OK);
    CHECK(later.follows == 1 && later.followed_set == 1 && later.followed_at == 100);
    end_core(core);
}

static void bend_a_watch_to_an_unknown_kind(weivel_event *event) {
    if (event->kind == WEIVEL_EVENT_WATCH && kind_before_bending == 0) {
        kind_before_bending = event->kind;
        event->kind = 4000;
        bend_event = NULL;
    }
}

static void an_event_of_a_kind_the_layer_does_not_know_is_released_and_never_heard(void) {
    hearing listener = {0};
    telling sink = {0};
    weivel_handle handle = 0;
    uint32_t drained_by_the_unregistration;
    weivel_c_core *core = make_core(NULL, &listener, &sink);
    CHECK(weivel_c_core_watch_service(core, WEIVEL_HOST, "c", &handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_register_service(core, WEIVEL_HOST, "c", &handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(listener.count == 1);
    forget_crossings();
    bend_event = bend_a_watch_to_an_unknown_kind;
    CHECK(weivel_c_core_unregister(core, handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(crossed.events >= 1 && crossed.event_release == crossed.events && crossed.unreleased == 0);
    CHECK(count_heard(&listener, 4000) == 0 && count_heard(&listener, WEIVEL_EVENT_WATCH) == 1);
    CHECK(listener.count == crossed.events);
    CHECK(sink.count == 0);
    drained_by_the_unregistration = crossed.events;
    CHECK(weivel_c_core_register_service(core, WEIVEL_HOST, "c", &handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(listener.items[listener.count - 1].kind == WEIVEL_EVENT_WATCH);
    CHECK(listener.items[listener.count - 1].sequence ==
          listener.items[0].sequence + 1 + drained_by_the_unregistration);
    end_core(core);
}

static void each_defect_of_one_call_keeps_its_own_text(void) {
    telling sink = {0};
    weivel_c_core *core = make_core(NULL, NULL, &sink);
    uint32_t condition = 0;
    forget_crossings();
    withhold_condition = 1;
    fail_event_release = 1;
    CHECK(weivel_c_core_handle_state(core, 0x123456789u, &condition, NULL) == WEIVEL_C_STATUS_LAYER_DEFECT);
    fail_event_release = 0;
    CHECK(sink.count == 2);
    CHECK(sink.items[0].failure.kind == WEIVEL_C_FAILURE_LAYER_DEFECT);
    CHECK(sink.items[1].failure.kind == WEIVEL_C_FAILURE_LAYER_DEFECT);
    CHECK(strstr(sink.items[0].text, "release") != NULL && strstr(sink.items[0].text, "malformed") == NULL);
    CHECK(strstr(sink.items[1].text, "malformed") != NULL);
    end_core(core);
}

static void a_drain_the_core_refuses_midway_is_a_defect_and_the_rest_is_drained_by_the_next_call(void) {
    hearing listener = {0};
    telling sink = {0};
    weivel_c_core *core = make_core(NULL, &listener, &sink);
    weivel_handle plugin = open_a_watched_provider(core);
    weivel_handle watch = 0;
    uint32_t heard_by_the_release;
    listener.count = 0;
    forget_crossings();
    fail_next_event_at = 2;
    CHECK(weivel_c_core_release_plugin(core, plugin, NULL) == WEIVEL_C_STATUS_OK);
    heard_by_the_release = listener.count;
    CHECK(heard_by_the_release == 1 && crossed.events == 1);
    CHECK(sink.count == 1 && sink.items[0].failure.kind == WEIVEL_C_FAILURE_LAYER_DEFECT);
    CHECK(strstr(sink.items[0].text, "not drained") != NULL);
    CHECK(weivel_c_core_watch_service(core, WEIVEL_HOST, "elsewhere", &watch, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(listener.count > heard_by_the_release && listener.count == crossed.events);
    CHECK(count_heard(&listener, WEIVEL_EVENT_WATCH) == 2);
    CHECK(sink.count == 1);
    end_core(core);
}

static void set_an_unreadable_stride(weivel_event *event) {
    static weivel_number unreadable;
    if (event->kind == WEIVEL_EVENT_CAPACITY_WANTED) {
        unreadable.size = 1;
        event->data.capacity.constraints.items = &unreadable;
        event->data.capacity.constraints.count = 1;
    }
}

static weivel_c_status count_incomplete_questions(hearing *self, weivel_c_core *core, const weivel_c_event *event) {
    uint32_t *found = self->more;
    (void)core;
    if (event->kind == WEIVEL_EVENT_CAPACITY_WANTED) {
        CHECK(event->incomplete == 1);
        CHECK(event->data.capacity.constraints.count == 0 && event->data.capacity.constraints.items == NULL);
        CHECK(event->data.capacity.registration != 0);
        *found += 1;
    }
    return WEIVEL_C_STATUS_OK;
}

static void an_array_whose_stride_cannot_be_read_leaves_its_event_incomplete(void) {
    hearing listener = {0};
    telling sink = {0};
    uint32_t found = 0;
    weivel_constraint_entry entry;
    weivel_core_options core_options = options_with_a_speed_constraint(&entry);
    weivel_c_core *core;
    forget_crossings();
    bend_event = set_an_unreadable_stride;
    listener.react = count_incomplete_questions;
    listener.more = &found;
    core = make_core(&core_options, &listener, &sink);
    ask_a_capacity_question(core);
    CHECK(found == 1);
    CHECK(sink.count == 1 && sink.items[0].failure.kind == WEIVEL_C_FAILURE_EVENT_INCOMPLETE);
    CHECK(sink.items[0].failure.event_kind == WEIVEL_EVENT_CAPACITY_WANTED);
    end_core(core);
}

/** What a listener that defers a step learned about where it would run. */
typedef struct deferral {
    weivel_c_step step;
    uint32_t runs;
    uint32_t dispatched_here_inside;
    int deferred;
} deferral;

static weivel_c_status count_a_run(void *data, weivel_c_core *core) {
    deferral *self = data;
    (void)core;
    self->runs++;
    return WEIVEL_C_STATUS_OK;
}

static weivel_c_status defer_once_inside_a_listener(hearing *self, weivel_c_core *core, const weivel_c_event *event) {
    deferral *more = self->more;
    (void)event;
    if (!more->deferred) {
        more->deferred = 1;
        more->dispatched_here_inside = 7;
        CHECK(weivel_c_core_defer(core, &more->step, &more->dispatched_here_inside) == WEIVEL_C_STATUS_OK);
        CHECK(more->runs == 1);
    }
    return WEIVEL_C_STATUS_OK;
}

static void a_deferral_says_whether_the_step_was_dispatched_before_it_returned(void) {
    hearing listener = {0};
    deferral more = {0};
    uint32_t dispatched_here = 7;
    weivel_handle handle = 0;
    weivel_c_core *core = make_core(NULL, &listener, NULL);
    more.step = (weivel_c_step)WEIVEL_INIT(weivel_c_step);
    more.step.run = count_a_run;
    more.step.data = &more;
    CHECK(weivel_c_core_defer(core, &more.step, &dispatched_here) == WEIVEL_C_STATUS_OK);
    CHECK(dispatched_here == 1 && more.runs == 1);
    CHECK(weivel_c_core_defer(core, NULL, &dispatched_here) == WEIVEL_C_STATUS_MISUSE);
    CHECK(dispatched_here == 0);
    listener.react = defer_once_inside_a_listener;
    listener.more = &more;
    CHECK(weivel_c_core_watch_service(core, WEIVEL_HOST, "c", &handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_register_service(core, WEIVEL_HOST, "c", &handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(more.deferred == 1 && more.dispatched_here_inside == 0 && more.runs == 2);
    end_core(core);
}

static weivel_c_status destroy_the_core(void *data, weivel_c_core *core) {
    uint32_t *runs = data;
    *runs += 1;
    CHECK(weivel_c_core_destroy(core) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_free(core) == WEIVEL_C_STATUS_MISUSE);
    return WEIVEL_C_STATUS_OK;
}

static weivel_c_status defer_a_destroy_and_a_step_behind_it(hearing *self, weivel_c_core *core,
                                                            const weivel_c_event *event) {
    weivel_c_step *steps = self->more;
    (void)event;
    if (self->count == 1) {
        CHECK(weivel_c_core_defer(core, &steps[0], NULL) == WEIVEL_C_STATUS_OK);
        CHECK(weivel_c_core_defer(core, &steps[1], NULL) == WEIVEL_C_STATUS_OK);
    }
    return WEIVEL_C_STATUS_OK;
}

static void destroy_inside_a_step_drops_the_steps_behind_it(void) {
    hearing listener = {0};
    weivel_c_step steps[2] = {WEIVEL_INIT(weivel_c_step), WEIVEL_INIT(weivel_c_step)};
    uint32_t destroys = 0;
    deferral behind = {0};
    weivel_handle handle = 0;
    weivel_c_core *core = make_core(NULL, &listener, NULL);
    steps[0].run = destroy_the_core;
    steps[0].data = &destroys;
    steps[1].run = count_a_run;
    steps[1].data = &behind;
    listener.react = defer_a_destroy_and_a_step_behind_it;
    listener.more = steps;
    CHECK(weivel_c_core_watch_service(core, WEIVEL_HOST, "c", &handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_register_service(core, WEIVEL_HOST, "c", &handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(destroys == 1 && behind.runs == 0);
    CHECK(steps[1].place.waiting == 0);
    CHECK(weivel_c_core_free(core) == WEIVEL_C_STATUS_OK);
}

/** What a listener that destroys its core tried afterwards. */
typedef struct ending {
    weivel_c_status destroyed;
    weivel_c_status freed_inside;
    weivel_c_status called_after;
    weivel_c_status deferred_after;
    weivel_c_step waiting_step;
    reentry step_record;
} ending;

static weivel_c_status destroy_on_the_first_event(hearing *self, weivel_c_core *core, const weivel_c_event *event) {
    ending *more = self->more;
    uint32_t condition = 0;
    (void)event;
    if (self->count == 1) {
        CHECK(weivel_c_core_defer(core, &more->waiting_step, NULL) == WEIVEL_C_STATUS_OK);
        CHECK(more->waiting_step.place.waiting != 0);
        more->destroyed = weivel_c_core_destroy(core);
        more->freed_inside = weivel_c_core_free(core);
        more->called_after = weivel_c_core_handle_state(core, 1, &condition, NULL);
        more->deferred_after = weivel_c_core_defer(core, &more->waiting_step, NULL);
    }
    return WEIVEL_C_STATUS_OK;
}

static void destroy_drops_what_was_not_dispatched_and_nothing_is_dispatched_after_it(void) {
    hearing destroying = {0};
    hearing other = {0};
    ending more;
    weivel_c_listener second = {hear, &other};
    uint64_t registration = 0;
    weivel_c_core *core = make_core(NULL, &destroying, NULL);
    weivel_handle plugin;
    memset(&more, 0, sizeof more);
    more.destroyed = WEIVEL_C_STATUS_FAILED;
    more.waiting_step = (weivel_c_step)WEIVEL_INIT(weivel_c_step);
    more.waiting_step.run = count_what_was_heard;
    more.waiting_step.data = &more.step_record;
    more.step_record.listener = &other;
    CHECK(weivel_c_core_add_listener(core, &second, &registration) == WEIVEL_C_STATUS_OK);
    plugin = open_a_watched_provider(core);
    destroying.count = 0;
    other.count = 0;
    destroying.react = destroy_on_the_first_event;
    destroying.more = &more;
    forget_crossings();
    CHECK(weivel_c_core_release_plugin(core, plugin, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(crossed.events >= 2);
    CHECK(destroying.count == 1 && other.count == 0);
    CHECK(more.destroyed == WEIVEL_C_STATUS_OK);
    CHECK(more.freed_inside == WEIVEL_C_STATUS_MISUSE);
    CHECK(more.called_after == WEIVEL_C_STATUS_DESTROYED);
    CHECK(more.deferred_after == WEIVEL_C_STATUS_DESTROYED);
    CHECK(more.step_record.steps_run == 0 && more.waiting_step.place.waiting == 0);
    CHECK(crossed.core_destroy == 1 && crossed.unreleased == 0);
    CHECK(weivel_c_core_release_plugin(core, plugin, NULL) == WEIVEL_C_STATUS_DESTROYED);
    CHECK(weivel_c_core_add_listener(core, &second, &registration) == WEIVEL_C_STATUS_DESTROYED);
    CHECK(weivel_c_core_remove_listener(core, registration) == WEIVEL_C_STATUS_DESTROYED);
    CHECK(weivel_c_core_set_deadline_hooks(core, NULL) == WEIVEL_C_STATUS_DESTROYED);
    CHECK(weivel_c_core_destroy(core) == WEIVEL_C_STATUS_OK);
    CHECK(crossed.core_destroy == 1);
    CHECK(weivel_c_core_free(core) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_destroy(NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_core_free(NULL) == WEIVEL_C_STATUS_OK);
}

static void a_core_that_is_not_destroyed_is_not_freed(void) {
    weivel_c_core *core = make_core(NULL, NULL, NULL);
    CHECK(weivel_c_core_free(core) == WEIVEL_C_STATUS_MISUSE);
    end_core(core);
}

static void refuses_a_misused_creation_and_creates_nothing(void) {
    weivel_c_core_options options = WEIVEL_INIT(weivel_c_core_options);
    weivel_c_core_table empty = WEIVEL_INIT(weivel_c_core_table);
    weivel_c_core_table small = counting_table;
    weivel_core_options core_options = WEIVEL_INIT(weivel_core_options);
    weivel_c_core *core = (weivel_c_core *)&options;
    forget_crossings();
    options.table = &counting_table;
    CHECK(weivel_c_core_create(NULL, &core) == WEIVEL_C_STATUS_MISUSE);
    CHECK(core == NULL);
    CHECK(weivel_c_core_create(&options, NULL) == WEIVEL_C_STATUS_MISUSE);
    options.table = NULL;
    CHECK(weivel_c_core_create(&options, &core) == WEIVEL_C_STATUS_MISUSE);
    options.table = &empty;
    CHECK(weivel_c_core_create(&options, &core) == WEIVEL_C_STATUS_MISUSE);
    small.size -= 1;
    options.table = &small;
    CHECK(weivel_c_core_create(&options, &core) == WEIVEL_C_STATUS_MISUSE);
    options.table = &counting_table;
    options.size -= 1;
    CHECK(weivel_c_core_create(&options, &core) == WEIVEL_C_STATUS_MISUSE);
    CHECK(crossed.core_create == 0);
    options.size += 1;
    core_options.initial_event_capacity = WEIVEL_CORE_MAXIMUM_INITIAL_EVENT_CAPACITY + 1;
    options.core_options = &core_options;
    CHECK(weivel_c_core_create(&options, &core) == WEIVEL_C_STATUS_MISUSE);
    CHECK(core == NULL && crossed.core_create == 1);
}

#if !defined(WEIVEL_C_USING_SHARED)

/**
 * An allocator that counts what it holds and fails the allocation it is told to, and every allocation of
 * `refuse_from_size` bytes or more while that is not 0.
 */
typedef struct counting_memory {
    uint32_t allocations;
    uint32_t fail_at;
    int32_t held;
    size_t refuse_from_size;
} counting_memory;

static void *allocate_counted(void *data, size_t size) {
    counting_memory *self = data;
    self->allocations++;
    if (self->allocations == self->fail_at || (self->refuse_from_size != 0 && size >= self->refuse_from_size)) {
        return NULL;
    }
    self->held++;
    return malloc(size);
}

static void release_counted(void *data, void *memory) {
    counting_memory *self = data;
    self->held--;
    free(memory);
}

static weivel_c_core *make_core_with_options_on(counting_memory *memory, const weivel_core_options *core_options,
                                                hearing *listener, telling *sink) {
    weivel_c_allocator allocator = {allocate_counted, release_counted, memory};
    weivel_c_core *core = make_core(core_options, NULL, sink);
    weivel_c_listener heard_by = {hear, listener};
    uint64_t registration = 0;
    weivel_c_core_replace_allocator(core, &allocator);
    CHECK(weivel_c_core_add_listener(core, &heard_by, &registration) == WEIVEL_C_STATUS_OK);
    return core;
}

static weivel_c_core *make_core_on(counting_memory *memory, hearing *listener, telling *sink) {
    return make_core_with_options_on(memory, NULL, listener, sink);
}

/** How many failures of `kind` about an event of `event_kind` a sink was told. */
static uint32_t count_told(const telling *sink, uint32_t kind, uint32_t event_kind) {
    uint32_t found = 0;
    uint32_t index;
    for (index = 0; index < sink->count && index < 16; index++) {
        found += sink->items[index].failure.kind == kind && sink->items[index].failure.event_kind == event_kind;
    }
    return found;
}

/** What a listener saw of the one event its test starves of memory. */
typedef struct starved {
    weivel_handle want;
    weivel_handle plugin;
    uint32_t checked;
} starved;

static weivel_c_status check_a_settlement_without_its_texts(hearing *self, weivel_c_core *core,
                                                            const weivel_c_event *event) {
    starved *more = self->more;
    (void)core;
    if (event->kind == WEIVEL_EVENT_WANT_SETTLED) {
        const weivel_want_event *settled = &event->data.want;
        CHECK(event->incomplete == 1);
        CHECK(settled->want == more->want && settled->plugin == more->plugin && settled->implementation == 0);
        CHECK(settled->refusal.code == WEIVEL_REFUSAL_TIMED_OUT && settled->has_fix == 1);
        check_text(settled->refusal.detail, WEIVEL_C_TEXT_NOT_COPIED, "a detail that could not be copied");
        check_text(settled->refusal.fix, WEIVEL_C_TEXT_NOT_COPIED, "a fix that could not be copied");
        check_text(settled->subject, WEIVEL_C_TEXT_NOT_COPIED, "a subject that could not be copied");
        more->checked++;
    } else {
        CHECK(event->incomplete == 0);
    }
    return WEIVEL_C_STATUS_OK;
}

static void an_event_whose_texts_cannot_be_copied_is_still_heard_with_a_fixed_sentence(void) {
    hearing listener = {0};
    telling sink = {0};
    counting_memory memory = {0};
    starved more = {0};
    weivel_service_want consumed = want_of("p:a");
    weivel_c_core *core = make_core_on(&memory, &listener, &sink);
    more.plugin = open_plugin(core, "c", NULL, 0, &consumed, 1);
    more.want = want_for_a_hundred_milliseconds(core, more.plugin);
    listener.react = check_a_settlement_without_its_texts;
    listener.more = &more;
    memory.refuse_from_size = sizeof(weivel_c_queued_event) + 1;
    CHECK(weivel_c_core_advance(core, 100, NULL) == WEIVEL_C_STATUS_OK);
    memory.refuse_from_size = 0;
    CHECK(more.checked == 1);
    CHECK(count_told(&sink, WEIVEL_C_FAILURE_EVENT_INCOMPLETE, WEIVEL_EVENT_WANT_SETTLED) == 1);
    CHECK(count_told(&sink, WEIVEL_C_FAILURE_EVENT_NOT_COPIED, WEIVEL_EVENT_WANT_SETTLED) == 0);
    end_core(core);
    CHECK(memory.held == 0);
}

static weivel_c_status check_a_question_without_its_constraints(hearing *self, weivel_c_core *core,
                                                                const weivel_c_event *event) {
    starved *more = self->more;
    (void)core;
    if (event->kind == WEIVEL_EVENT_CAPACITY_WANTED) {
        const weivel_capacity_event *asked = &event->data.capacity;
        CHECK(event->incomplete == 1);
        CHECK(asked->want != 0 && asked->plugin != 0 && asked->registration != 0);
        CHECK(asked->question == WEIVEL_CAPACITY_ATTRIBUTES);
        CHECK(asked->constraints.count == 0 && asked->constraints.items == NULL);
        more->checked++;
    }
    return WEIVEL_C_STATUS_OK;
}

static void a_capacity_event_whose_constraints_cannot_be_copied_is_heard_marked_incomplete(void) {
    hearing listener = {0};
    telling sink = {0};
    counting_memory memory = {0};
    starved more = {0};
    weivel_constraint_entry entry;
    weivel_core_options core_options = options_with_a_speed_constraint(&entry);
    weivel_c_core *core = make_core_with_options_on(&memory, &core_options, &listener, &sink);
    listener.react = check_a_question_without_its_constraints;
    listener.more = &more;
    memory.refuse_from_size = sizeof(weivel_c_queued_event) + 1;
    ask_a_capacity_question(core);
    memory.refuse_from_size = 0;
    CHECK(more.checked == 1);
    CHECK(count_told(&sink, WEIVEL_C_FAILURE_EVENT_INCOMPLETE, WEIVEL_EVENT_CAPACITY_WANTED) == 1);
    end_core(core);
    CHECK(memory.held == 0);
}

static void an_event_with_no_memory_at_all_is_dropped_and_told_with_its_kind(void) {
    hearing listener = {0};
    telling sink = {0};
    counting_memory memory = {0};
    weivel_service_want consumed = want_of("p:a");
    weivel_c_core *core = make_core_on(&memory, &listener, &sink);
    weivel_handle consumer = open_plugin(core, "c", NULL, 0, &consumed, 1);
    want_for_a_hundred_milliseconds(core, consumer);
    listener.count = 0;
    forget_crossings();
    memory.refuse_from_size = 1;
    CHECK(weivel_c_core_advance(core, 100, NULL) == WEIVEL_C_STATUS_OK);
    memory.refuse_from_size = 0;
    CHECK(crossed.events >= 2 && crossed.event_release == crossed.events);
    CHECK(listener.count == 0);
    CHECK(sink.count == crossed.events);
    CHECK(count_told(&sink, WEIVEL_C_FAILURE_EVENT_NOT_COPIED, WEIVEL_EVENT_WANT_SETTLED) == 1);
    CHECK(count_told(&sink, WEIVEL_C_FAILURE_EVENT_NOT_COPIED, WEIVEL_EVENT_DEADLINE) == 1);
    end_core(core);
    CHECK(memory.held == 0);
}

static void an_event_that_cannot_be_copied_goes_to_the_sink_and_the_rest_are_still_drained(void) {
    hearing listener = {0};
    telling sink = {0};
    counting_memory memory = {0};
    weivel_c_core *core = make_core_on(&memory, &listener, &sink);
    weivel_handle plugin = open_a_watched_provider(core);
    listener.count = 0;
    forget_crossings();
    memory.fail_at = memory.allocations + 2;
    CHECK(weivel_c_core_release_plugin(core, plugin, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(crossed.events >= 3);
    CHECK(listener.count == crossed.events - 1);
    CHECK(crossed.event_release == crossed.events && crossed.unreleased == 0 && crossed.most_unreleased == 1);
    CHECK(sink.count == 1);
    CHECK(sink.items[0].failure.kind == WEIVEL_C_FAILURE_EVENT_NOT_COPIED);
    CHECK(sink.items[0].failure.event_kind != WEIVEL_EVENT_NONE);
    CHECK(sink.items[0].failure.sequence == listener.items[0].sequence + 1);
    CHECK(listener.items[1].sequence == listener.items[0].sequence + 2);
    end_core(core);
    CHECK(memory.held == 0);
}

static void a_refusal_that_cannot_be_copied_is_answered_with_its_code_and_a_fixed_sentence(void) {
    hearing listener = {0};
    telling sink = {0};
    counting_memory memory = {0};
    weivel_c_refusal refusal = WEIVEL_INIT(weivel_c_refusal);
    weivel_c_core *core = make_core_on(&memory, &listener, &sink);
    weivel_handle plugin = open_plugin(core, "p", NULL, 0, NULL, 0);
    weivel_handle handle = 0;
    uint32_t has_fix;
    CHECK(weivel_c_core_register_service(core, plugin, "elsewhere:w", &handle, &refusal) ==
          WEIVEL_C_STATUS_REFUSED);
    has_fix = refusal.has_fix;
    CHECK(has_fix == 1);
    CHECK(weivel_c_refusal_release(&refusal) == WEIVEL_C_STATUS_OK);
    memory.fail_at = memory.allocations + 2;
    CHECK(weivel_c_core_register_service(core, plugin, "elsewhere:x", &handle, &refusal) ==
          WEIVEL_C_STATUS_REFUSED);
    CHECK(refusal.code == WEIVEL_REFUSAL_OUTSIDE_NAMESPACE && refusal.has_fix == has_fix);
    check_text(refusal.detail, WEIVEL_C_TEXT_NOT_COPIED, "a refusal's detail that could not be copied");
    check_text(refusal.fix, WEIVEL_C_TEXT_NOT_COPIED, "a refusal's fix that could not be copied");
    check_text(refusal.subject, WEIVEL_C_TEXT_NOT_COPIED, "a refusal's subject that could not be copied");
    CHECK(refusal.storage == NULL);
    CHECK(sink.count == 1 && sink.items[0].failure.kind == WEIVEL_C_FAILURE_REFUSAL_NOT_COPIED);
    CHECK(count_heard(&listener, WEIVEL_EVENT_REFUSAL) == 2);
    CHECK(weivel_c_core_register_service(core, plugin, "p:fine", &handle, &refusal) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_refusal_release(&refusal) == WEIVEL_C_STATUS_OK);
    CHECK(is_empty_refusal(&refusal));
    CHECK(weivel_c_core_register_service(core, plugin, "elsewhere:y", &handle, &refusal) ==
          WEIVEL_C_STATUS_REFUSED);
    check_text(refusal.subject, "elsewhere:y", "a refusal copied once memory is back");
    end_core(core);
    CHECK(memory.held == 1);
    CHECK(weivel_c_refusal_release(&refusal) == WEIVEL_C_STATUS_OK);
    CHECK(memory.held == 0);
}

static void a_listener_that_cannot_be_kept_is_answered_out_of_memory(void) {
    hearing listener = {0};
    counting_memory memory = {0};
    weivel_c_allocator allocator = {allocate_counted, release_counted, &memory};
    weivel_c_listener heard_by = {hear, &listener};
    uint64_t registration = 0;
    weivel_c_core *core = make_core(NULL, NULL, NULL);
    weivel_c_core_replace_allocator(core, &allocator);
    memory.fail_at = 1;
    CHECK(weivel_c_core_add_listener(core, &heard_by, &registration) == WEIVEL_C_STATUS_OUT_OF_MEMORY);
    CHECK(registration == 0);
    CHECK(weivel_c_core_add_listener(core, &heard_by, &registration) == WEIVEL_C_STATUS_OK);
    end_core(core);
    CHECK(memory.held == 0);
}

static void a_deadline_whose_copy_fails_is_still_followed(void) {
    hearing listener = {0};
    telling sink = {0};
    counting_memory memory = {0};
    clockwork clock = {0};
    weivel_c_deadline_hooks hooks = hooks_of(&clock);
    weivel_service_want consumed = want_of("p:a");
    weivel_c_core *core = make_core_on(&memory, &listener, &sink);
    weivel_handle consumer = open_plugin(core, "c", NULL, 0, &consumed, 1);
    CHECK(weivel_c_core_set_deadline_hooks(core, &hooks) == WEIVEL_C_STATUS_OK);
    listener.count = 0;
    memory.refuse_from_size = 1;
    want_for_a_hundred_milliseconds(core, consumer);
    memory.refuse_from_size = 0;
    CHECK(count_heard(&listener, WEIVEL_EVENT_DEADLINE) == 0);
    CHECK(count_told(&sink, WEIVEL_C_FAILURE_EVENT_NOT_COPIED, WEIVEL_EVENT_DEADLINE) == 1);
    CHECK(clock.follows == 1 && clock.followed_set == 1 && clock.followed_at == 100);
    end_core(core);
    CHECK(memory.held == 0);
}

static void more_failures_than_one_call_keeps_are_counted_for_the_sink(void) {
    hearing listener = {0};
    telling sink = {0};
    counting_memory memory = {0};
    weivel_c_core *core = make_core_on(&memory, &listener, &sink);
    weivel_handle plugin = open_a_watched_provider(core);
    weivel_handle watch = 0;
    int index;
    for (index = 0; index < 4; index++) {
        CHECK(weivel_c_core_watch_service(core, WEIVEL_HOST, "p:a", &watch, NULL) == WEIVEL_C_STATUS_OK);
        CHECK(weivel_c_core_watch_service(core, WEIVEL_HOST, "p:b", &watch, NULL) == WEIVEL_C_STATUS_OK);
    }
    forget_crossings();
    memory.refuse_from_size = 1;
    CHECK(weivel_c_core_release_plugin(core, plugin, NULL) == WEIVEL_C_STATUS_OK);
    memory.refuse_from_size = 0;
    CHECK(crossed.events > 8);
    CHECK(sink.count == 9);
    CHECK(sink.items[7].failure.kind == WEIVEL_C_FAILURE_EVENT_NOT_COPIED);
    CHECK(sink.items[8].failure.kind == WEIVEL_C_FAILURE_NOT_TOLD);
    CHECK(sink.items[8].failure.sequence == crossed.events - 8);
    end_core(core);
    CHECK(memory.held == 0);
}

static void run_the_tests_that_fail_an_allocation(void) {
    a_deadline_whose_copy_fails_is_still_followed();
    more_failures_than_one_call_keeps_are_counted_for_the_sink();
    an_event_that_cannot_be_copied_goes_to_the_sink_and_the_rest_are_still_drained();
    an_event_whose_texts_cannot_be_copied_is_still_heard_with_a_fixed_sentence();
    a_capacity_event_whose_constraints_cannot_be_copied_is_heard_marked_incomplete();
    an_event_with_no_memory_at_all_is_dropped_and_told_with_its_kind();
    a_refusal_that_cannot_be_copied_is_answered_with_its_code_and_a_fixed_sentence();
    a_listener_that_cannot_be_kept_is_answered_out_of_memory();
}

#else

static void run_the_tests_that_fail_an_allocation(void) {
}

#endif

int main(int argument_count, char **arguments) {
    if (argument_count != 2 || !load_tables(arguments[1])) {
        fprintf(stderr, "usage: wrapper_test <core-library>\n");
        return 2;
    }
    a_call_that_reports_nothing_waiting_crosses_no_further();
    drains_once_for_each_event_the_core_reports_waiting();
    stops_at_none_when_the_core_holds_fewer_events_than_it_reported();
    releases_each_event_before_it_drains_the_next();
    texts_inside_an_event_are_the_layers_own_copies();
    a_capacity_events_constraints_are_the_layers_own_copies();
    steps_through_an_array_at_the_stride_its_first_element_declares();
    a_refused_call_is_answered_with_the_refusal_the_core_queued_for_it();
    a_refusal_of_reach_is_answered_once_and_its_repeat_with_the_status_alone();
    a_late_registration_and_a_late_watch_are_answered_with_the_status_alone();
    a_handle_that_names_nothing_is_answered_with_the_status_alone();
    malformed_from_a_call_the_layer_built_is_reported_as_the_layers_defect();
    malformed_for_a_struct_the_caller_built_is_the_callers_mistake();
    a_call_inside_a_listener_joins_the_end_and_never_re_enters_the_dispatcher();
    a_deferred_step_runs_in_queue_order_behind_what_was_already_drained();
    a_step_deferred_while_no_dispatch_runs_has_run_when_the_call_returns();
    a_step_runs_as_the_dispatcher_so_a_call_inside_it_joins_the_end();
    a_failing_listener_goes_to_the_sink_and_every_later_event_is_still_heard();
    listeners_hear_in_the_order_added_until_removed();
    a_deadline_is_followed_after_its_drain_and_before_its_dispatch();
    a_due_deadline_is_handed_to_the_core_before_the_call_and_never_checked_again();
    an_advance_the_caller_makes_is_not_preceded_by_the_check();
    a_failing_deadline_hook_goes_to_the_sink_and_the_call_is_still_made();
    the_deadline_hooks_run_outside_the_layer_lock();
    hooks_set_while_a_deadline_is_recorded_are_told_it_at_once();
    an_event_of_a_kind_the_layer_does_not_know_is_released_and_never_heard();
    each_defect_of_one_call_keeps_its_own_text();
    a_drain_the_core_refuses_midway_is_a_defect_and_the_rest_is_drained_by_the_next_call();
    an_array_whose_stride_cannot_be_read_leaves_its_event_incomplete();
    a_deferral_says_whether_the_step_was_dispatched_before_it_returned();
    destroy_inside_a_step_drops_the_steps_behind_it();
    destroy_drops_what_was_not_dispatched_and_nothing_is_dispatched_after_it();
    a_core_that_is_not_destroyed_is_not_freed();
    refuses_a_misused_creation_and_creates_nothing();
    run_the_tests_that_fail_an_allocation();
    CHECK(weivel_c_core_unload(&real_table) == WEIVEL_C_STATUS_OK);
    return check_summary("core wrapper");
}
