/**
 * @file
 * How a context's records end: a withdrawal, a stop and an unregistration against what the dispatch already holds,
 * entries ended or a context freed from inside a callback, the contexts freed, and every reservation a record takes
 * given back exactly once.
 */

#include "world.h"

#if !defined(WEIVEL_C_USING_SHARED)
#include "../../src/context/contexts.h"
#endif

/** Objects plugins register; only their addresses matter. */
static int first_object;
static int second_object;

/** A provider `p` of `p:a` and a consumer `c` of it and of `also`, opened in a new world. */
static void begin_pair_also_wanting(world *in, plugin *provider, plugin *consumer, const char *also) {
    begin_world(in, NULL, 1);
    name_plugin(provider, "p");
    provides(provider, "p:a");
    name_plugin(consumer, "c");
    consumes(consumer, "p:a");
    consumes(consumer, also);
    open_in(in, provider);
    open_in(in, consumer);
}

/** A flag one thread raises and another waits on, guarded so the thread sanitizer sees each crossing. */
typedef struct gate {
    test_mutex mutex;
    int open;
} gate;

static void start_gate(gate *self) {
    start_test_mutex(&self->mutex);
    self->open = 0;
}

static void open_gate(gate *self) {
    lock_test_mutex(&self->mutex);
    self->open = 1;
    unlock_test_mutex(&self->mutex);
}

static void pass_gate(gate *self) {
    int open = 0;
    while (!open) {
        lock_test_mutex(&self->mutex);
        open = self->open;
        unlock_test_mutex(&self->mutex);
        if (!open) {
            let_other_threads_run();
        }
    }
}

/**
 * A want withdrawn on one thread while its settlement is dispatched on another. The dispatcher takes the settlement
 * and holds still until the withdrawing thread is inside its call, between asking and being answered; the
 * withdrawing thread holds still there until the settlement has been heard in full.
 */
typedef struct race {
    world *in;
    plugin *provider;
    weivel_handle registration;
    weivel_handle want;
    gate settlement_taken;
    gate withdrawal_inside;
    gate settlement_heard;
    int paused;
} race;

static int is_the_raced_settlement(const race *self, const weivel_c_event *event) {
    return event->kind == WEIVEL_EVENT_WANT_SETTLED && event->data.want.want == self->want;
}

static weivel_c_status hold_the_settlement_still(hearing *listener, weivel_c_core *core, const weivel_c_event *event) {
    race *self = listener->more;
    (void)core;
    if (is_the_raced_settlement(self, event)) {
        open_gate(&self->settlement_taken);
        pass_gate(&self->withdrawal_inside);
    }
    return WEIVEL_C_STATUS_OK;
}

static weivel_c_status tell_the_settlement_was_heard(void *data, weivel_c_core *core, const weivel_c_event *event) {
    race *self = data;
    (void)core;
    if (is_the_raced_settlement(self, event)) {
        open_gate(&self->settlement_heard);
    }
    return WEIVEL_C_STATUS_OK;
}

static void hold_the_withdrawal_still(void *more) {
    race *self = more;
    if (self->paused) {
        return;
    }
    self->paused = 1;
    open_gate(&self->withdrawal_inside);
    pass_gate(&self->settlement_heard);
}

static weivel_c_status register_the_awaited_service(void *data, weivel_c_core *core) {
    race *self = data;
    (void)core;
    CHECK(weivel_c_context_register_service(self->provider->context, "p:a", &first_object, NULL,
                                            &self->registration, NULL) == WEIVEL_C_STATUS_OK);
    return WEIVEL_C_STATUS_OK;
}

static void dispatch_the_settlement(void *argument) {
    race *self = argument;
    in_dispatch(self->in, register_the_awaited_service, self);
}

static void begin_the_race(world *in, race *self, plugin *provider, plugin *consumer) {
    weivel_c_listener last = {tell_the_settlement_was_heard, self};
    uint64_t listening = 0;
    memset(self, 0, sizeof *self);
    self->in = in;
    self->provider = provider;
    start_gate(&self->settlement_taken);
    start_gate(&self->withdrawal_inside);
    start_gate(&self->settlement_heard);
    begin_pair_also_wanting(in, provider, consumer, "p:later");
    CHECK(weivel_c_core_add_listener(in->core, &last, &listening) == WEIVEL_C_STATUS_OK);
    in->events.react = hold_the_settlement_still;
    in->events.more = self;
}

/** Leaves a deadline armed far away, so every call reads the clock before it asks the core. */
static void arm_a_distant_deadline(plugin *consumer, settled_record *never) {
    weivel_c_want_options options = WEIVEL_INIT(weivel_c_want_options);
    weivel_c_want_callback callback = settling(never);
    options.timed = WEIVEL_CHOICE_YES;
    options.timeout_millis = 1000000000;
    CHECK(weivel_c_context_want_service(consumer->context, "p:later", &options, &callback, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
}

static void a_want_withdrawn_while_its_settlement_is_dispatched_settles_once_and_its_revocation_is_heard(void) {
    world in;
    plugin provider;
    plugin consumer;
    race raced;
    settled_record settled = {0};
    settled_record never = {0};
    reported_record reported = {0};
    weivel_c_want_callback callback = settling(&settled);
    weivel_c_refusal refusal = WEIVEL_INIT(weivel_c_refusal);
    weivel_c_status withdrawn;
    test_thread dispatcher;
    begin_the_race(&in, &raced, &provider, &consumer);
    arm_a_distant_deadline(&consumer, &never);
    listen_to(&consumer, &reported);
    CHECK(weivel_c_context_want_service(consumer.context, "p:a", NULL, &callback, &raced.want, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(start_test_thread(&dispatcher, dispatch_the_settlement, &raced));
    pass_gate(&raced.settlement_taken);
    in.clock.more = &raced;
    in.clock.on_read = hold_the_withdrawal_still;
    withdrawn = weivel_c_context_withdraw_want(consumer.context, raced.want, &refusal);
    in.clock.on_read = NULL;
    join_test_thread(&dispatcher);
    CHECK(raced.paused == 1);
    CHECK(withdrawn == WEIVEL_C_STATUS_REFUSED && refusal.code == WEIVEL_REFUSAL_STALE_HANDLE);
    CHECK(weivel_c_refusal_release(&refusal) == WEIVEL_C_STATUS_OK);
    CHECK(settled.count == 1 && settled.service == &first_object && settled.want == raced.want);
    CHECK(holdings_of(&in) == 1);
    in.events.react = NULL;
    CHECK(weivel_c_context_unregister_service(provider.context, raced.registration, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(reported.count == 1 && reported.items[0].kind == WEIVEL_C_PLUGIN_EVENT_REVOKED);
    CHECK(reported.items[0].object == &first_object);
    CHECK(never.count == 0 && in.sink.count == 0);
    end_world(&in);
}

/** A withdrawal made inside a dispatch while the want's settlement waits behind it, so the core refuses it. */
typedef struct late_withdrawal {
    plugin *provider;
    plugin *consumer;
    settled_record *settled;
    weivel_handle want;
    weivel_handle registration;
    weivel_c_status answered;
    uint32_t code;
    uint32_t settled_before;
} late_withdrawal;

static weivel_c_status register_then_withdraw(void *data, weivel_c_core *core) {
    late_withdrawal *self = data;
    weivel_c_refusal refusal = WEIVEL_INIT(weivel_c_refusal);
    (void)core;
    CHECK(weivel_c_context_register_service(self->provider->context, "p:a", &first_object, NULL,
                                            &self->registration, NULL) == WEIVEL_C_STATUS_OK);
    self->answered = weivel_c_context_withdraw_want(self->consumer->context, self->want, &refusal);
    self->code = refusal.code;
    self->settled_before = self->settled->count;
    CHECK(weivel_c_refusal_release(&refusal) == WEIVEL_C_STATUS_OK);
    return WEIVEL_C_STATUS_OK;
}

static void a_withdrawal_the_core_refuses_answers_its_refusal_and_the_want_still_settles(void) {
    world in;
    plugin provider;
    plugin consumer;
    settled_record settled = {0};
    weivel_c_want_callback callback = settling(&settled);
    late_withdrawal late = {&provider, &consumer, &settled, 0, 0, WEIVEL_C_STATUS_OK, 0, 0};
    begin_pair(&in, &provider, &consumer, 1);
    CHECK(weivel_c_context_want_service(consumer.context, "p:a", NULL, &callback, &late.want, NULL) ==
          WEIVEL_C_STATUS_OK);
    in_dispatch(&in, register_then_withdraw, &late);
    CHECK(late.answered == WEIVEL_C_STATUS_REFUSED && late.code == WEIVEL_REFUSAL_STALE_HANDLE);
    CHECK(late.settled_before == 0);
    CHECK(settled.count == 1 && settled.service == &first_object);
    CHECK(records_of(&in) == 1 && holdings_of(&in) == 1);
    end_world(&in);
}

typedef struct late_stop {
    plugin *provider;
    plugin *consumer;
    weivel_handle watch;
} late_stop;

static weivel_c_status register_then_stop_watching(void *data, weivel_c_core *core) {
    late_stop *self = data;
    (void)core;
    CHECK(weivel_c_context_register_service(self->provider->context, "p:a", &first_object, NULL, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_stop_watching(self->consumer->context, self->watch, NULL) == WEIVEL_C_STATUS_OK);
    return WEIVEL_C_STATUS_OK;
}

static void a_change_still_queued_for_a_watch_that_stopped_is_not_heard(void) {
    world in;
    plugin provider;
    plugin consumer;
    watched_record watched = {0};
    weivel_c_service_watcher watcher = watching_services(&watched);
    late_stop late = {&provider, &consumer, 0};
    begin_pair(&in, &provider, &consumer, 1);
    CHECK(weivel_c_context_watch_service(consumer.context, "p:a", &watcher, &late.watch, NULL) ==
          WEIVEL_C_STATUS_OK);
    in_dispatch(&in, register_then_stop_watching, &late);
    CHECK(count_heard(&in.events, WEIVEL_EVENT_WATCH) == 1);
    CHECK(watched.count == 0 && records_of(&in) == 1);
    end_world(&in);
}

static void a_watch_does_not_hear_a_registration_the_layer_keeps_no_object_for(void) {
    world in;
    plugin provider;
    plugin consumer;
    watched_record watched = {0};
    weivel_c_service_watcher watcher = watching_services(&watched);
    weivel_handle around = 0;
    begin_pair(&in, &provider, &consumer, 1);
    CHECK(weivel_c_context_watch_service(consumer.context, "p:a", &watcher, NULL, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_register_service(in.core, provider.handle, "p:a", &around, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(watched.count == 0 && holdings_of(&in) == 0);
    CHECK(in.sink.count == 1 && told_of(&in, WEIVEL_C_FAILURE_OBJECT_UNKNOWN) == 1);
    end_world(&in);
}

static void ending_a_handle_as_another_kind_answers_nothing_and_asks_the_core_nothing(void) {
    world in;
    plugin provider;
    plugin consumer;
    watched_record watched = {0};
    settled_record settled = {0};
    weivel_c_service_watcher watcher = watching_services(&watched);
    weivel_c_want_callback callback = settling(&settled);
    weivel_handle watch = 0;
    weivel_handle want = 0;
    weivel_handle registration = 0;
    begin_pair(&in, &provider, &consumer, 1);
    CHECK(weivel_c_context_watch_service(consumer.context, "p:a", &watcher, &watch, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_want_service(consumer.context, "p:a", NULL, &callback, &want, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_register_service(provider.context, "p:b", &first_object, NULL, &registration, NULL) ==
          WEIVEL_C_STATUS_OK);
    forget_every_crossing();
    CHECK(weivel_c_context_unregister_service(consumer.context, watch, NULL) == WEIVEL_C_STATUS_NONE);
    CHECK(weivel_c_context_withdraw_want(consumer.context, watch, NULL) == WEIVEL_C_STATUS_NONE);
    CHECK(weivel_c_context_stop_watching(consumer.context, want, NULL) == WEIVEL_C_STATUS_NONE);
    CHECK(weivel_c_context_stop_watching(provider.context, registration, NULL) == WEIVEL_C_STATUS_NONE);
    CHECK(weivel_c_context_withdraw_want(provider.context, registration, NULL) == WEIVEL_C_STATUS_NONE);
    CHECK(every_crossing() == 0);
    CHECK(weivel_c_context_register_service(provider.context, "p:a", &second_object, NULL, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(watched.count == 1 && settled.count == 1 && settled.service == &second_object);
    end_world(&in);
}

static void a_revocation_of_nothing_the_layer_handed_over_is_not_told(void) {
    world in;
    plugin provider;
    plugin consumer;
    reported_record reported = {0};
    weivel_handle registration = 0;
    weivel_handle got_around = 0;
    begin_pair(&in, &provider, &consumer, 1);
    listen_to(&consumer, &reported);
    CHECK(weivel_c_context_register_service(provider.context, "p:a", &first_object, NULL, &registration, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_get_service(in.core, consumer.handle, "p:a", &got_around, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(got_around == registration && holdings_of(&in) == 0);
    CHECK(weivel_c_context_unregister_service(provider.context, registration, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(count_heard(&in.events, WEIVEL_EVENT_REVOKED) == 1);
    CHECK(reported.count == 0 && in.sink.count == 0);
    end_world(&in);
}

/** The object the contexts keep under a handle: what a holding of that handle points at. */
static void *object_kept_under(world *in, weivel_handle handle) {
    weivel_c_handle_entry entry = WEIVEL_INIT(weivel_c_handle_entry);
    CHECK(weivel_c_handles_find(in->handles, handle, &entry) == WEIVEL_C_STATUS_OK);
    return entry.object;
}

static void a_revocation_of_a_holding_that_no_longer_matches_its_registration_is_not_told(void) {
    world in;
    plugin provider;
    plugin consumer;
    reported_record reported = {0};
    weivel_handle registration = 0;
    weivel_handle other = 0;
    void *service = NULL;
    begin_world(&in, NULL, 1);
    name_plugin(&provider, "p");
    provides(&provider, "p:a");
    provides(&provider, "p:b");
    name_plugin(&consumer, "c");
    consumes(&consumer, "p:a");
    open_in(&in, &provider);
    open_in(&in, &consumer);
    listen_to(&consumer, &reported);
    CHECK(weivel_c_context_register_service(provider.context, "p:a", &first_object, NULL, &registration, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_register_service(provider.context, "p:b", &second_object, NULL, &other, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_get_service(consumer.context, "p:a", &service, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_handles_hold(in.handles, consumer.handle, registration, object_kept_under(&in, other)) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_unregister_service(provider.context, registration, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(count_heard(&in.events, WEIVEL_EVENT_REVOKED) == 1);
    CHECK(reported.count == 0 && holdings_of(&in) == 0);
    end_world(&in);
}

static void destroy_the_core(void *more) {
    CHECK(weivel_c_core_destroy(more) == WEIVEL_C_STATUS_OK);
}

static void an_unregistration_whose_dispatch_destroys_the_core_leaves_its_record_to_end_entries(void) {
    world in;
    plugin provider;
    plugin consumer;
    watched_record watched = {0};
    weivel_c_service_watcher watcher = watching_services(&watched);
    weivel_handle registration = 0;
    begin_pair(&in, &provider, &consumer, 1);
    CHECK(weivel_c_context_watch_service(consumer.context, "p:a", &watcher, NULL, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_register_service(provider.context, "p:a", &first_object, NULL, &registration, NULL) ==
          WEIVEL_C_STATUS_OK);
    watched.react = destroy_the_core;
    watched.more = in.core;
    CHECK(weivel_c_context_unregister_service(provider.context, registration, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(watched.count == 2 && records_of(&in) == 2);
    CHECK(weivel_c_context_end_entries(provider.context) == WEIVEL_C_STATUS_OK);
    CHECK(records_of(&in) == 1);
    end_world(&in);
}

static void a_capacity_answer_is_reported_at_the_time_on_the_hosts_clock(void) {
    world in;
    plugin provider;
    plugin consumer;
    weivel_constraint_entry entry = WEIVEL_INIT(weivel_constraint_entry);
    weivel_core_options core_options = WEIVEL_INIT(weivel_core_options);
    weivel_number constraint = WEIVEL_INIT(weivel_number);
    settled_record settled = {0};
    weivel_c_want_callback callback = settling(&settled);
    weivel_c_want_options options = WEIVEL_INIT(weivel_c_want_options);
    entry.constraint_name = "speed";
    entry.attribute_name = "speed";
    entry.bound = WEIVEL_BOUND_LOWER;
    core_options.constraint_vocabulary.items = &entry;
    core_options.constraint_vocabulary.count = 1;
    constraint.name = "speed";
    constraint.value = 1.0;
    begin_world(&in, &core_options, 1);
    name_plugin(&provider, "p");
    provides(&provider, "p:a");
    name_plugin(&consumer, "c");
    consumes(&consumer, "p:a")->constraints = (weivel_numbers){&constraint, 1};
    open_in(&in, &provider);
    open_in(&in, &consumer);
    CHECK(weivel_c_context_register_service(provider.context, "p:a", &first_object, NULL, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    in.clock.now = 4321;
    options.poll_millis = 10;
    forget_every_crossing();
    CHECK(weivel_c_context_want_service(consumer.context, "p:a", &options, &callback, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(capacities_reported >= 1 && reported_now == 4321);
    end_world(&in);
}

/** A host's worth of everything but a timer: contexts made with none. */
typedef struct untimed {
    weivel_c_core *core;
    weivel_c_handles *handles;
    weivel_c_contexts *contexts;
    weivel_c_context *context;
    weivel_handle plugin;
} untimed;

static void begin_untimed(untimed *self, weivel_manifest *manifest) {
    weivel_c_contexts_options options = WEIVEL_INIT(weivel_c_contexts_options);
    weivel_plugin_options opening = WEIVEL_INIT(weivel_plugin_options);
    self->core = make_core(NULL, NULL, NULL);
    CHECK(weivel_c_handles_create(&self->handles) == WEIVEL_C_STATUS_OK);
    options.core = self->core;
    options.handles = self->handles;
    CHECK(weivel_c_contexts_create(&options, &self->contexts) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_register_manifest(self->core, manifest, &self->plugin, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_create(self->contexts, self->plugin, manifest, &self->context) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_token(self->context, &opening.payload) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_open_plugin(self->core, self->plugin, &opening, NULL) == WEIVEL_C_STATUS_OK);
}

static void end_untimed(untimed *self) {
    CHECK(weivel_c_core_destroy(self->core) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_contexts_free(self->contexts) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_free(self->core) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_handles_free(self->handles) == WEIVEL_C_STATUS_OK);
}

static void contexts_made_without_a_timer_take_a_timed_want_as_the_callers_mistake(void) {
    untimed host;
    weivel_service_want consumed = want_of("p:a");
    weivel_manifest manifest = WEIVEL_INIT(weivel_manifest);
    settled_record settled = {0};
    weivel_c_want_callback callback = settling(&settled);
    weivel_c_want_options options = WEIVEL_INIT(weivel_c_want_options);
    manifest.import_name = "c";
    manifest.api = 1;
    manifest.services.consumes.items = &consumed;
    manifest.services.consumes.count = 1;
    forget_every_crossing();
    begin_untimed(&host, &manifest);
    options.timed = WEIVEL_CHOICE_YES;
    options.timeout_millis = 5;
    wants_asked = 0;
    CHECK(weivel_c_context_want_service(host.context, "p:a", &options, &callback, NULL, NULL) ==
          WEIVEL_C_STATUS_MISUSE);
    CHECK(wants_asked == 0 && settled.count == 0);
    CHECK(weivel_c_context_want_service(host.context, "p:a", NULL, &callback, NULL, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(wants_asked == 1);
    end_untimed(&host);
}

static void freeing_the_contexts_leaves_no_entry_and_no_holding_in_the_table(void) {
    world in;
    plugin provider;
    plugin consumer;
    watched_record watched = {0};
    settled_record settled = {0};
    weivel_c_service_watcher watcher = watching_services(&watched);
    weivel_c_want_callback callback = settling(&settled);
    void *service = NULL;
    uint32_t entries = 99;
    uint32_t holdings = 99;
    begin_pair_also_wanting(&in, &provider, &consumer, "p:never");
    CHECK(weivel_c_context_register_service(provider.context, "p:a", &first_object, NULL, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_get_service(consumer.context, "p:a", &service, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_watch_service(consumer.context, "p:a", &watcher, NULL, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_want_service(consumer.context, "p:never", NULL, &callback, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_timer_destroy(in.timer) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_destroy(in.core) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_contexts_free(in.contexts) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_handles_count(in.handles, &entries, &holdings) == WEIVEL_C_STATUS_OK);
    CHECK(entries == 0 && holdings == 0);
    check_no_reservation_is_left(&in);
    CHECK(weivel_c_timer_free(in.timer) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_free(in.core) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_handles_free(in.handles) == WEIVEL_C_STATUS_OK);
}

static void a_listing_that_leaves_out_an_implementation_gives_back_the_room_it_kept_for_it(void) {
    world in;
    plugin maker;
    plugin consumer;
    weivel_c_implementations list = WEIVEL_INIT(weivel_c_implementations);
    weivel_handle around = 0;
    begin_world(&in, NULL, 1);
    name_plugin(&maker, "m");
    maker.capabilities_provided[maker.capabilities_provided_count++] = "screens";
    name_plugin(&consumer, "c");
    consumer.capabilities_consumed[consumer.capabilities_consumed_count++] = "screens";
    open_in(&in, &maker);
    open_in(&in, &consumer);
    CHECK(weivel_c_core_provide_capability(in.core, maker.handle, "screens", &around, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_verify_activation(in.core, maker.handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_all_capabilities(consumer.context, "screens", &list, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(list.count == 0 && holdings_of(&in) == 0);
    CHECK(told_of(&in, WEIVEL_C_FAILURE_OBJECT_UNKNOWN) == 1);
    end_world(&in);
}

static void a_detached_context_reaches_the_core_no_more_and_leaves_its_handle_free(void) {
    world in;
    plugin provider;
    plugin consumer;
    settled_record settled = {0};
    watched_record watched = {0};
    weivel_c_want_callback callback = settling(&settled);
    weivel_c_service_watcher watcher = watching_services(&watched);
    weivel_c_implementations list = WEIVEL_INIT(weivel_c_implementations);
    weivel_c_context *again = NULL;
    void *service = &second_object;
    begin_pair(&in, &provider, &consumer, 1);
    CHECK(weivel_c_context_register_service(provider.context, "p:a", &first_object, NULL, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_get_service(consumer.context, "p:a", &service, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_release_plugin(in.core, consumer.handle, NULL) == WEIVEL_C_STATUS_OK);
    end_entries_of(&in, &consumer);
    CHECK(weivel_c_core_forget_plugin(in.core, consumer.handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_detach(consumer.context) == WEIVEL_C_STATUS_OK);
    CHECK(entries_of(&in) == 2 && holdings_of(&in) == 0);
    forget_every_crossing();
    CHECK(weivel_c_context_get_service(consumer.context, "p:a", &service, NULL) == WEIVEL_C_STATUS_REFUSED);
    CHECK(service == NULL);
    CHECK(weivel_c_context_want_service(consumer.context, "p:a", NULL, &callback, NULL, NULL) ==
          WEIVEL_C_STATUS_REFUSED);
    CHECK(weivel_c_context_watch_service(consumer.context, "p:a", &watcher, NULL, NULL) == WEIVEL_C_STATUS_REFUSED);
    CHECK(weivel_c_context_register_service(consumer.context, "c:x", &first_object, NULL, NULL, NULL) ==
          WEIVEL_C_STATUS_REFUSED);
    CHECK(weivel_c_context_all_capabilities(consumer.context, "screens", &list, NULL) == WEIVEL_C_STATUS_REFUSED);
    CHECK(weivel_c_context_log(consumer.context, WEIVEL_LOG_ERROR, "still heard") == WEIVEL_C_STATUS_OK);
    CHECK(every_crossing() == 0 && settled.count == 0 && in.log.count == 1);
    CHECK(weivel_c_context_detach(consumer.context) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_detach(NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_context_create(in.contexts, consumer.handle, NULL, &again) == WEIVEL_C_STATUS_OK);
    end_world(&in);
}

#if !defined(WEIVEL_C_USING_SHARED)

#define GRAVEYARD_BLOCKS 64

/**
 * An allocator that never hands memory back while a test runs: a released block is poisoned and kept, so a read of it
 * reads the poison rather than whatever came next, and a block released twice is counted rather than freed twice.
 */
typedef struct graveyard {
    void *blocks[GRAVEYARD_BLOCKS];
    size_t sizes[GRAVEYARD_BLOCKS];
    int dead[GRAVEYARD_BLOCKS];
    uint32_t count;
    uint32_t released_twice;
    uint32_t unknown;
} graveyard;

static void *allocate_into_the_graveyard(void *data, size_t size) {
    graveyard *self = data;
    void *block;
    if (self->count == GRAVEYARD_BLOCKS) {
        return NULL;
    }
    block = malloc(size);
    if (block != NULL) {
        self->blocks[self->count] = block;
        self->sizes[self->count] = size;
        self->dead[self->count] = 0;
        self->count++;
    }
    return block;
}

static void bury(void *data, void *memory) {
    graveyard *self = data;
    uint32_t index;
    for (index = 0; index < self->count; index++) {
        if (self->blocks[index] == memory) {
            self->released_twice += self->dead[index];
            self->dead[index] = 1;
            memset(memory, 0xDD, self->sizes[index]);
            return;
        }
    }
    self->unknown++;
}

static void empty_the_graveyard(graveyard *self) {
    uint32_t index;
    for (index = 0; index < self->count; index++) {
        free(self->blocks[index]);
    }
    self->count = 0;
}

enum { FROM_A_WANT = 0, FROM_A_WATCH = 1, FROM_AN_ANSWERER = 2, CALLBACK_KINDS = 3 };

/** What a callback does to its own plugin's context before it reads the id it was handed. */
typedef struct ending_inside {
    plugin *ended;
    int frees;
    char id[WORLD_TEXT];
    uint32_t calls;
} ending_inside;

static void end_from_inside(ending_inside *self, const char *id) {
    self->calls++;
    if (self->calls > 1) {
        return;
    }
    if (self->frees) {
        CHECK(weivel_c_context_free(self->ended->context) == WEIVEL_C_STATUS_OK);
        self->ended->context = NULL;
    } else {
        CHECK(weivel_c_context_end_entries(self->ended->context) == WEIVEL_C_STATUS_OK);
    }
    copy_text(self->id, id);
}

static weivel_c_status end_inside_a_settlement(void *data, const weivel_c_settlement *settlement) {
    end_from_inside(data, settlement->id);
    return WEIVEL_C_STATUS_OK;
}

static weivel_c_status end_inside_a_change(void *data, const weivel_c_service_change *change) {
    end_from_inside(data, change->id);
    return WEIVEL_C_STATUS_OK;
}

static weivel_c_status end_inside_an_answer(void *data, const weivel_c_capacity_question *question,
                                           weivel_c_capacity_answer *answer) {
    (void)answer;
    end_from_inside(data, question->id);
    return WEIVEL_C_STATUS_FAILED;
}

/** A provider of `p:a` and a consumer that needs it at some speed, on a core that knows what speed is. */
typedef struct speeds {
    weivel_constraint_entry entry;
    weivel_number constraint;
    weivel_number advertised;
    plugin provider;
    plugin consumer;
} speeds;

static void begin_speeds(world *in, speeds *with, graveyard *dead) {
    weivel_core_options core_options = WEIVEL_INIT(weivel_core_options);
    weivel_c_allocator allocator = {allocate_into_the_graveyard, bury, dead};
    with->entry = (weivel_constraint_entry)WEIVEL_INIT(weivel_constraint_entry);
    with->entry.constraint_name = "speed";
    with->entry.attribute_name = "speed";
    with->entry.bound = WEIVEL_BOUND_LOWER;
    with->constraint = (weivel_number)WEIVEL_INIT(weivel_number);
    with->constraint.name = "speed";
    with->constraint.value = 1.0;
    with->advertised = with->constraint;
    with->advertised.value = 9.0;
    core_options.constraint_vocabulary.items = &with->entry;
    core_options.constraint_vocabulary.count = 1;
    begin_world(in, &core_options, 1);
    weivel_c_contexts_replace_allocator(in->contexts, &allocator);
    name_plugin(&with->provider, "p");
    provides(&with->provider, "p:a")->attributes = (weivel_numbers){&with->advertised, 1};
    name_plugin(&with->consumer, "c");
    consumes(&with->consumer, "p:a")->constraints = (weivel_numbers){&with->constraint, 1};
    open_in(in, &with->provider);
    open_in(in, &with->consumer);
}

static void make_the_callback_run(uint32_t kind, speeds *with, ending_inside *inside) {
    weivel_c_want_callback settled = {end_inside_a_settlement, inside};
    weivel_c_service_watcher watcher = {end_inside_a_change, inside};
    weivel_c_capacity_answerer answerer = {end_inside_an_answer, inside};
    weivel_c_want_options options = WEIVEL_INIT(weivel_c_want_options);
    options.poll_millis = 10;
    if (kind == FROM_A_WATCH) {
        CHECK(weivel_c_context_watch_service(with->consumer.context, "p:a", &watcher, NULL, NULL) ==
              WEIVEL_C_STATUS_OK);
    }
    CHECK(weivel_c_context_register_service(with->provider.context, "p:a", &first_object,
                                            kind == FROM_AN_ANSWERER ? &answerer : NULL, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    if (kind != FROM_A_WATCH) {
        CHECK(weivel_c_context_want_service(with->consumer.context, "p:a", &options, &settled, NULL, NULL) ==
              WEIVEL_C_STATUS_OK);
    }
}

static void end_inside_one_callback(uint32_t kind, int frees) {
    world in;
    speeds with;
    graveyard dead;
    ending_inside inside;
    memset(&dead, 0, sizeof dead);
    memset(&inside, 0, sizeof inside);
    begin_speeds(&in, &with, &dead);
    inside.ended = kind == FROM_AN_ANSWERER ? &with.provider : &with.consumer;
    inside.frees = frees;
    make_the_callback_run(kind, &with, &inside);
    CHECK(inside.calls >= 1);
    check_text(inside.id, "p:a", "the id a callback reads after ending its own entries");
    CHECK(dead.released_twice == 0 && dead.unknown == 0);
    end_world(&in);
    CHECK(dead.released_twice == 0 && dead.unknown == 0);
    empty_the_graveyard(&dead);
}

static void ending_entries_or_freeing_the_context_inside_a_callback_releases_each_record_once(void) {
    uint32_t kind;
    for (kind = 0; kind < CALLBACK_KINDS; kind++) {
        end_inside_one_callback(kind, 0);
        end_inside_one_callback(kind, 1);
    }
}

static void a_handle_the_table_already_holds_gives_back_no_reservation_it_did_not_keep(void) {
    world in;
    plugin provider;
    plugin consumer;
    weivel_handle registration = 77;
    begin_pair(&in, &provider, &consumer, 1);
    CHECK(weivel_c_handles_reserve(in.handles) == WEIVEL_C_STATUS_OK);
    registration_answered_instead = consumer.handle;
    CHECK(weivel_c_context_register_service(provider.context, "p:a", &first_object, NULL, &registration, NULL) ==
          WEIVEL_C_STATUS_MISUSE);
    CHECK(registration == 0 && records_of(&in) == 0);
    CHECK(weivel_c_handles_reserved(in.handles) == 1);
    CHECK(weivel_c_handles_unreserve(in.handles) == WEIVEL_C_STATUS_OK);
    end_world(&in);
}

/** Starves the table as a settlement is heard, before the contexts record what it hands over. */
static weivel_c_status starve_the_table_on_a_settlement(hearing *listener, weivel_c_core *core,
                                                       const weivel_c_event *event) {
    world *in = listener->more;
    scarce_memory *memory = in->clock.more;
    (void)core;
    if (event->kind == WEIVEL_EVENT_WANT_SETTLED) {
        bring_the_table_to_the_brink(in, memory);
        memory->fails_all = 1;
    }
    return WEIVEL_C_STATUS_OK;
}

static void feed_the_table(void *more) {
    scarce_memory *memory = more;
    memory->fails_all = 0;
}

static void a_settled_wants_holding_needs_no_memory_so_its_revocation_is_heard(void) {
    world in;
    plugin provider;
    plugin consumer;
    scarce_memory memory = {0, 0, 0};
    weivel_c_allocator allocator = {allocate_scarcely, release_plainly, &memory};
    settled_record settled = {0};
    reported_record reported = {0};
    weivel_c_want_callback callback = settling(&settled);
    weivel_handle registration = 0;
    begin_world(&in, NULL, 1);
    weivel_c_handles_replace_allocator(in.handles, &allocator);
    name_plugin(&provider, "p");
    provides(&provider, "p:a");
    name_plugin(&consumer, "c");
    consumes(&consumer, "p:a");
    open_in(&in, &provider);
    open_in(&in, &consumer);
    listen_to(&consumer, &reported);
    CHECK(weivel_c_context_want_service(consumer.context, "p:a", NULL, &callback, NULL, NULL) == WEIVEL_C_STATUS_OK);
    in.clock.more = &memory;
    in.events.react = starve_the_table_on_a_settlement;
    in.events.more = &in;
    settled.react = feed_the_table;
    settled.more = &memory;
    CHECK(weivel_c_context_register_service(provider.context, "p:a", &first_object, NULL, &registration, NULL) ==
          WEIVEL_C_STATUS_OK);
    in.events.react = NULL;
    CHECK(settled.count == 1 && settled.service == &first_object && memory.fails_all == 0);
    CHECK(holdings_of(&in) == 1 && told_of(&in, WEIVEL_C_FAILURE_OBJECT_UNKNOWN) == 0);
    CHECK(weivel_c_context_unregister_service(provider.context, registration, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(reported.count == 1 && reported.items[0].object == &first_object);
    end_world(&in);
}

static void run_the_tests_of_the_layers_own_seams(void) {
    ending_entries_or_freeing_the_context_inside_a_callback_releases_each_record_once();
    a_handle_the_table_already_holds_gives_back_no_reservation_it_did_not_keep();
    a_settled_wants_holding_needs_no_memory_so_its_revocation_is_heard();
}

#else

static void run_the_tests_of_the_layers_own_seams(void) {
}

#endif

int main(int argument_count, char **arguments) {
    if (argument_count != 2 || !load_every_table(arguments[1])) {
        fprintf(stderr, "usage: context_endings_test <core-library>\n");
        return 2;
    }
    a_want_withdrawn_while_its_settlement_is_dispatched_settles_once_and_its_revocation_is_heard();
    a_withdrawal_the_core_refuses_answers_its_refusal_and_the_want_still_settles();
    a_change_still_queued_for_a_watch_that_stopped_is_not_heard();
    a_watch_does_not_hear_a_registration_the_layer_keeps_no_object_for();
    ending_a_handle_as_another_kind_answers_nothing_and_asks_the_core_nothing();
    a_revocation_of_nothing_the_layer_handed_over_is_not_told();
    a_revocation_of_a_holding_that_no_longer_matches_its_registration_is_not_told();
    an_unregistration_whose_dispatch_destroys_the_core_leaves_its_record_to_end_entries();
    a_capacity_answer_is_reported_at_the_time_on_the_hosts_clock();
    contexts_made_without_a_timer_take_a_timed_want_as_the_callers_mistake();
    freeing_the_contexts_leaves_no_entry_and_no_holding_in_the_table();
    a_listing_that_leaves_out_an_implementation_gives_back_the_room_it_kept_for_it();
    a_detached_context_reaches_the_core_no_more_and_leaves_its_handle_free();
    run_the_tests_of_the_layers_own_seams();
    CHECK(weivel_c_core_unload(&real_table) == WEIVEL_C_STATUS_OK);
    return check_summary("context endings");
}
