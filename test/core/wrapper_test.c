#include "fixture.h"

#if !defined(WEIVEL_C_USING_SHARED)
#include "../../src/core/wrapper.h"
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

static weivel_c_status keep_constraints(hearing *self, weivel_c_core *core, const weivel_c_event *event) {
    (void)core;
    if (event->kind == WEIVEL_EVENT_CAPACITY_WANTED && self->more != NULL) {
        weivel_capacity_event *kept = self->more;
        *kept = event->data.capacity;
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
    weivel_capacity_event kept;
    weivel_constraint_entry entry;
    weivel_core_options core_options = options_with_a_speed_constraint(&entry);
    weivel_c_core *core;
    forget_crossings();
    memset(&kept, 0, sizeof kept);
    listener.react = keep_constraints;
    listener.more = &kept;
    core = make_core(&core_options, &listener, NULL);
    ask_a_capacity_question(core);
    CHECK(count_heard(&listener, WEIVEL_EVENT_CAPACITY_WANTED) >= 1);
    CHECK(kept.question == WEIVEL_CAPACITY_ATTRIBUTES);
    CHECK(kept.constraints.count == 1);
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
        CHECK(weivel_c_core_defer(core, &more->step) == WEIVEL_C_STATUS_OK);
        more->second_deferral = weivel_c_core_defer(core, &more->step);
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

static void a_step_deferred_while_no_dispatch_runs_is_run_at_once(void) {
    hearing listener = {0};
    reentry more = {0};
    weivel_c_step small = WEIVEL_INIT(weivel_c_step);
    weivel_c_core *core = make_core(NULL, &listener, NULL);
    more.step = (weivel_c_step)WEIVEL_INIT(weivel_c_step);
    more.step.run = count_what_was_heard;
    more.step.data = &more;
    more.listener = &listener;
    CHECK(weivel_c_core_defer(core, &more.step) == WEIVEL_C_STATUS_OK);
    CHECK(more.steps_run == 1);
    CHECK(weivel_c_core_defer(core, &more.step) == WEIVEL_C_STATUS_OK);
    CHECK(more.steps_run == 2);
    CHECK(weivel_c_core_defer(core, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_core_defer(core, &small) == WEIVEL_C_STATUS_MISUSE);
    small.run = count_what_was_heard;
    small.size -= 1;
    CHECK(weivel_c_core_defer(core, &small) == WEIVEL_C_STATUS_MISUSE);
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
    CHECK(weivel_c_core_defer(core, &step) == WEIVEL_C_STATUS_OK);
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

/** A timer's stand-in: it records each announcement and says a deadline is due as often as it is told to. */
typedef struct clockwork {
    uint32_t announcements;
    int64_t announced_at;
    uint32_t announced_set;
    uint32_t asked;
    uint32_t due_answers_left;
    int64_t now;
    weivel_c_status answer;
} clockwork;

static weivel_c_status announced(void *data, int64_t at, uint32_t set) {
    clockwork *self = data;
    self->announcements++;
    self->announced_at = at;
    self->announced_set = set;
    return self->answer;
}

static weivel_c_status is_due(void *data, int64_t *now, uint32_t *due) {
    clockwork *self = data;
    self->asked++;
    if (self->due_answers_left > 0) {
        self->due_answers_left--;
        *now = self->now;
        *due = 1;
    }
    return self->answer;
}

static weivel_c_deadline_hooks hooks_of(clockwork *self) {
    weivel_c_deadline_hooks hooks = WEIVEL_INIT(weivel_c_deadline_hooks);
    hooks.announced = announced;
    hooks.is_due = is_due;
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
    uint32_t announcements_after_the_call;
    uint32_t deadlines_heard_by_then;
    int wanted;
} drain_time;

static weivel_c_status want_inside_a_listener(hearing *self, weivel_c_core *core, const weivel_c_event *event) {
    drain_time *more = self->more;
    if (event->kind == WEIVEL_EVENT_WATCH && !more->wanted) {
        more->wanted = 1;
        want_for_a_hundred_milliseconds(core, more->consumer);
        more->announcements_after_the_call = more->clock->announcements;
        more->deadlines_heard_by_then = count_heard(self, WEIVEL_EVENT_DEADLINE);
    }
    return WEIVEL_C_STATUS_OK;
}

static void a_deadline_is_announced_as_it_is_drained_and_before_it_is_dispatched(void) {
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
    CHECK(weivel_c_core_register_service(core, WEIVEL_HOST, "c", &handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(more.wanted == 1);
    CHECK(more.announcements_after_the_call == 1);
    CHECK(more.deadlines_heard_by_then == 0);
    CHECK(clock.announced_set == 1 && clock.announced_at == 100);
    CHECK(count_heard(&listener, WEIVEL_EVENT_DEADLINE) == 1);
    CHECK(weivel_c_core_set_deadline_hooks(core, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_unregister(core, handle, NULL) == WEIVEL_C_STATUS_OK);
    hooks.size -= 1;
    CHECK(weivel_c_core_set_deadline_hooks(core, &hooks) == WEIVEL_C_STATUS_MISUSE);
    end_core(core);
}

static void a_due_deadline_is_handed_to_the_core_before_the_call_and_never_checked_again(void) {
    static const char *const PROVIDED[] = {"p:a"};
    hearing listener = {0};
    clockwork clock = {0};
    weivel_c_deadline_hooks hooks = hooks_of(&clock);
    weivel_service_want consumed = want_of("p:a");
    weivel_c_core *core = make_core(NULL, &listener, NULL);
    weivel_handle provider = open_plugin(core, "p", PROVIDED, 1, NULL, 0);
    weivel_handle consumer = open_plugin(core, "c", NULL, 0, &consumed, 1);
    weivel_handle handle = 0;
    CHECK(weivel_c_core_set_deadline_hooks(core, &hooks) == WEIVEL_C_STATUS_OK);
    want_for_a_hundred_milliseconds(core, consumer);
    listener.count = 0;
    forget_crossings();
    clock.asked = 0;
    clock.due_answers_left = 3;
    clock.now = 150;
    CHECK(weivel_c_core_register_service(core, provider, "p:a", &handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(clock.asked == 1);
    CHECK(crossed.advance == 1 && crossed.advanced_to == 150);
    CHECK(count_heard(&listener, WEIVEL_EVENT_WANT_SETTLED) == 1);
    CHECK(listener.items[0].kind == WEIVEL_EVENT_WANT_SETTLED);
    CHECK(listener.items[0].code == WEIVEL_REFUSAL_TIMED_OUT && listener.items[0].implementation == 0);
    CHECK(clock.announced_set == 0);
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
    CHECK(sink.count == 2);
    CHECK(sink.items[0].failure.kind == WEIVEL_C_FAILURE_DEADLINE_HOOK);
    CHECK(sink.items[1].failure.kind == WEIVEL_C_FAILURE_DEADLINE_HOOK);
    CHECK(sink.items[1].failure.status == WEIVEL_C_STATUS_FAILED);
    CHECK(weivel_c_core_handle_state(core, want, &condition, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(condition == WEIVEL_HANDLE_WAITING);
    end_core(core);
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
        CHECK(weivel_c_core_defer(core, &more->waiting_step) == WEIVEL_C_STATUS_OK);
        CHECK(more->waiting_step.place.waiting != 0);
        more->destroyed = weivel_c_core_destroy(core);
        more->freed_inside = weivel_c_core_free(core);
        more->called_after = weivel_c_core_handle_state(core, 1, &condition, NULL);
        more->deferred_after = weivel_c_core_defer(core, &more->waiting_step);
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

/** An allocator that counts what it holds and fails the allocation it is told to. */
typedef struct counting_memory {
    uint32_t allocations;
    uint32_t fail_at;
    int32_t held;
} counting_memory;

static void *allocate_counted(void *data, size_t size) {
    counting_memory *self = data;
    self->allocations++;
    if (self->allocations == self->fail_at) {
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

static weivel_c_core *make_core_on(counting_memory *memory, hearing *listener, telling *sink) {
    weivel_c_allocator allocator = {allocate_counted, release_counted, memory};
    weivel_c_core *core = make_core(NULL, NULL, sink);
    weivel_c_listener heard_by = {hear, listener};
    uint64_t registration = 0;
    weivel_c_core_replace_allocator(core, &allocator);
    CHECK(weivel_c_core_add_listener(core, &heard_by, &registration) == WEIVEL_C_STATUS_OK);
    return core;
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

static void a_refusal_that_cannot_be_copied_leaves_the_caller_the_status_alone(void) {
    hearing listener = {0};
    telling sink = {0};
    counting_memory memory = {0};
    weivel_c_refusal refusal = WEIVEL_INIT(weivel_c_refusal);
    weivel_c_core *core = make_core_on(&memory, &listener, &sink);
    weivel_handle plugin = open_plugin(core, "p", NULL, 0, NULL, 0);
    weivel_handle handle = 0;
    memory.fail_at = memory.allocations + 2;
    CHECK(weivel_c_core_register_service(core, plugin, "elsewhere:x", &handle, &refusal) ==
          WEIVEL_C_STATUS_REFUSED);
    CHECK(is_empty_refusal(&refusal));
    CHECK(sink.count == 1 && sink.items[0].failure.kind == WEIVEL_C_FAILURE_REFUSAL_NOT_COPIED);
    CHECK(count_heard(&listener, WEIVEL_EVENT_REFUSAL) == 1);
    CHECK(weivel_c_core_register_service(core, plugin, "elsewhere:y", &handle, &refusal) ==
          WEIVEL_C_STATUS_REFUSED);
    CHECK(refusal.code == WEIVEL_REFUSAL_OUTSIDE_NAMESPACE);
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

static void run_the_tests_that_fail_an_allocation(void) {
    an_event_that_cannot_be_copied_goes_to_the_sink_and_the_rest_are_still_drained();
    a_refusal_that_cannot_be_copied_leaves_the_caller_the_status_alone();
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
    a_step_deferred_while_no_dispatch_runs_is_run_at_once();
    a_failing_listener_goes_to_the_sink_and_every_later_event_is_still_heard();
    listeners_hear_in_the_order_added_until_removed();
    a_deadline_is_announced_as_it_is_drained_and_before_it_is_dispatched();
    a_due_deadline_is_handed_to_the_core_before_the_call_and_never_checked_again();
    a_failing_deadline_hook_goes_to_the_sink_and_the_call_is_still_made();
    destroy_drops_what_was_not_dispatched_and_nothing_is_dispatched_after_it();
    a_core_that_is_not_destroyed_is_not_freed();
    refuses_a_misused_creation_and_creates_nothing();
    run_the_tests_that_fail_an_allocation();
    CHECK(weivel_c_core_unload(&real_table) == WEIVEL_C_STATUS_OK);
    return check_summary("core wrapper");
}
