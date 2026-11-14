/**
 * @file
 * What a plugin sees of the core through its context, each bullet of the contract's `context` section against the
 * real core, with plugins registered and opened the way a host opens them.
 */

#include "world.h"

#if !defined(WEIVEL_C_USING_SHARED)
#include "../../src/context/contexts.h"
#include "../../src/core/events.h"
#include "../../src/handles/table.h"
#endif

/** Objects plugins register; only their addresses matter. */
static int first_object;
static int second_object;

static void a_get_answers_the_providers_own_pointer_and_absence_is_not_a_refusal(void) {
    world in;
    plugin provider;
    plugin consumer;
    weivel_c_refusal refusal = WEIVEL_INIT(weivel_c_refusal);
    void *service = &second_object;
    begin_pair(&in, &provider, &consumer, 1);
    CHECK(weivel_c_context_get_service(consumer.context, "p:a", &service, &refusal) == WEIVEL_C_STATUS_NONE);
    CHECK(service == NULL && refusal.code == WEIVEL_REFUSAL_NONE && refusal.detail == NULL);
    CHECK(weivel_c_context_register_service(provider.context, "p:a", &first_object, NULL, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_get_service(consumer.context, "p:a", &service, &refusal) == WEIVEL_C_STATUS_OK);
    CHECK(service == &first_object);
    CHECK(refusal.code == WEIVEL_REFUSAL_NONE);
    CHECK(weivel_c_context_get_service(consumer.context, "p:a", NULL, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_context_get_service(NULL, "p:a", &service, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(in.sink.count == 0);
    end_world(&in);
}

static void a_refusal_of_reach_answers_the_cores_refusal_with_its_code_subject_and_fix(void) {
    world in;
    plugin provider;
    plugin consumer;
    weivel_c_refusal refusal = WEIVEL_INIT(weivel_c_refusal);
    void *service = &second_object;
    begin_pair(&in, &provider, &consumer, 1);
    CHECK(weivel_c_context_get_service(consumer.context, "p:other", &service, &refusal) == WEIVEL_C_STATUS_REFUSED);
    CHECK(service == NULL);
    CHECK(refusal.code == WEIVEL_REFUSAL_UNDECLARED);
    check_text(refusal.subject, "p:other", "the subject of a refused reach");
    CHECK(refusal.has_fix == 1 && refusal.fix != NULL && refusal.fix[0] != '\0');
    CHECK(refusal.detail != NULL && refusal.detail[0] != '\0');
    CHECK(weivel_c_refusal_release(&refusal) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_get_service(consumer.context, "p:other", &service, &refusal) == WEIVEL_C_STATUS_REFUSED);
    CHECK(refusal.code == WEIVEL_REFUSAL_NONE && refusal.detail == NULL);
    end_world(&in);
}

static void a_want_settles_exactly_once_with_the_providers_pointer(void) {
    world in;
    plugin provider;
    plugin consumer;
    settled_record settled = {0};
    weivel_c_want_callback callback = settling(&settled);
    weivel_handle want = 0;
    weivel_handle registration = 0;
    begin_pair(&in, &provider, &consumer, 1);
    CHECK(weivel_c_context_want_service(consumer.context, "p:a", NULL, &callback, &want, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(want != 0 && settled.count == 0 && records_of(&in) == 1);
    CHECK(weivel_c_context_register_service(provider.context, "p:a", &first_object, NULL, &registration, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(settled.count == 1 && settled.service == &first_object && settled.want == want);
    CHECK(settled.code == WEIVEL_REFUSAL_NONE && settled.degraded == 0);
    check_text(settled.id, "p:a", "the id a settlement names");
    CHECK(weivel_c_context_unregister_service(provider.context, registration, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_register_service(provider.context, "p:a", &second_object, NULL, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(settled.count == 1 && settled.service == &first_object);
    CHECK(records_of(&in) == 1);
    end_world(&in);
}

static void a_want_already_satisfiable_and_one_rejected_at_once_settle_through_the_callback_too(void) {
    world in;
    plugin provider;
    plugin consumer;
    settled_record served = {0};
    settled_record rejected = {0};
    weivel_c_want_callback serving = settling(&served);
    weivel_c_want_callback rejecting = settling(&rejected);
    begin_pair(&in, &provider, &consumer, 1);
    CHECK(weivel_c_context_register_service(provider.context, "p:a", &first_object, NULL, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_want_service(consumer.context, "p:a", NULL, &serving, NULL, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(served.count == 1 && served.service == &first_object);
    CHECK(weivel_c_context_want_service(consumer.context, "p:other", NULL, &rejecting, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(rejected.count == 1 && rejected.service == NULL);
    CHECK(rejected.code == WEIVEL_REFUSAL_UNDECLARED && rejected.has_fix == 1 && rejected.fix[0] != '\0');
    check_text(rejected.subject, "p:other", "the subject of a want rejected at once");
    CHECK(records_of(&in) == 1);
    CHECK(weivel_c_context_want_service(consumer.context, "p:a", NULL, NULL, NULL, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_context_want_service(consumer.context, "", NULL, &serving, NULL, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(served.count == 1);
    end_world(&in);
}

static void a_want_waits_without_bound_unless_the_plugin_asks_for_a_timeout(void) {
    world in;
    plugin provider;
    plugin consumer;
    settled_record settled = {0};
    weivel_c_want_callback callback = settling(&settled);
    int64_t deadline = 0;
    begin_pair(&in, &provider, &consumer, 1);
    forget_every_crossing();
    CHECK(weivel_c_context_want_service(consumer.context, "p:a", NULL, &callback, NULL, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(wants_asked == 1 && asked_with_options == 0);
    CHECK(in.clock.reads == 0);
    CHECK(weivel_c_timer_deadline(in.timer, &deadline) == WEIVEL_C_STATUS_NONE);
    CHECK(weivel_c_core_advance(in.core, INT64_MAX / 2, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(settled.count == 0);
    end_world(&in);
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

static weivel_number speed_of(double value) {
    weivel_number number = WEIVEL_INIT(weivel_number);
    number.name = "speed";
    number.value = value;
    return number;
}

/** A provider `p` of `p:a` and a consumer `c` that needs a speed of 5 of it, on a core that knows what speed is. */
typedef struct constrained_pair {
    weivel_constraint_entry entry;
    weivel_number constraint;
    weivel_number advertised;
    plugin provider;
    plugin consumer;
} constrained_pair;

static void name_constrained_pair(constrained_pair *pair) {
    weivel_service_want *want;
    pair->constraint = speed_of(5.0);
    name_plugin(&pair->provider, "p");
    provides(&pair->provider, "p:a");
    name_plugin(&pair->consumer, "c");
    want = consumes(&pair->consumer, "p:a");
    want->constraints.items = &pair->constraint;
    want->constraints.count = 1;
}

static void begin_constrained(world *in, constrained_pair *pair) {
    weivel_core_options core_options = options_with_a_speed_constraint(&pair->entry);
    begin_world(in, &core_options, 1);
    open_in(in, &pair->provider);
    open_in(in, &pair->consumer);
}

static void a_timeout_a_budget_and_a_poll_interval_reach_the_core_unchanged_with_the_hosts_time(void) {
    world in;
    constrained_pair pair;
    settled_record settled = {0};
    weivel_c_want_callback callback = settling(&settled);
    weivel_c_want_options options = WEIVEL_INIT(weivel_c_want_options);
    void *service = NULL;
    name_constrained_pair(&pair);
    consumes(&pair.consumer, "p:b");
    begin_constrained(&in, &pair);
    in.clock.now = 7000;
    options.timed = WEIVEL_CHOICE_YES;
    options.timeout_millis = 250;
    forget_every_crossing();
    CHECK(weivel_c_context_want_service(pair.consumer.context, "p:b", &options, &callback, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(wants_asked == 1 && asked_with_options == 1);
    CHECK(asked_options.timed == WEIVEL_CHOICE_YES && asked_options.timeout_millis == 250);
    CHECK(asked_options.now == 7000);
    CHECK(asked_options.budgeted == 0 && asked_options.budget_millis == 0 && asked_options.poll_millis == 0);
    in.clock.now = 7250;
    CHECK(weivel_c_context_get_service(pair.consumer.context, "p:a", &service, NULL) == WEIVEL_C_STATUS_NONE);
    CHECK(settled.count == 1 && settled.code == WEIVEL_REFUSAL_TIMED_OUT);
    check_text(settled.subject, "p:b", "the subject of a want that timed out");
    options = (weivel_c_want_options)WEIVEL_INIT(weivel_c_want_options);
    options.budgeted = WEIVEL_CHOICE_YES;
    options.budget_millis = 900;
    options.poll_millis = 30;
    CHECK(weivel_c_context_want_service(pair.consumer.context, "p:a", &options, &callback, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(wants_asked == 2);
    CHECK(asked_options.budgeted == WEIVEL_CHOICE_YES && asked_options.budget_millis == 900);
    CHECK(asked_options.poll_millis == 30 && asked_options.now == 7250);
    CHECK(asked_options.timed == 0 && asked_options.timeout_millis == 0);
    end_world(&in);
}

static void without_a_clock_a_want_that_names_a_time_is_the_callers_mistake_and_never_reaches_the_core(void) {
    world in;
    plugin provider;
    plugin consumer;
    settled_record settled = {0};
    weivel_c_want_callback callback = settling(&settled);
    weivel_c_want_options timed = WEIVEL_INIT(weivel_c_want_options);
    weivel_c_want_options budgeted = WEIVEL_INIT(weivel_c_want_options);
    weivel_c_want_options polled = WEIVEL_INIT(weivel_c_want_options);
    weivel_c_want_options untimed = WEIVEL_INIT(weivel_c_want_options);
    weivel_handle want = 77;
    begin_pair(&in, &provider, &consumer, 0);
    timed.timed = WEIVEL_CHOICE_YES;
    timed.timeout_millis = 100;
    budgeted.budgeted = WEIVEL_CHOICE_YES;
    polled.poll_millis = 10;
    forget_every_crossing();
    CHECK(weivel_c_context_want_service(consumer.context, "p:a", &timed, &callback, &want, NULL) ==
          WEIVEL_C_STATUS_MISUSE);
    CHECK(want == 0);
    CHECK(weivel_c_context_want_service(consumer.context, "p:a", &budgeted, &callback, NULL, NULL) ==
          WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_context_want_service(consumer.context, "p:a", &polled, &callback, NULL, NULL) ==
          WEIVEL_C_STATUS_MISUSE);
    CHECK(every_crossing() == 0 && wants_asked == 0);
    CHECK(records_of(&in) == 0 && settled.count == 0);
    CHECK(weivel_c_context_want_service(consumer.context, "p:a", &untimed, &callback, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(wants_asked == 1);
    end_world(&in);
}

static void a_want_rejected_because_its_plugin_stopped_is_still_delivered_to_the_plugin(void) {
    world in;
    plugin provider;
    plugin consumer;
    settled_record settled = {0};
    weivel_c_want_callback callback = settling(&settled);
    begin_pair(&in, &provider, &consumer, 1);
    CHECK(weivel_c_context_want_service(consumer.context, "p:a", NULL, &callback, NULL, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_release_plugin(in.core, consumer.handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(settled.count == 1 && settled.service == NULL && settled.code == WEIVEL_REFUSAL_STOPPED);
    check_text(settled.subject, "p:a", "the subject of a want rejected as stopped");
    end_entries_of(&in, &consumer);
    CHECK(settled.count == 1 && records_of(&in) == 0);
    end_world(&in);
}

static void a_withdrawn_want_never_settles(void) {
    world in;
    plugin provider;
    plugin consumer;
    settled_record withdrawn = {0};
    settled_record kept = {0};
    weivel_c_want_callback withdrawing = settling(&withdrawn);
    weivel_c_want_callback keeping = settling(&kept);
    weivel_handle want = 0;
    weivel_handle other = 0;
    begin_pair(&in, &provider, &consumer, 1);
    CHECK(weivel_c_context_want_service(consumer.context, "p:a", NULL, &withdrawing, &want, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_want_service(consumer.context, "p:a", NULL, &keeping, &other, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_withdraw_want(provider.context, want, NULL) == WEIVEL_C_STATUS_NONE);
    CHECK(weivel_c_context_withdraw_want(consumer.context, want, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(records_of(&in) == 1);
    forget_every_crossing();
    CHECK(weivel_c_context_withdraw_want(consumer.context, want, NULL) == WEIVEL_C_STATUS_NONE);
    CHECK(every_crossing() == 0);
    CHECK(weivel_c_context_register_service(provider.context, "p:a", &first_object, NULL, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(withdrawn.count == 0);
    CHECK(kept.count == 1 && kept.service == &first_object);
    CHECK(weivel_c_context_withdraw_want(consumer.context, other, NULL) == WEIVEL_C_STATUS_NONE);
    end_world(&in);
}

static void a_watch_hears_each_registration_with_the_object_and_each_unregistration_with_nothing(void) {
    world in;
    plugin provider;
    plugin consumer;
    watched_record heard = {0};
    weivel_c_service_watcher watcher = watching_services(&heard);
    weivel_handle watch = 0;
    weivel_handle registration = 0;
    begin_pair(&in, &provider, &consumer, 1);
    CHECK(weivel_c_context_watch_service(consumer.context, "p:a", &watcher, &watch, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(watch != 0 && heard.count == 0);
    CHECK(weivel_c_context_register_service(provider.context, "p:a", &first_object, NULL, &registration, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_unregister_service(provider.context, registration, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_register_service(provider.context, "p:a", &second_object, NULL, &registration, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(heard.count == 3);
    CHECK(heard.changes[0] == WEIVEL_CHANGE_REGISTERED && heard.objects[0] == &first_object);
    CHECK(heard.changes[1] == WEIVEL_CHANGE_UNREGISTERED && heard.objects[1] == NULL);
    CHECK(heard.changes[2] == WEIVEL_CHANGE_REGISTERED && heard.objects[2] == &second_object);
    check_text(heard.id, "p:a", "the id a change names");
    CHECK(weivel_c_context_stop_watching(provider.context, watch, NULL) == WEIVEL_C_STATUS_NONE);
    CHECK(weivel_c_context_stop_watching(consumer.context, watch, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_unregister_service(provider.context, registration, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(heard.count == 3);
    CHECK(weivel_c_context_stop_watching(consumer.context, watch, NULL) == WEIVEL_C_STATUS_NONE);
    CHECK(records_of(&in) == 0);
    end_world(&in);
}

static void the_ids_listed_are_those_the_plugin_may_reach_that_are_registered_now(void) {
    world in;
    plugin provider;
    plugin consumer;
    weivel_service_list list = WEIVEL_INIT(weivel_service_list);
    weivel_handle registration = 0;
    begin_world(&in, NULL, 1);
    name_plugin(&provider, "p");
    provides(&provider, "p:a");
    provides(&provider, "p:b");
    provides(&provider, "p:hidden");
    name_plugin(&consumer, "c");
    consumes(&consumer, "p:a");
    consumes(&consumer, "q:absent");
    consumes(&consumer, "p:b");
    open_in(&in, &provider);
    open_in(&in, &consumer);
    CHECK(weivel_c_context_register_service(provider.context, "p:b", &first_object, NULL, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_register_service(provider.context, "p:a", &first_object, NULL, &registration, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_register_service(provider.context, "p:hidden", &first_object, NULL, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_service_ids(consumer.context, &list, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(list.ids.count == 2);
    if (list.ids.count == 2) {
        check_text(list.ids.items[0], "p:a", "the first id listed");
        check_text(list.ids.items[1], "p:b", "the second id listed");
    }
    CHECK(weivel_c_context_service_ids_release(consumer.context, &list) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_unregister_service(provider.context, registration, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_service_ids(consumer.context, &list, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(list.ids.count == 1);
    CHECK(weivel_c_context_service_ids_release(consumer.context, &list) == WEIVEL_C_STATUS_OK);
    end_world(&in);
}

static void a_registration_lasts_until_the_plugin_unregisters_it_or_stops(void) {
    world in;
    plugin provider;
    plugin consumer;
    weivel_c_refusal refusal = WEIVEL_INIT(weivel_c_refusal);
    weivel_handle registration = 0;
    void *service = NULL;
    begin_pair(&in, &provider, &consumer, 1);
    CHECK(weivel_c_context_register_service(provider.context, "p:a", NULL, NULL, &registration, NULL) ==
          WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_context_register_service(provider.context, "elsewhere:x", &first_object, NULL, &registration,
                                            &refusal) == WEIVEL_C_STATUS_REFUSED);
    CHECK(registration == 0 && refusal.code == WEIVEL_REFUSAL_OUTSIDE_NAMESPACE);
    check_text(refusal.subject, "elsewhere:x", "the subject of a refused registration");
    CHECK(weivel_c_refusal_release(&refusal) == WEIVEL_C_STATUS_OK);
    CHECK(records_of(&in) == 0);
    CHECK(weivel_c_context_register_service(provider.context, "p:a", &first_object, NULL, &registration, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(registration != 0 && records_of(&in) == 1);
    CHECK(weivel_c_context_unregister_service(consumer.context, registration, NULL) == WEIVEL_C_STATUS_NONE);
    CHECK(weivel_c_context_get_service(consumer.context, "p:a", &service, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_unregister_service(provider.context, registration, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_get_service(consumer.context, "p:a", &service, NULL) == WEIVEL_C_STATUS_NONE);
    CHECK(records_of(&in) == 0 && holdings_of(&in) == 0);
    forget_every_crossing();
    CHECK(weivel_c_context_unregister_service(provider.context, registration, NULL) == WEIVEL_C_STATUS_NONE);
    CHECK(every_crossing() == 0);
    CHECK(weivel_c_context_register_service(provider.context, "p:a", &second_object, NULL, &registration, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_get_service(consumer.context, "p:a", &service, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(service == &second_object);
    CHECK(weivel_c_core_release_plugin(in.core, provider.handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_get_service(consumer.context, "p:a", &service, NULL) == WEIVEL_C_STATUS_NONE);
    end_world(&in);
}

/** A provider's answerer: what it was asked, what it answers, and the status it answers with. */
typedef struct answering {
    uint32_t questions[WORLD_RECORDS];
    uint32_t constraint_counts[WORLD_RECORDS];
    double constraint_values[WORLD_RECORDS];
    uint32_t count;
    weivel_number live;
    uint32_t serves;
    weivel_c_status answer;
    char id[WORLD_TEXT];
    void (*react)(void *more);
    void *more;
} answering;

static weivel_c_status answer_capacity(void *data, const weivel_c_capacity_question *question,
                                       weivel_c_capacity_answer *answer) {
    answering *self = data;
    if (self->count < WORLD_RECORDS) {
        self->questions[self->count] = question->question;
        self->constraint_counts[self->count] = question->constraints.count;
        self->constraint_values[self->count] =
            question->constraints.count == 0 ? 0.0 : question->constraints.items[0].value;
    }
    self->count++;
    copy_text(self->id, question->id);
    if (self->react != NULL) {
        self->react(self->more);
    }
    if (question->question == WEIVEL_CAPACITY_ATTRIBUTES) {
        answer->attributes.items = &self->live;
        answer->attributes.count = 1;
    } else {
        answer->serves = self->serves;
    }
    return self->answer;
}

static weivel_c_want_options polling_every(int64_t poll_millis) {
    weivel_c_want_options options = WEIVEL_INIT(weivel_c_want_options);
    options.poll_millis = poll_millis;
    return options;
}

static void each_capacity_question_is_put_to_the_answerer_and_its_answer_reported(void) {
    world in;
    constrained_pair pair;
    answering provider = {0};
    weivel_c_capacity_answerer answerer = {answer_capacity, &provider};
    settled_record settled = {0};
    weivel_c_want_callback callback = settling(&settled);
    weivel_c_want_options options = polling_every(10);
    name_constrained_pair(&pair);
    begin_constrained(&in, &pair);
    provider.live = speed_of(9.0);
    provider.serves = WEIVEL_CHOICE_YES;
    CHECK(weivel_c_context_register_service(pair.provider.context, "p:a", &first_object, &answerer, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    forget_every_crossing();
    CHECK(weivel_c_context_want_service(pair.consumer.context, "p:a", &options, &callback, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(provider.count == 2);
    CHECK(provider.questions[0] == WEIVEL_CAPACITY_ATTRIBUTES && provider.questions[1] == WEIVEL_CAPACITY_SERVE);
    CHECK(provider.constraint_counts[1] == 1 && provider.constraint_values[1] == 5.0);
    check_text(provider.id, "p:a", "the id a capacity question names");
    CHECK(capacities_reported == 2 && reported_capacity.serves == WEIVEL_CHOICE_YES);
    CHECK(settled.count == 1 && settled.service == &first_object && settled.degraded == 0);
    CHECK(in.sink.count == 0);
    end_world(&in);
}

static void a_registration_that_offered_no_answerer_answers_at_once_nothing_live_and_that_it_serves(void) {
    world in;
    constrained_pair pair;
    settled_record settled = {0};
    weivel_c_want_callback callback = settling(&settled);
    weivel_c_want_options options = polling_every(10);
    name_constrained_pair(&pair);
    pair.advertised = speed_of(9.0);
    pair.provider.offers[0].attributes.items = &pair.advertised;
    pair.provider.offers[0].attributes.count = 1;
    begin_constrained(&in, &pair);
    CHECK(weivel_c_context_register_service(pair.provider.context, "p:a", &first_object, NULL, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    forget_every_crossing();
    CHECK(weivel_c_context_want_service(pair.consumer.context, "p:a", &options, &callback, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(capacities_reported == 2);
    CHECK(reported_capacity.attributes.count == 0 && reported_capacity.serves == WEIVEL_CHOICE_YES);
    CHECK(settled.count == 1 && settled.service == &first_object);
    CHECK(in.sink.count == 0);
    end_world(&in);
}

static void an_answerer_that_fails_goes_to_the_sink_and_its_question_is_left_unanswered(void) {
    world in;
    constrained_pair pair;
    answering provider = {0};
    weivel_c_capacity_answerer answerer = {answer_capacity, &provider};
    settled_record settled = {0};
    weivel_c_want_callback callback = settling(&settled);
    weivel_c_want_options options = polling_every(10);
    name_constrained_pair(&pair);
    begin_constrained(&in, &pair);
    provider.live = speed_of(9.0);
    provider.serves = WEIVEL_CHOICE_YES;
    provider.answer = WEIVEL_C_STATUS_FAILED;
    CHECK(weivel_c_context_register_service(pair.provider.context, "p:a", &first_object, &answerer, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    forget_every_crossing();
    CHECK(weivel_c_context_want_service(pair.consumer.context, "p:a", &options, &callback, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(provider.count == 1 && capacities_reported == 0 && settled.count == 0);
    CHECK(in.sink.count == 1 && told_of(&in, WEIVEL_C_FAILURE_CAPACITY_ANSWERER) == 1);
    CHECK(in.sink.items[0].failure.status == WEIVEL_C_STATUS_FAILED);
    CHECK(in.sink.items[0].failure.event_kind == WEIVEL_EVENT_CAPACITY_WANTED);
    end_world(&in);
}

#if !defined(WEIVEL_C_USING_SHARED)

/** An allocator that refuses every allocation of `refuse_from_size` bytes or more while that is not 0. */
typedef struct refusing_memory {
    size_t refuse_from_size;
} refusing_memory;

static void *allocate_unless_refused(void *data, size_t size) {
    refusing_memory *self = data;
    return self->refuse_from_size != 0 && size >= self->refuse_from_size ? NULL : malloc(size);
}

static void release_plainly(void *data, void *memory) {
    (void)data;
    free(memory);
}

static void an_incomplete_question_is_left_unanswered_and_the_sink_told(void) {
    world in;
    constrained_pair pair;
    refusing_memory memory = {0};
    weivel_c_allocator allocator = {allocate_unless_refused, release_plainly, &memory};
    weivel_core_options core_options = options_with_a_speed_constraint(&pair.entry);
    answering provider = {0};
    weivel_c_capacity_answerer answerer = {answer_capacity, &provider};
    settled_record settled = {0};
    weivel_c_want_callback callback = settling(&settled);
    weivel_c_want_options options = polling_every(10);
    name_constrained_pair(&pair);
    begin_world_core(&in, &core_options);
    weivel_c_core_replace_allocator(in.core, &allocator);
    begin_world_rest(&in, 1);
    open_in(&in, &pair.provider);
    open_in(&in, &pair.consumer);
    provider.live = speed_of(9.0);
    provider.serves = WEIVEL_CHOICE_YES;
    CHECK(weivel_c_context_register_service(pair.provider.context, "p:a", &first_object, &answerer, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    forget_every_crossing();
    memory.refuse_from_size = sizeof(weivel_c_queued_event) + 1;
    CHECK(weivel_c_context_want_service(pair.consumer.context, "p:a", &options, &callback, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    memory.refuse_from_size = 0;
    CHECK(count_heard(&in.events, WEIVEL_EVENT_CAPACITY_WANTED) == 1);
    CHECK(provider.count == 0 && capacities_reported == 0 && settled.count == 0);
    CHECK(told_of(&in, WEIVEL_C_FAILURE_EVENT_INCOMPLETE) >= 1);
    end_world(&in);
}

static void an_incomplete_question_is_not_answered_for_a_registration_without_an_answerer_either(void) {
    world in;
    constrained_pair pair;
    refusing_memory memory = {0};
    weivel_c_allocator allocator = {allocate_unless_refused, release_plainly, &memory};
    weivel_core_options core_options = options_with_a_speed_constraint(&pair.entry);
    settled_record settled = {0};
    weivel_c_want_callback callback = settling(&settled);
    weivel_c_want_options options = polling_every(10);
    name_constrained_pair(&pair);
    begin_world_core(&in, &core_options);
    weivel_c_core_replace_allocator(in.core, &allocator);
    begin_world_rest(&in, 1);
    open_in(&in, &pair.provider);
    open_in(&in, &pair.consumer);
    CHECK(weivel_c_context_register_service(pair.provider.context, "p:a", &first_object, NULL, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    forget_every_crossing();
    memory.refuse_from_size = sizeof(weivel_c_queued_event) + 1;
    CHECK(weivel_c_context_want_service(pair.consumer.context, "p:a", &options, &callback, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    memory.refuse_from_size = 0;
    CHECK(count_heard(&in.events, WEIVEL_EVENT_CAPACITY_WANTED) == 1);
    CHECK(capacities_reported == 0 && settled.count == 0);
    end_world(&in);
}

/** An allocator that fails the one allocation it is told to, and every one while `fails_all` is set. */
typedef struct scarce_memory {
    uint32_t allocations;
    uint32_t fail_at;
    int fails_all;
} scarce_memory;

static void *allocate_scarcely(void *data, size_t size) {
    scarce_memory *self = data;
    self->allocations++;
    return self->fails_all || self->allocations == self->fail_at ? NULL : malloc(size);
}

/** What a listener found in the handles table for the handles an event named, as the event was heard. */
typedef struct naming {
    weivel_c_handles *handles;
    uint32_t named;
    uint32_t lacking;
} naming;

static void look_up_named(naming *self, weivel_handle handle) {
    weivel_c_handle_entry entry = WEIVEL_INIT(weivel_c_handle_entry);
    self->named++;
    self->lacking += weivel_c_handles_find(self->handles, handle, &entry) != WEIVEL_C_STATUS_OK;
}

static weivel_c_status look_up_what_an_event_names(hearing *self, weivel_c_core *core, const weivel_c_event *event) {
    naming *names = self->more;
    (void)core;
    if (event->kind == WEIVEL_EVENT_WATCH && event->data.watch.change == WEIVEL_CHANGE_REGISTERED) {
        look_up_named(names, event->data.watch.watch);
        look_up_named(names, event->data.watch.implementation);
    } else if (event->kind == WEIVEL_EVENT_WANT_SETTLED) {
        look_up_named(names, event->data.want.want);
    }
    return WEIVEL_C_STATUS_OK;
}

/** Fills the table until its next addition must grow it, so the next allocation it makes is one a test can fail. */
static void bring_the_table_to_the_brink(world *in, scarce_memory *memory) {
    weivel_c_handle_entry filler = WEIVEL_INIT(weivel_c_handle_entry);
    weivel_c_status reserved = WEIVEL_C_STATUS_OK;
    filler.kind = WEIVEL_C_HANDLE_WATCH;
    filler.object = &first_object;
    while (reserved == WEIVEL_C_STATUS_OK) {
        memory->fails_all = 1;
        reserved = weivel_c_handles_reserve(in->handles);
        memory->fails_all = 0;
        if (reserved == WEIVEL_C_STATUS_OK) {
            filler.handle += 1;
            CHECK(weivel_c_handles_add_reserved(in->handles, &filler, NULL) == WEIVEL_C_STATUS_OK);
        }
    }
    CHECK(reserved == WEIVEL_C_STATUS_OUT_OF_MEMORY);
}

enum { SCARCE_REGISTER = 0, SCARCE_WANT = 1, SCARCE_WATCH = 2, SCARCE_OPERATIONS = 3 };

static weivel_c_status make_scarcely(uint32_t operation, plugin *provider, plugin *consumer, settled_record *settled,
                                     watched_record *watched) {
    weivel_c_want_callback callback = settling(settled);
    weivel_c_service_watcher watcher = watching_services(watched);
    if (operation == SCARCE_REGISTER) {
        return weivel_c_context_register_service(provider->context, "p:a", &first_object, NULL, NULL, NULL);
    }
    if (operation == SCARCE_WANT) {
        return weivel_c_context_want_service(consumer->context, "p:a", NULL, &callback, NULL, NULL);
    }
    return weivel_c_context_watch_service(consumer->context, "p:b", &watcher, NULL, NULL);
}

/**
 * Makes one registration, want or watch with the allocation `failing` allocations ahead refused. Answers 1 when it
 * was made, and then every event that named its handle found the entry; and 0 when it failed, and then before the
 * core was asked anything.
 */
static int is_made_whole_or_not_at_all(uint32_t operation, uint32_t failing) {
    world in;
    plugin provider;
    plugin consumer;
    scarce_memory memory = {0, 0, 0};
    weivel_c_allocator allocator = {allocate_scarcely, release_plainly, &memory};
    naming names = {NULL, 0, 0};
    settled_record settled = {0};
    watched_record watched = {0};
    weivel_c_service_watcher watcher = watching_services(&watched);
    weivel_c_status status;
    begin_world(&in, NULL, 1);
    weivel_c_handles_replace_allocator(in.handles, &allocator);
    weivel_c_contexts_replace_allocator(in.contexts, &allocator);
    name_plugin(&provider, "p");
    provides(&provider, "p:a");
    provides(&provider, "p:b");
    name_plugin(&consumer, "c");
    consumes(&consumer, "p:a");
    consumes(&consumer, "p:b");
    open_in(&in, &provider);
    open_in(&in, &consumer);
    if (operation == SCARCE_REGISTER) {
        CHECK(weivel_c_context_watch_service(consumer.context, "p:a", &watcher, NULL, NULL) == WEIVEL_C_STATUS_OK);
    } else if (operation == SCARCE_WANT) {
        CHECK(weivel_c_context_register_service(provider.context, "p:a", &first_object, NULL, NULL, NULL) ==
              WEIVEL_C_STATUS_OK);
    }
    bring_the_table_to_the_brink(&in, &memory);
    names.handles = in.handles;
    in.events.react = look_up_what_an_event_names;
    in.events.more = &names;
    forget_every_crossing();
    memory.fail_at = memory.allocations + failing;
    status = make_scarcely(operation, &provider, &consumer, &settled, &watched);
    memory.fail_at = 0;
    if (status == WEIVEL_C_STATUS_OK && operation == SCARCE_WATCH) {
        CHECK(weivel_c_context_register_service(provider.context, "p:b", &second_object, NULL, NULL, NULL) ==
              WEIVEL_C_STATUS_OK);
    }
    if (status == WEIVEL_C_STATUS_OK) {
        CHECK(names.named >= 1 && names.lacking == 0);
        CHECK(settled.count + watched.count == 1);
    } else {
        CHECK(status == WEIVEL_C_STATUS_OUT_OF_MEMORY);
        CHECK(every_crossing() == 0 && names.named == 0);
    }
    in.events.react = NULL;
    end_world(&in);
    return status == WEIVEL_C_STATUS_OK;
}

static void a_registration_a_want_and_a_watch_are_made_whole_or_fail_before_the_core_is_asked(void) {
    uint32_t operation;
    uint32_t failing;
    for (operation = 0; operation < SCARCE_OPERATIONS; operation++) {
        uint32_t made = 0;
        uint32_t failed = 0;
        for (failing = 1; failing <= 5; failing++) {
            if (is_made_whole_or_not_at_all(operation, failing)) {
                made++;
            } else {
                failed++;
            }
        }
        CHECK(failed >= 2 && made >= 1);
    }
}

static void run_the_tests_of_the_layers_own_seams(void) {
    an_incomplete_question_is_left_unanswered_and_the_sink_told();
    an_incomplete_question_is_not_answered_for_a_registration_without_an_answerer_either();
    a_registration_a_want_and_a_watch_are_made_whole_or_fail_before_the_core_is_asked();
}

#else

static void run_the_tests_of_the_layers_own_seams(void) {
}

#endif

/** Two providers of the capability `screens`, a consumer of it and a plugin that declares nothing. */
typedef struct screens {
    plugin first;
    plugin second;
    plugin consumer;
    plugin stranger;
} screens;

static void begin_screens(world *in, screens *with) {
    begin_world(in, NULL, 1);
    name_plugin(&with->first, "a");
    with->first.capabilities_provided[with->first.capabilities_provided_count++] = "screens";
    name_plugin(&with->second, "b");
    with->second.capabilities_provided[with->second.capabilities_provided_count++] = "screens";
    name_plugin(&with->consumer, "c");
    with->consumer.capabilities_consumed[with->consumer.capabilities_consumed_count++] = "screens";
    name_plugin(&with->stranger, "d");
    open_in(in, &with->first);
    open_in(in, &with->second);
    open_in(in, &with->consumer);
    open_in(in, &with->stranger);
}

static void a_plugin_provides_lists_and_watches_the_implementations_of_a_capability(void) {
    world in;
    screens with;
    watched_record heard = {0};
    weivel_c_capability_watcher watcher = watching_capabilities(&heard);
    weivel_c_implementations list = WEIVEL_INIT(weivel_c_implementations);
    weivel_c_refusal refusal = WEIVEL_INIT(weivel_c_refusal);
    weivel_handle watch = 0;
    weivel_handle provided = 0;
    begin_screens(&in, &with);
    CHECK(weivel_c_context_watch_capability(with.consumer.context, "screens", &watcher, &watch, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_provide_capability(with.first.context, "screens", &first_object, &provided, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(provided != 0);
    CHECK(weivel_c_context_provide_capability(with.second.context, "screens", &second_object, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_all_capabilities(with.consumer.context, "screens", &list, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(list.count == 0 && heard.count == 0);
    CHECK(weivel_c_core_verify_activation(in.core, with.second.handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_verify_activation(in.core, with.first.handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(heard.count == 2);
    CHECK(heard.changes[0] == WEIVEL_CAPABILITY_PROVIDED && heard.objects[0] == &second_object);
    CHECK(heard.changes[1] == WEIVEL_CAPABILITY_PROVIDED && heard.objects[1] == &first_object);
    check_text(heard.id, "screens", "the capability a change names");
    CHECK(weivel_c_context_all_capabilities(with.consumer.context, "screens", &list, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(list.count == 2);
    if (list.count == 2) {
        CHECK(list.items[0] == &second_object && list.items[1] == &first_object);
    }
    CHECK(weivel_c_context_all_capabilities(with.consumer.context, "screens", &list, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_implementations_release(&list) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_release_plugin(in.core, with.first.handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(heard.count == 3);
    CHECK(heard.changes[2] == WEIVEL_CAPABILITY_WITHDRAWN && heard.objects[2] == &first_object);
    CHECK(weivel_c_context_all_capabilities(with.consumer.context, "screens", &list, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(list.count == 1 && list.items[0] == &second_object);
    CHECK(weivel_c_implementations_release(&list) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_all_capabilities(with.stranger.context, "screens", &list, &refusal) ==
          WEIVEL_C_STATUS_REFUSED);
    CHECK(list.count == 0 && refusal.code == WEIVEL_REFUSAL_UNDECLARED);
    check_text(refusal.subject, "screens", "the subject of a refused capability reach");
    CHECK(weivel_c_refusal_release(&refusal) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_stop_watching(with.consumer.context, watch, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_release_plugin(in.core, with.second.handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(heard.count == 3);
    CHECK(in.sink.count == 0);
    end_world(&in);
}

static void a_plugin_hears_which_object_it_holds_was_revoked(void) {
    world in;
    screens with;
    plugin provider;
    reported_record reported = {0};
    weivel_c_implementations list = WEIVEL_INIT(weivel_c_implementations);
    weivel_handle registration = 0;
    void *service = NULL;
    begin_screens(&in, &with);
    name_plugin(&provider, "p");
    provides(&provider, "p:a");
    open_in(&in, &provider);
    CHECK(weivel_c_core_release_plugin(in.core, with.consumer.handle, NULL) == WEIVEL_C_STATUS_OK);
    end_entries_of(&in, &with.consumer);
    free_context_of(&in, &with.consumer);
    name_plugin(&with.consumer, "c");
    consumes(&with.consumer, "p:a");
    with.consumer.capabilities_consumed[with.consumer.capabilities_consumed_count++] = "screens";
    open_in(&in, &with.consumer);
    listen_to(&with.consumer, &reported);
    CHECK(weivel_c_context_register_service(provider.context, "p:a", &first_object, NULL, &registration, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_get_service(with.consumer.context, "p:a", &service, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_get_service(with.consumer.context, "p:a", &service, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(holdings_of(&in) == 1);
    CHECK(weivel_c_context_unregister_service(provider.context, registration, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(reported.count == 1);
    CHECK(reported.items[0].kind == WEIVEL_C_PLUGIN_EVENT_REVOKED && reported.items[0].object == &first_object);
    CHECK(reported.items[0].reach == WEIVEL_REACH_SERVICE && reported.items[0].code == WEIVEL_REFUSAL_NONE);
    check_text(reported.items[0].id, "p:a", "the service a revocation names");
    CHECK(holdings_of(&in) == 0);
    CHECK(weivel_c_context_provide_capability(with.first.context, "screens", &second_object, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_verify_activation(in.core, with.first.handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_all_capabilities(with.consumer.context, "screens", &list, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(list.count == 1 && holdings_of(&in) == 1);
    CHECK(weivel_c_implementations_release(&list) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_release_plugin(in.core, with.first.handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(reported.count == 2);
    CHECK(reported.items[1].kind == WEIVEL_C_PLUGIN_EVENT_REVOKED && reported.items[1].object == &second_object);
    CHECK(reported.items[1].reach == WEIVEL_REACH_CAPABILITY);
    check_text(reported.items[1].id, "screens", "the capability a revocation names");
    CHECK(holdings_of(&in) == 0);
    end_world(&in);
}

static void a_plugin_hears_a_refusal_of_reach_once(void) {
    world in;
    plugin provider;
    plugin consumer;
    reported_record reported = {0};
    void *service = NULL;
    uint64_t registration;
    begin_pair(&in, &provider, &consumer, 1);
    registration = listen_to(&consumer, &reported);
    CHECK(weivel_c_context_get_service(consumer.context, "p:other", &service, NULL) == WEIVEL_C_STATUS_REFUSED);
    CHECK(weivel_c_context_get_service(consumer.context, "p:other", &service, NULL) == WEIVEL_C_STATUS_REFUSED);
    CHECK(reported.count == 1);
    CHECK(reported.items[0].kind == WEIVEL_C_PLUGIN_EVENT_REACH_REFUSED);
    CHECK(reported.items[0].reach == WEIVEL_REACH_SERVICE && reported.items[0].object == NULL);
    CHECK(reported.items[0].code == WEIVEL_REFUSAL_UNDECLARED && reported.items[0].has_fix == 1);
    check_text(reported.items[0].id, "p:other", "the id a refused reach names");
    check_text(reported.items[0].subject, "p:other", "the subject of a refused reach, as heard");
    CHECK(weivel_c_context_remove_listener(consumer.context, registration) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_remove_listener(consumer.context, registration) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_context_get_service(consumer.context, "p:third", &service, NULL) == WEIVEL_C_STATUS_REFUSED);
    CHECK(reported.count == 1);
    end_world(&in);
}

static void a_plugin_hears_a_constrained_want_that_settled_degraded(void) {
    world in;
    constrained_pair pair;
    answering provider = {0};
    weivel_c_capacity_answerer answerer = {answer_capacity, &provider};
    settled_record settled = {0};
    reported_record reported = {0};
    weivel_c_want_callback callback = settling(&settled);
    weivel_c_want_options options = polling_every(10);
    name_constrained_pair(&pair);
    pair.consumer.wants[0].degradable = WEIVEL_CHOICE_YES;
    begin_constrained(&in, &pair);
    listen_to(&pair.consumer, &reported);
    provider.live = speed_of(1.0);
    provider.serves = WEIVEL_CHOICE_YES;
    options.budgeted = WEIVEL_CHOICE_YES;
    CHECK(weivel_c_context_register_service(pair.provider.context, "p:a", &first_object, &answerer, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_want_service(pair.consumer.context, "p:a", &options, &callback, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(settled.count == 1 && settled.degraded == 1 && settled.service == &first_object);
    check_text(settled.subject, "speed", "the constraint a degraded want accepted unmet");
    CHECK(reported.count == 1 && reported.items[0].kind == WEIVEL_C_PLUGIN_EVENT_DEGRADED);
    check_text(reported.items[0].id, "p:a", "the service a degraded want names");
    check_text(reported.items[0].subject, "speed", "the constraint a degraded event names");
    end_world(&in);
}

static void a_plugins_log_and_the_cores_own_go_to_the_hosts_log(void) {
    world in;
    plugin provider;
    plugin consumer;
    weivel_core_options core_options = WEIVEL_INIT(weivel_core_options);
    weivel_quarantine_reason reason = WEIVEL_INIT(weivel_quarantine_reason);
    core_options.minimum_log_level = WEIVEL_LOG_INFO;
    begin_world(&in, &core_options, 1);
    name_plugin(&provider, "p");
    name_plugin(&consumer, "c");
    open_in(&in, &provider);
    open_in(&in, &consumer);
    CHECK(in.log.count == 0);
    CHECK(weivel_c_context_log(consumer.context, WEIVEL_LOG_ERROR, "the disk is full") == WEIVEL_C_STATUS_OK);
    CHECK(in.log.count == 1 && in.log.items[0].level == WEIVEL_LOG_ERROR);
    CHECK(in.log.items[0].plugin == consumer.handle && in.log.items[0].named);
    check_text(in.log.items[0].name, "c", "the plugin a log line names");
    check_text(in.log.items[0].text, "the disk is full", "the line a plugin logged");
    reason.detail = "its activation failed";
    reason.fix = "fix its activation";
    CHECK(weivel_c_core_quarantine_plugin(in.core, provider.handle, &reason, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(in.log.count == 2 && in.log.items[1].level == WEIVEL_LOG_INFO);
    CHECK(in.log.items[1].plugin == 0 && !in.log.items[1].named && in.log.items[1].text[0] != '\0');
    CHECK(in.sink.count == 0);
    in.log.answer = WEIVEL_C_STATUS_FAILED;
    CHECK(weivel_c_context_log(consumer.context, WEIVEL_LOG_INFO, "again") == WEIVEL_C_STATUS_OK);
    CHECK(in.log.count == 3 && in.sink.count == 1 && told_of(&in, WEIVEL_C_FAILURE_LOG) == 1);
    CHECK(weivel_c_context_log(consumer.context, WEIVEL_LOG_INFO, NULL) == WEIVEL_C_STATUS_MISUSE);
    end_world(&in);
}

static void a_plugin_reads_its_manifest_value_as_the_host_registered_it(void) {
    world in;
    plugin provider;
    plugin consumer;
    weivel_c_context *of_the_host = NULL;
    const weivel_manifest *manifest = NULL;
    begin_pair(&in, &provider, &consumer, 1);
    CHECK(weivel_c_context_manifest(consumer.context, &manifest) == WEIVEL_C_STATUS_OK);
    CHECK(manifest == &consumer.manifest);
    check_text(manifest->services.consumes.items[0].id, "p:a", "what the manifest says the plugin consumes");
    CHECK(weivel_c_context_create(in.contexts, WEIVEL_HOST, NULL, &of_the_host) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_manifest(of_the_host, &manifest) == WEIVEL_C_STATUS_NONE && manifest == NULL);
    CHECK(weivel_c_context_manifest(consumer.context, NULL) == WEIVEL_C_STATUS_MISUSE);
    end_world(&in);
}

/** Mirrors the layer vector consume/a-throwing-listener-does-not-silence-the-others. */
static void a_failing_listener_goes_to_the_sink_and_does_not_silence_the_others(void) {
    world in;
    plugin calendar;
    plugin bad;
    plugin agenda;
    watched_record heard_by_bad = {0};
    watched_record heard_by_agenda = {0};
    weivel_c_capability_watcher failing = watching_capabilities(&heard_by_bad);
    weivel_c_capability_watcher hearing_well = watching_capabilities(&heard_by_agenda);
    begin_world(&in, NULL, 1);
    name_plugin(&calendar, "calendar");
    calendar.capabilities_provided[calendar.capabilities_provided_count++] = "screens";
    name_plugin(&bad, "bad");
    bad.capabilities_consumed[bad.capabilities_consumed_count++] = "screens";
    name_plugin(&agenda, "agenda");
    agenda.capabilities_consumed[agenda.capabilities_consumed_count++] = "screens";
    open_in(&in, &calendar);
    open_in(&in, &bad);
    open_in(&in, &agenda);
    heard_by_bad.answer = WEIVEL_C_STATUS_FAILED;
    CHECK(weivel_c_context_watch_capability(bad.context, "screens", &failing, NULL, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_watch_capability(agenda.context, "screens", &hearing_well, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_provide_capability(calendar.context, "screens", &first_object, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    forget_every_crossing();
    in.events.count = 0;
    CHECK(weivel_c_core_verify_activation(in.core, calendar.handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(heard_by_bad.count == 1 && heard_by_bad.changes[0] == WEIVEL_CAPABILITY_PROVIDED);
    CHECK(heard_by_bad.objects[0] == &first_object);
    CHECK(heard_by_agenda.count == 1 && heard_by_agenda.changes[0] == WEIVEL_CAPABILITY_PROVIDED);
    CHECK(heard_by_agenda.objects[0] == &first_object);
    CHECK(in.sink.count == 1 && told_of(&in, WEIVEL_C_FAILURE_LISTENER) == 1);
    CHECK(in.sink.items[0].failure.status == WEIVEL_C_STATUS_FAILED);
    CHECK(in.sink.items[0].failure.event_kind == WEIVEL_EVENT_CAPABILITY);
    CHECK(count_heard(&in.events, WEIVEL_EVENT_REFUSAL) == 0);
    CHECK(count_heard(&in.events, WEIVEL_EVENT_REACH_REFUSED) == 0);
    CHECK(other_crossings == 1);
    end_world(&in);
}

static void each_listener_of_a_plugin_hears_every_event_in_order_whatever_another_answers(void) {
    world in;
    plugin provider;
    plugin consumer;
    reported_record first = {0};
    reported_record failing = {0};
    reported_record last = {0};
    void *service = NULL;
    begin_pair(&in, &provider, &consumer, 1);
    listen_to(&consumer, &first);
    listen_to(&consumer, &failing);
    listen_to(&consumer, &last);
    failing.answer = WEIVEL_C_STATUS_FAILED;
    CHECK(weivel_c_context_get_service(consumer.context, "p:x", &service, NULL) == WEIVEL_C_STATUS_REFUSED);
    CHECK(weivel_c_context_get_service(consumer.context, "p:y", &service, NULL) == WEIVEL_C_STATUS_REFUSED);
    CHECK(first.count == 2 && failing.count == 2 && last.count == 2);
    check_text(first.items[0].id, "p:x", "what the first listener heard first");
    check_text(last.items[0].id, "p:x", "what the last listener heard first");
    check_text(last.items[1].id, "p:y", "what the last listener heard second");
    CHECK(in.sink.count == 2 && told_of(&in, WEIVEL_C_FAILURE_LISTENER) == 2);
    end_world(&in);
}

/** What a released plugin's callbacks heard, in the order they heard it, with the host's own steps between. */
typedef struct trace {
    char lines[WORLD_RECORDS][WORLD_TEXT];
    uint32_t count;
} trace;

static void trace_line(trace *into, const char *line) {
    if (into->count < WORLD_RECORDS) {
        snprintf(into->lines[into->count], WORLD_TEXT, "%s", line);
    }
    into->count++;
}

typedef struct releasing {
    world *in;
    plugin *provider;
    plugin *consumer;
    trace heard;
    weivel_c_step ending;
    uint32_t records_after_release;
    uint32_t lines_after_release;
    weivel_handle registration;
} releasing;

static weivel_c_status trace_settlement(void *data, const weivel_c_settlement *settlement) {
    releasing *self = data;
    trace_line(&self->heard, settlement->refusal.code == WEIVEL_REFUSAL_STOPPED ? "rejected stopped" : "settled");
    return WEIVEL_C_STATUS_OK;
}

static weivel_c_status trace_plugin_event(void *data, const weivel_c_plugin_event *event) {
    releasing *self = data;
    trace_line(&self->heard, event->kind == WEIVEL_C_PLUGIN_EVENT_REVOKED ? "revoked" : "reported");
    return WEIVEL_C_STATUS_OK;
}

static weivel_c_status end_the_consumers_entries(void *data, weivel_c_core *core) {
    releasing *self = data;
    (void)core;
    trace_line(&self->heard, "entries ended");
    return weivel_c_context_end_entries(self->consumer->context);
}

/**
 * A release made inside a dispatch, as a host makes it: the core's release, then the ending of the plugin's entries
 * deferred behind the events that release queued, then a late reach the core refuses, whose report is queued behind
 * that ending and so is not heard, and a late get whose service is then unregistered.
 */
static weivel_c_status release_the_consumer(void *data, weivel_c_core *core) {
    releasing *self = data;
    void *service = NULL;
    CHECK(weivel_c_core_release_plugin(core, self->consumer->handle, NULL) == WEIVEL_C_STATUS_OK);
    self->records_after_release = records_of(self->in);
    self->lines_after_release = self->heard.count;
    CHECK(weivel_c_core_defer(core, &self->ending, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_get_service(self->consumer->context, "p:undeclared", &service, NULL) ==
          WEIVEL_C_STATUS_REFUSED);
    CHECK(weivel_c_context_get_service(self->consumer->context, "p:a", &service, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(service == &first_object);
    CHECK(weivel_c_context_unregister_service(self->provider->context, self->registration, NULL) ==
          WEIVEL_C_STATUS_OK);
    return WEIVEL_C_STATUS_OK;
}

static void a_released_plugin_hears_what_its_release_queued_and_nothing_queued_after(void) {
    world in;
    plugin provider;
    plugin consumer;
    releasing release = {0};
    watched_record watched = {0};
    weivel_c_service_watcher watcher = watching_services(&watched);
    weivel_c_want_callback callback = {trace_settlement, &release};
    weivel_c_plugin_listener listener = {trace_plugin_event, &release};
    uint64_t registration = 0;
    void *service = NULL;
    begin_world(&in, NULL, 1);
    name_plugin(&provider, "p");
    provides(&provider, "p:a");
    name_plugin(&consumer, "c");
    consumes(&consumer, "p:a");
    consumes(&consumer, "q:never");
    open_in(&in, &provider);
    open_in(&in, &consumer);
    release.in = &in;
    release.provider = &provider;
    release.consumer = &consumer;
    release.ending = (weivel_c_step)WEIVEL_INIT(weivel_c_step);
    release.ending.run = end_the_consumers_entries;
    release.ending.data = &release;
    CHECK(weivel_c_context_register_service(provider.context, "p:a", &first_object, NULL, &release.registration,
                                            NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_add_listener(consumer.context, &listener, &registration) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_watch_service(consumer.context, "p:a", &watcher, NULL, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_want_service(consumer.context, "q:never", NULL, &callback, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_get_service(consumer.context, "p:a", &service, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(records_of(&in) == 3);
    in_dispatch(&in, release_the_consumer, &release);
    CHECK(release.records_after_release == 3 && release.lines_after_release == 0);
    CHECK(release.heard.count == 3);
    if (release.heard.count == 3) {
        check_text(release.heard.lines[0], "rejected stopped", "what the released plugin heard first");
        check_text(release.heard.lines[1], "revoked", "what the released plugin heard second");
        check_text(release.heard.lines[2], "entries ended", "what came after everything the release queued");
    }
    CHECK(records_of(&in) == 0 && holdings_of(&in) == 0);
    end_world(&in);
}

static void a_quarantined_plugins_entries_end_only_when_the_host_ends_them_and_it_goes_on_when_opened_again(void) {
    world in;
    plugin provider;
    plugin consumer;
    settled_record settled = {0};
    watched_record watched = {0};
    weivel_c_want_callback callback = settling(&settled);
    weivel_c_service_watcher watcher = watching_services(&watched);
    weivel_quarantine_reason reason = WEIVEL_INIT(weivel_quarantine_reason);
    weivel_plugin_options options = WEIVEL_INIT(weivel_plugin_options);
    void *service = NULL;
    begin_world(&in, NULL, 1);
    name_plugin(&provider, "p");
    provides(&provider, "p:a");
    name_plugin(&consumer, "c");
    consumes(&consumer, "p:a");
    consumes(&consumer, "q:never");
    open_in(&in, &provider);
    open_in(&in, &consumer);
    reason.detail = "its activation failed";
    reason.fix = "fix its activation";
    CHECK(weivel_c_context_watch_service(consumer.context, "p:a", &watcher, NULL, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_want_service(consumer.context, "q:never", NULL, &callback, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_quarantine_plugin(in.core, consumer.handle, &reason, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(settled.count == 1 && settled.code == WEIVEL_REFUSAL_STOPPED);
    CHECK(records_of(&in) == 1);
    end_entries_of(&in, &consumer);
    CHECK(records_of(&in) == 0);
    CHECK(weivel_c_context_token(consumer.context, &options.payload) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_open_plugin(in.core, consumer.handle, &options, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_register_service(provider.context, "p:a", &first_object, NULL, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(watched.count == 0);
    CHECK(weivel_c_context_get_service(consumer.context, "p:a", &service, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(service == &first_object);
    end_world(&in);
}

static void a_call_a_released_plugin_still_makes_is_answered_as_the_core_answers_a_late_call(void) {
    world in;
    plugin provider;
    plugin consumer;
    settled_record settled = {0};
    watched_record watched = {0};
    weivel_c_want_callback callback = settling(&settled);
    weivel_c_service_watcher watcher = watching_services(&watched);
    weivel_c_refusal refusal = WEIVEL_INIT(weivel_c_refusal);
    weivel_handle handle = 77;
    void *service = NULL;
    begin_pair(&in, &provider, &consumer, 1);
    CHECK(weivel_c_context_register_service(provider.context, "p:a", &first_object, NULL, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_release_plugin(in.core, consumer.handle, NULL) == WEIVEL_C_STATUS_OK);
    end_entries_of(&in, &consumer);
    forget_every_crossing();
    CHECK(weivel_c_context_get_service(consumer.context, "p:a", &service, &refusal) == WEIVEL_C_STATUS_OK);
    CHECK(service == &first_object && holdings_of(&in) == 1);
    CHECK(weivel_c_context_want_service(consumer.context, "p:a", NULL, &callback, &handle, &refusal) ==
          WEIVEL_C_STATUS_OK);
    CHECK(settled.count == 1 && settled.service == NULL && settled.code == WEIVEL_REFUSAL_STOPPED);
    CHECK(weivel_c_context_watch_service(consumer.context, "p:a", &watcher, &handle, &refusal) ==
          WEIVEL_C_STATUS_REFUSED);
    CHECK(handle == 0 && refusal.code == WEIVEL_REFUSAL_NONE);
    CHECK(weivel_c_core_release_plugin(in.core, provider.handle, NULL) == WEIVEL_C_STATUS_OK);
    end_entries_of(&in, &provider);
    handle = 77;
    CHECK(weivel_c_context_register_service(provider.context, "p:a", &second_object, NULL, &handle, &refusal) ==
          WEIVEL_C_STATUS_REFUSED);
    CHECK(handle == 0 && refusal.code == WEIVEL_REFUSAL_NONE);
    CHECK(other_crossings >= 5 && records_of(&in) == 0);
    CHECK(weivel_c_core_forget_plugin(in.core, consumer.handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(holdings_of(&in) == 0);
    CHECK(weivel_c_context_get_service(consumer.context, "p:a", &service, &refusal) == WEIVEL_C_STATUS_REFUSED);
    CHECK(refusal.code == WEIVEL_REFUSAL_STALE_HANDLE);
    CHECK(weivel_c_refusal_release(&refusal) == WEIVEL_C_STATUS_OK);
    end_world(&in);
}

/** A provider's object: something a consumer calls through, as plain C. */
typedef struct adder {
    int (*add)(int left, int right);
} adder;

static int add_two(int left, int right) {
    return left + right;
}

static void a_resolved_service_is_called_without_crossing_into_the_core_and_no_answer_is_cached(void) {
    world in;
    plugin provider;
    plugin consumer;
    adder first = {add_two};
    adder second = {add_two};
    weivel_handle registration = 0;
    void *service = NULL;
    const adder *resolved;
    int total = 0;
    int round;
    begin_pair(&in, &provider, &consumer, 1);
    CHECK(weivel_c_context_register_service(provider.context, "p:a", &first, NULL, &registration, NULL) ==
          WEIVEL_C_STATUS_OK);
    forget_every_crossing();
    CHECK(weivel_c_context_get_service(consumer.context, "p:a", &service, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(service == &first && other_crossings == 1);
    resolved = service;
    forget_every_crossing();
    for (round = 0; round < 1000; round++) {
        total += resolved->add(round, 1);
    }
    CHECK(total == 500500 && every_crossing() == 0);
    CHECK(weivel_c_context_unregister_service(provider.context, registration, NULL) == WEIVEL_C_STATUS_OK);
    forget_every_crossing();
    CHECK(weivel_c_context_get_service(consumer.context, "p:a", &service, NULL) == WEIVEL_C_STATUS_NONE);
    CHECK(service == NULL && other_crossings == 1);
    CHECK(weivel_c_context_register_service(provider.context, "p:a", &second, NULL, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_get_service(consumer.context, "p:a", &service, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(service == &second);
    end_world(&in);
}

/** What a callback does to prove no lock of the layer's is held around it: a call that takes every one of them. */
typedef struct calling_in {
    world *in;
    plugin *consumer;
    uint32_t calls;
    uint32_t failures;
    uint32_t inside;
} calling_in;

static weivel_c_status hear_nothing(void *data, const weivel_c_service_change *change) {
    (void)data;
    (void)change;
    return WEIVEL_C_STATUS_OK;
}

static weivel_c_status hear_no_event(void *data, const weivel_c_plugin_event *event) {
    (void)data;
    (void)event;
    return WEIVEL_C_STATUS_OK;
}

static void call_into_the_context(void *more) {
    calling_in *self = more;
    weivel_c_service_watcher watcher = {hear_nothing, NULL};
    weivel_c_plugin_listener listener = {hear_no_event, NULL};
    weivel_handle watch = 0;
    uint64_t registration = 0;
    void *service = NULL;
    if (self->inside) {
        return;
    }
    self->inside = 1;
    self->calls++;
    self->failures +=
        weivel_c_context_get_service(self->consumer->context, "p:a", &service, NULL) != WEIVEL_C_STATUS_OK;
    self->failures += weivel_c_context_watch_service(self->consumer->context, "p:a", &watcher, &watch, NULL) !=
                      WEIVEL_C_STATUS_OK;
    self->failures += weivel_c_context_stop_watching(self->consumer->context, watch, NULL) != WEIVEL_C_STATUS_OK;
    self->failures +=
        weivel_c_context_add_listener(self->consumer->context, &listener, &registration) != WEIVEL_C_STATUS_OK;
    self->failures +=
        weivel_c_context_remove_listener(self->consumer->context, registration) != WEIVEL_C_STATUS_OK;
    self->failures += weivel_c_context_log(self->consumer->context, WEIVEL_LOG_DEBUG, "inside") != WEIVEL_C_STATUS_OK;
    records_of(self->in);
    self->inside = 0;
}

static void no_callback_of_the_plugins_or_the_hosts_runs_under_a_lock_of_the_layers(void) {
    world in;
    constrained_pair pair;
    plugin maker;
    calling_in calling = {0};
    answering provider = {0};
    weivel_c_capacity_answerer answerer = {answer_capacity, &provider};
    settled_record settled = {0};
    watched_record services = {0};
    watched_record capabilities = {0};
    reported_record reported = {0};
    weivel_c_want_callback callback = settling(&settled);
    weivel_c_service_watcher service_watcher = watching_services(&services);
    weivel_c_capability_watcher capability_watcher = watching_capabilities(&capabilities);
    weivel_c_want_options options = polling_every(10);
    weivel_handle registration = 0;
    void *service = NULL;
    name_constrained_pair(&pair);
    consumes(&pair.consumer, "p:b");
    provides(&pair.provider, "p:b");
    pair.consumer.capabilities_consumed[pair.consumer.capabilities_consumed_count++] = "screens";
    begin_constrained(&in, &pair);
    name_plugin(&maker, "m");
    maker.capabilities_provided[maker.capabilities_provided_count++] = "screens";
    open_in(&in, &maker);
    calling = (calling_in){&in, &pair.consumer, 0, 0, 0};
    provider.live = speed_of(9.0);
    provider.serves = WEIVEL_CHOICE_YES;
    CHECK(weivel_c_context_register_service(pair.provider.context, "p:a", &first_object, &answerer, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    provider.react = call_into_the_context;
    provider.more = &calling;
    settled.react = call_into_the_context;
    settled.more = &calling;
    services.react = call_into_the_context;
    services.more = &calling;
    capabilities.react = call_into_the_context;
    capabilities.more = &calling;
    reported.react = call_into_the_context;
    reported.more = &calling;
    in.log.react = call_into_the_context;
    in.log.more = &calling;
    listen_to(&pair.consumer, &reported);
    CHECK(weivel_c_context_want_service(pair.consumer.context, "p:a", &options, &callback, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(provider.count == 2 && settled.count == 1);
    CHECK(weivel_c_context_watch_service(pair.consumer.context, "p:b", &service_watcher, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_register_service(pair.provider.context, "p:b", &second_object, NULL, &registration,
                                            NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_unregister_service(pair.provider.context, registration, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(services.count == 2 && reported.count == 1);
    CHECK(weivel_c_context_watch_capability(pair.consumer.context, "screens", &capability_watcher, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_provide_capability(maker.context, "screens", &second_object, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_verify_activation(in.core, maker.handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(capabilities.count == 1);
    CHECK(weivel_c_context_get_service(pair.consumer.context, "p:undeclared", &service, NULL) ==
          WEIVEL_C_STATUS_REFUSED);
    CHECK(reported.count == 2);
    CHECK(weivel_c_context_log(pair.consumer.context, WEIVEL_LOG_INFO, "outside") == WEIVEL_C_STATUS_OK);
    CHECK(calling.calls == 9 && calling.failures == 0);
    CHECK(in.sink.count == 0);
    end_world(&in);
}

typedef struct stopping {
    plugin *consumer;
    weivel_handle watch;
} stopping;

/**
 * Stops a watch, whose record's step then waits behind this one, releases the plugin, ends its entries ahead of that
 * step and frees its context: the record outlives its context and is freed by its own step.
 */
static weivel_c_status stop_then_end_then_free(void *data, weivel_c_core *core) {
    stopping *self = data;
    CHECK(weivel_c_context_stop_watching(self->consumer->context, self->watch, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_release_plugin(core, self->consumer->handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_end_entries(self->consumer->context) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_free(self->consumer->context) == WEIVEL_C_STATUS_OK);
    self->consumer->context = NULL;
    return WEIVEL_C_STATUS_OK;
}

static void every_record_is_freed_on_its_ending_on_end_entries_and_on_the_contexts_free(void) {
    world in;
    plugin provider;
    plugin consumer;
    settled_record settled = {0};
    watched_record watched = {0};
    reported_record reported = {0};
    weivel_c_want_callback callback = settling(&settled);
    weivel_c_service_watcher watcher = watching_services(&watched);
    stopping stop;
    weivel_handle registration = 0;
    weivel_handle want = 0;
    uint32_t contexts_made = 0;
    begin_pair(&in, &provider, &consumer, 1);
    CHECK(weivel_c_contexts_count(in.contexts, &contexts_made, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(contexts_made == 2 && records_of(&in) == 0 && entries_of(&in) == 2);
    CHECK(weivel_c_context_want_service(consumer.context, "p:a", NULL, &callback, NULL, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_want_service(consumer.context, "p:a", NULL, &callback, &want, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_watch_service(consumer.context, "p:a", &watcher, &stop.watch, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(records_of(&in) == 3 && entries_of(&in) == 5);
    CHECK(weivel_c_context_withdraw_want(consumer.context, want, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(records_of(&in) == 2 && entries_of(&in) == 4);
    CHECK(weivel_c_context_register_service(provider.context, "p:a", &first_object, NULL, &registration, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(settled.count == 1 && records_of(&in) == 2 && entries_of(&in) == 4);
    CHECK(weivel_c_context_unregister_service(provider.context, registration, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(records_of(&in) == 1 && entries_of(&in) == 3 && holdings_of(&in) == 0);
    CHECK(weivel_c_context_want_service(consumer.context, "p:a", NULL, &callback, NULL, NULL) == WEIVEL_C_STATUS_OK);
    listen_to(&consumer, &reported);
    CHECK(weivel_c_contexts_free(in.contexts) == WEIVEL_C_STATUS_MISUSE);
    CHECK(records_of(&in) == 2);
    stop.consumer = &consumer;
    in_dispatch(&in, stop_then_end_then_free, &stop);
    CHECK(settled.count == 1 && records_of(&in) == 0 && entries_of(&in) == 1);
    CHECK(weivel_c_contexts_count(in.contexts, &contexts_made, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(contexts_made == 1);
    CHECK(weivel_c_context_register_service(provider.context, "p:a", &first_object, NULL, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_watch_service(provider.context, "p:a", &watcher, NULL, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(records_of(&in) == 2);
    CHECK(in.sink.count == 0);
    end_world(&in);
}

static void the_hosts_own_context_registers_and_is_answered_as_a_plugins_is(void) {
    world in;
    plugin provider;
    plugin consumer;
    weivel_c_context *of_the_host = NULL;
    weivel_c_context *again = NULL;
    reported_record reported = {0};
    weivel_c_plugin_listener listener = {record_plugin_event, &reported};
    weivel_handle registration = 0;
    uint64_t listening = 0;
    void *service = NULL;
    begin_world(&in, NULL, 1);
    name_plugin(&provider, "p");
    provides(&provider, "p:a");
    name_plugin(&consumer, "c");
    consumes(&consumer, "clock");
    open_in(&in, &provider);
    open_in(&in, &consumer);
    CHECK(weivel_c_context_create(in.contexts, WEIVEL_HOST, NULL, &of_the_host) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_create(in.contexts, WEIVEL_HOST, NULL, &again) == WEIVEL_C_STATUS_MISUSE);
    CHECK(again == NULL);
    CHECK(weivel_c_context_create(in.contexts, 0, NULL, &again) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_context_register_service(of_the_host, "clock", &first_object, NULL, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_get_service(consumer.context, "clock", &service, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(service == &first_object);
    CHECK(weivel_c_context_add_listener(of_the_host, &listener, &listening) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_register_service(provider.context, "p:a", &second_object, NULL, &registration, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_get_service(of_the_host, "p:a", &service, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(service == &second_object);
    CHECK(weivel_c_context_unregister_service(provider.context, registration, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(reported.count == 1 && reported.items[0].kind == WEIVEL_C_PLUGIN_EVENT_REVOKED);
    CHECK(reported.items[0].object == &second_object);
    end_world(&in);
}

static void a_misused_creation_makes_nothing(void) {
    world in;
    weivel_c_contexts_options options = WEIVEL_INIT(weivel_c_contexts_options);
    weivel_c_contexts *contexts = (weivel_c_contexts *)&first_object;
    begin_world(&in, NULL, 1);
    CHECK(weivel_c_contexts_create(NULL, &contexts) == WEIVEL_C_STATUS_MISUSE && contexts == NULL);
    options.handles = in.handles;
    CHECK(weivel_c_contexts_create(&options, &contexts) == WEIVEL_C_STATUS_MISUSE && contexts == NULL);
    options.core = in.core;
    options.handles = NULL;
    CHECK(weivel_c_contexts_create(&options, &contexts) == WEIVEL_C_STATUS_MISUSE && contexts == NULL);
    options.handles = in.handles;
    options.size -= 1;
    CHECK(weivel_c_contexts_create(&options, &contexts) == WEIVEL_C_STATUS_MISUSE && contexts == NULL);
    CHECK(weivel_c_contexts_create(&options, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_contexts_free(NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_free(NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_end_entries(NULL) == WEIVEL_C_STATUS_MISUSE);
    end_world(&in);
}

int main(int argument_count, char **arguments) {
    if (argument_count != 2 || !load_every_table(arguments[1])) {
        fprintf(stderr, "usage: context_test <core-library>\n");
        return 2;
    }
    a_get_answers_the_providers_own_pointer_and_absence_is_not_a_refusal();
    a_refusal_of_reach_answers_the_cores_refusal_with_its_code_subject_and_fix();
    a_want_settles_exactly_once_with_the_providers_pointer();
    a_want_already_satisfiable_and_one_rejected_at_once_settle_through_the_callback_too();
    a_want_waits_without_bound_unless_the_plugin_asks_for_a_timeout();
    a_timeout_a_budget_and_a_poll_interval_reach_the_core_unchanged_with_the_hosts_time();
    without_a_clock_a_want_that_names_a_time_is_the_callers_mistake_and_never_reaches_the_core();
    a_want_rejected_because_its_plugin_stopped_is_still_delivered_to_the_plugin();
    a_withdrawn_want_never_settles();
    a_watch_hears_each_registration_with_the_object_and_each_unregistration_with_nothing();
    the_ids_listed_are_those_the_plugin_may_reach_that_are_registered_now();
    a_registration_lasts_until_the_plugin_unregisters_it_or_stops();
    each_capacity_question_is_put_to_the_answerer_and_its_answer_reported();
    a_registration_that_offered_no_answerer_answers_at_once_nothing_live_and_that_it_serves();
    an_answerer_that_fails_goes_to_the_sink_and_its_question_is_left_unanswered();
    a_plugin_provides_lists_and_watches_the_implementations_of_a_capability();
    a_plugin_hears_which_object_it_holds_was_revoked();
    a_plugin_hears_a_refusal_of_reach_once();
    a_plugin_hears_a_constrained_want_that_settled_degraded();
    a_plugins_log_and_the_cores_own_go_to_the_hosts_log();
    a_plugin_reads_its_manifest_value_as_the_host_registered_it();
    a_failing_listener_goes_to_the_sink_and_does_not_silence_the_others();
    each_listener_of_a_plugin_hears_every_event_in_order_whatever_another_answers();
    a_released_plugin_hears_what_its_release_queued_and_nothing_queued_after();
    a_quarantined_plugins_entries_end_only_when_the_host_ends_them_and_it_goes_on_when_opened_again();
    a_call_a_released_plugin_still_makes_is_answered_as_the_core_answers_a_late_call();
    a_resolved_service_is_called_without_crossing_into_the_core_and_no_answer_is_cached();
    no_callback_of_the_plugins_or_the_hosts_runs_under_a_lock_of_the_layers();
    every_record_is_freed_on_its_ending_on_end_entries_and_on_the_contexts_free();
    the_hosts_own_context_registers_and_is_answered_as_a_plugins_is();
    a_misused_creation_makes_nothing();
    run_the_tests_of_the_layers_own_seams();
    CHECK(deadline_polls == 0);
    CHECK(weivel_c_core_unload(&real_table) == WEIVEL_C_STATUS_OK);
    return check_summary("context");
}
