/**
 * @file
 * Calls from several threads on one core: every event is dispatched exactly once, in the order it was drained, by one
 * thread at a time, every deferred step runs exactly once, and a refused call is answered its own refusal, which it
 * can only be while no other thread's call lands between a call and its drain.
 *
 * @implNote The record the listener and the steps write is deliberately unguarded. One dispatcher at a time, handed
 * on under the layer lock, is what makes that safe, so a second dispatcher is a data race the thread sanitizer names.
 */

#include "fixture.h"
#include "threads.h"

#define THREADS 4
#define ROUNDS 400

typedef struct dispatch_record {
    uint64_t expected_sequence;
    uint32_t heard;
    uint32_t out_of_order;
    uint32_t inside;
    uint32_t overlaps;
    uint32_t steps;
} dispatch_record;

typedef struct worker {
    weivel_c_core *core;
    char id[16];
    weivel_c_step steps[ROUNDS];
    uint32_t ran[ROUNDS];
    uint32_t failures;
} worker;

static weivel_c_status hear_in_order(void *data, weivel_c_core *core, const weivel_c_event *event) {
    dispatch_record *heard = data;
    (void)core;
    heard->overlaps += heard->inside;
    heard->inside = 1;
    if (heard->expected_sequence != 0 && event->sequence != heard->expected_sequence) {
        heard->out_of_order++;
    }
    heard->expected_sequence = event->sequence + 1;
    heard->heard++;
    heard->inside = 0;
    return WEIVEL_C_STATUS_OK;
}

/** The one record every listener call and every step writes, unguarded. */
static dispatch_record record;

static weivel_c_status mark_run(void *data, weivel_c_core *core) {
    uint32_t *ran = data;
    (void)core;
    record.overlaps += record.inside;
    record.inside = 1;
    *ran += 1;
    record.steps++;
    record.inside = 0;
    return WEIVEL_C_STATUS_OK;
}

static int is_refused_as_stale(weivel_c_core *core, weivel_handle registration) {
    weivel_c_refusal refusal = WEIVEL_INIT(weivel_c_refusal);
    weivel_c_status status = weivel_c_core_unregister(core, registration, &refusal);
    int stale = status == WEIVEL_C_STATUS_REFUSED && refusal.code == WEIVEL_REFUSAL_STALE_HANDLE;
    weivel_c_refusal_release(&refusal);
    return stale;
}

static void work(worker *self) {
    uint32_t round;
    for (round = 0; round < ROUNDS; round++) {
        weivel_handle registration = 0;
        self->steps[round] = (weivel_c_step)WEIVEL_INIT(weivel_c_step);
        self->steps[round].run = mark_run;
        self->steps[round].data = &self->ran[round];
        self->failures +=
            weivel_c_core_register_service(self->core, WEIVEL_HOST, self->id, &registration, NULL) !=
            WEIVEL_C_STATUS_OK;
        self->failures += weivel_c_core_defer(self->core, &self->steps[round], NULL) != WEIVEL_C_STATUS_OK;
        self->failures += weivel_c_core_unregister(self->core, registration, NULL) != WEIVEL_C_STATUS_OK;
        self->failures += !is_refused_as_stale(self->core, registration);
    }
}

static void run_worker(void *argument) {
    work(argument);
}

static void run_all(worker *workers) {
    test_thread threads[THREADS - 1];
    int index;
    for (index = 1; index < THREADS; index++) {
        CHECK(start_test_thread(&threads[index - 1], run_worker, &workers[index]));
    }
    work(&workers[0]);
    for (index = 0; index < THREADS - 1; index++) {
        join_test_thread(&threads[index]);
    }
}

static void watch_every_workers_service(weivel_c_core *core, worker *workers) {
    int index;
    for (index = 0; index < THREADS; index++) {
        weivel_handle watch = 0;
        workers[index].core = core;
        snprintf(workers[index].id, sizeof workers[index].id, "t%d", index);
        CHECK(weivel_c_core_watch_service(core, WEIVEL_HOST, workers[index].id, &watch, NULL) ==
              WEIVEL_C_STATUS_OK);
    }
}

static void check_every_step_ran_once(const worker *workers) {
    uint32_t wrong = 0;
    uint32_t failures = 0;
    int index;
    uint32_t round;
    for (index = 0; index < THREADS; index++) {
        failures += workers[index].failures;
        for (round = 0; round < ROUNDS; round++) {
            wrong += workers[index].ran[round] != 1;
        }
    }
    CHECK(failures == 0);
    CHECK(wrong == 0);
}

static void calls_from_several_threads_are_dispatched_once_in_drained_order_by_one_thread(void) {
    weivel_c_listener listener = {hear_in_order, &record};
    uint64_t registration = 0;
    worker *workers = calloc(THREADS, sizeof *workers);
    weivel_c_core *core = make_core(NULL, NULL, NULL);
    watch_every_workers_service(core, workers);
    CHECK(weivel_c_core_add_listener(core, &listener, &registration) == WEIVEL_C_STATUS_OK);
    forget_crossings();
    run_all(workers);
    check_every_step_ran_once(workers);
    CHECK(crossed.events >= (uint32_t)(THREADS * ROUNDS * 3));
    CHECK(record.heard == crossed.events);
    CHECK(record.out_of_order == 0);
    CHECK(record.overlaps == 0);
    CHECK(record.steps == (uint32_t)(THREADS * ROUNDS));
    CHECK(crossed.unreleased == 0 && crossed.most_unreleased == 1);
    end_core(core);
    free(workers);
}

/**
 * What the `follow` hook was told, written unguarded: one thread in `follow` at a time, handed on under the layer
 * lock, is what makes that safe.
 */
typedef struct followed {
    uint32_t inside;
    uint32_t overlaps;
    uint32_t calls;
    int64_t last_at;
    uint32_t last_set;
} followed;

static weivel_c_status follow_and_linger(void *data, int64_t at, uint32_t set) {
    followed *self = data;
    self->overlaps += self->inside;
    self->inside = 1;
    self->calls++;
    self->last_at = at;
    self->last_set = set;
    let_other_threads_run();
    self->inside = 0;
    return WEIVEL_C_STATUS_OK;
}

typedef struct deadline_mover {
    weivel_c_core *core;
    char id[16];
    int64_t timeout;
    uint32_t failures;
} deadline_mover;

static void move_the_deadline(void *argument) {
    deadline_mover *self = argument;
    uint32_t round;
    for (round = 0; round < ROUNDS; round++) {
        weivel_want_options options = WEIVEL_INIT(weivel_want_options);
        weivel_handle want = 0;
        options.timed = WEIVEL_CHOICE_YES;
        options.timeout_millis = self->timeout + round;
        self->failures +=
            weivel_c_core_want_service(self->core, WEIVEL_HOST, self->id, &options, &want, NULL) != WEIVEL_C_STATUS_OK;
        self->failures += weivel_c_core_unregister(self->core, want, NULL) != WEIVEL_C_STATUS_OK;
    }
}

static void the_latest_deadline_is_followed_last_and_by_one_thread_at_a_time(void) {
    static followed record_of_following;
    weivel_c_deadline_hooks hooks = WEIVEL_INIT(weivel_c_deadline_hooks);
    deadline_mover movers[THREADS];
    test_thread threads[THREADS];
    weivel_c_core *core = make_core(NULL, NULL, NULL);
    weivel_handle want = 0;
    weivel_want_options options = WEIVEL_INIT(weivel_want_options);
    uint32_t failures = 0;
    int index;
    hooks.follow = follow_and_linger;
    hooks.data = &record_of_following;
    CHECK(weivel_c_core_set_deadline_hooks(core, &hooks) == WEIVEL_C_STATUS_OK);
    for (index = 0; index < THREADS; index++) {
        movers[index].core = core;
        movers[index].timeout = 1000 * (index + 1);
        movers[index].failures = 0;
        snprintf(movers[index].id, sizeof movers[index].id, "m%d", index);
        CHECK(start_test_thread(&threads[index], move_the_deadline, &movers[index]));
    }
    for (index = 0; index < THREADS; index++) {
        join_test_thread(&threads[index]);
        failures += movers[index].failures;
    }
    CHECK(failures == 0);
    CHECK(record_of_following.overlaps == 0);
    CHECK(record_of_following.calls >= 2);
    CHECK(record_of_following.last_set == 0);
    options.timed = WEIVEL_CHOICE_YES;
    options.timeout_millis = 77;
    CHECK(weivel_c_core_want_service(core, WEIVEL_HOST, "last", &options, &want, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(record_of_following.last_set == 1 && record_of_following.last_at == 77);
    end_core(core);
}

/**
 * A `follow` hook that waits at a gate its test opens, and what it was told, guarded by a mutex of its own so the
 * test may look while a call sits inside it.
 */
typedef struct gated_follow {
    test_mutex mutex;
    int open;
    followed told;
    weivel_c_core *core;
    weivel_c_status answered;
    /** Written by the dispatching listener, read once every thread is joined. */
    uint32_t heard_unfollowed;
} gated_follow;

static followed told_through(gated_follow *self) {
    followed told;
    lock_test_mutex(&self->mutex);
    told = self->told;
    unlock_test_mutex(&self->mutex);
    return told;
}

static int gate_is_open(gated_follow *self) {
    int open;
    lock_test_mutex(&self->mutex);
    open = self->open;
    unlock_test_mutex(&self->mutex);
    return open;
}

static void open_the_gate(gated_follow *self) {
    lock_test_mutex(&self->mutex);
    self->open = 1;
    unlock_test_mutex(&self->mutex);
}

static weivel_c_status follow_through_the_gate(void *data, int64_t at, uint32_t set) {
    gated_follow *self = data;
    lock_test_mutex(&self->mutex);
    self->told.calls++;
    self->told.inside = 1;
    self->told.last_at = at;
    self->told.last_set = set;
    unlock_test_mutex(&self->mutex);
    while (!gate_is_open(self)) {
        let_other_threads_run();
    }
    lock_test_mutex(&self->mutex);
    self->told.inside = 0;
    unlock_test_mutex(&self->mutex);
    return WEIVEL_C_STATUS_OK;
}

static void wait_until_follow_is_inside(gated_follow *self) {
    while (!told_through(self).inside) {
        let_other_threads_run();
    }
}

static void begin_gate(gated_follow *self, weivel_c_core *core) {
    weivel_c_deadline_hooks hooks = WEIVEL_INIT(weivel_c_deadline_hooks);
    memset(self, 0, sizeof *self);
    start_test_mutex(&self->mutex);
    hooks.follow = follow_through_the_gate;
    hooks.data = self;
    CHECK(weivel_c_core_set_deadline_hooks(core, &hooks) == WEIVEL_C_STATUS_OK);
}

typedef struct timed_wanter {
    weivel_c_core *core;
    const char *id;
    int64_t timeout;
    weivel_c_status answered;
} timed_wanter;

static void want_within_a_timeout(void *argument) {
    timed_wanter *self = argument;
    weivel_want_options options = WEIVEL_INIT(weivel_want_options);
    weivel_handle want = 0;
    options.timed = WEIVEL_CHOICE_YES;
    options.timeout_millis = self->timeout;
    self->answered = weivel_c_core_want_service(self->core, WEIVEL_HOST, self->id, &options, &want, NULL);
}

/**
 * @implNote The newer want is made on the test's own thread and has returned before the gate opens, and that thread
 * calls nothing more until the other is joined: so the second `follow` can only be the first thread's.
 */
static void a_deadline_drained_while_another_thread_follows_is_followed_by_that_thread(void) {
    gated_follow gate;
    test_thread thread;
    followed told;
    weivel_c_core *core = make_core(NULL, NULL, NULL);
    timed_wanter first = {core, "first", 1000, WEIVEL_C_STATUS_FAILED};
    timed_wanter newer = {core, "newer", 500, WEIVEL_C_STATUS_FAILED};
    begin_gate(&gate, core);
    CHECK(start_test_thread(&thread, want_within_a_timeout, &first));
    wait_until_follow_is_inside(&gate);
    want_within_a_timeout(&newer);
    told = told_through(&gate);
    CHECK(newer.answered == WEIVEL_C_STATUS_OK);
    CHECK(told.calls == 1 && told.inside == 1 && told.last_at == 1000);
    open_the_gate(&gate);
    join_test_thread(&thread);
    told = told_through(&gate);
    CHECK(first.answered == WEIVEL_C_STATUS_OK);
    CHECK(told.calls == 2 && told.inside == 0);
    CHECK(told.last_set == 1 && told.last_at == 500);
    end_core(core);
}

static void set_the_gated_hooks(void *argument) {
    gated_follow *self = argument;
    weivel_c_deadline_hooks hooks = WEIVEL_INIT(weivel_c_deadline_hooks);
    hooks.follow = follow_through_the_gate;
    hooks.data = self;
    self->answered = weivel_c_core_set_deadline_hooks(self->core, &hooks);
}

/**
 * A thread told the recorded deadline again, because it gave new hooks, sits in `follow` having drained nothing, so
 * no event waits on it: another thread's call is dispatched by that thread at once.
 */
static void a_thread_sitting_in_a_follow_that_drained_nothing_keeps_no_event_waiting(void) {
    static hearing listener;
    gated_follow gate;
    test_thread thread;
    weivel_handle handle = 0;
    weivel_c_core *core = make_core(NULL, &listener, NULL);
    timed_wanter first = {core, "first", 1000, WEIVEL_C_STATUS_FAILED};
    CHECK(weivel_c_core_watch_service(core, WEIVEL_HOST, "s", &handle, NULL) == WEIVEL_C_STATUS_OK);
    want_within_a_timeout(&first);
    memset(&gate, 0, sizeof gate);
    start_test_mutex(&gate.mutex);
    gate.core = core;
    listener.count = 0;
    forget_crossings();
    CHECK(start_test_thread(&thread, set_the_gated_hooks, &gate));
    wait_until_follow_is_inside(&gate);
    CHECK(weivel_c_core_register_service(core, WEIVEL_HOST, "s", &handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(told_through(&gate).inside == 1);
    CHECK(crossed.events >= 1 && listener.count == crossed.events);
    CHECK(count_heard(&listener, WEIVEL_EVENT_WATCH) == 1);
    open_the_gate(&gate);
    join_test_thread(&thread);
    CHECK(gate.answered == WEIVEL_C_STATUS_OK && listener.count == crossed.events);
    end_core(core);
}

/** Counts each deadline announcement heard before `follow` had returned with it. */
static weivel_c_status check_the_deadline_was_followed(hearing *self, weivel_c_core *core, const weivel_c_event *event) {
    gated_follow *gate = self->more;
    followed told = told_through(gate);
    (void)core;
    if (event->kind == WEIVEL_EVENT_DEADLINE &&
        (told.inside || told.last_at != event->data.deadline.at || told.last_set != event->data.deadline.set)) {
        gate->heard_unfollowed++;
    }
    return WEIVEL_C_STATUS_OK;
}

/**
 * A thread sits in `follow` for the deadline its own call drained while another thread calls and would dispatch: no
 * event of the first call, nor any queued behind them, is dispatched before `follow` has returned, so a listener
 * reading the timer never finds it armed for less than the announcement it hears.
 */
static void a_calls_events_wait_until_follow_has_returned_with_its_deadline(void) {
    static hearing listener;
    gated_follow gate;
    test_thread thread;
    weivel_handle handle = 0;
    weivel_c_core *core = make_core(NULL, &listener, NULL);
    timed_wanter first = {core, "first", 1000, WEIVEL_C_STATUS_FAILED};
    CHECK(weivel_c_core_watch_service(core, WEIVEL_HOST, "s", &handle, NULL) == WEIVEL_C_STATUS_OK);
    begin_gate(&gate, core);
    listener.count = 0;
    listener.react = check_the_deadline_was_followed;
    listener.more = &gate;
    forget_crossings();
    CHECK(start_test_thread(&thread, want_within_a_timeout, &first));
    wait_until_follow_is_inside(&gate);
    CHECK(weivel_c_core_register_service(core, WEIVEL_HOST, "s", &handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(told_through(&gate).inside == 1 && listener.count == 0);
    open_the_gate(&gate);
    join_test_thread(&thread);
    CHECK(first.answered == WEIVEL_C_STATUS_OK && listener.count == crossed.events);
    CHECK(listener.items[0].kind == WEIVEL_EVENT_DEADLINE && count_heard(&listener, WEIVEL_EVENT_WATCH) == 1);
    CHECK(gate.heard_unfollowed == 0);
    listener.react = NULL;
    end_core(core);
}

typedef struct caller_until_destroyed {
    weivel_c_core *core;
    char id[16];
    uint32_t rounds;
    uint32_t unexpected;
} caller_until_destroyed;

static void call_until_destroyed(void *argument) {
    caller_until_destroyed *self = argument;
    weivel_c_status status = WEIVEL_C_STATUS_OK;
    while (status != WEIVEL_C_STATUS_DESTROYED) {
        weivel_handle registration = 0;
        status = weivel_c_core_register_service(self->core, WEIVEL_HOST, self->id, &registration, NULL);
        if (status == WEIVEL_C_STATUS_OK) {
            status = weivel_c_core_unregister(self->core, registration, NULL);
        }
        self->unexpected += status != WEIVEL_C_STATUS_OK && status != WEIVEL_C_STATUS_DESTROYED;
        self->rounds++;
    }
}

static weivel_c_status hear_and_count(void *data, weivel_c_core *core, const weivel_c_event *event) {
    uint32_t *heard = data;
    (void)core;
    (void)event;
    *heard += 1;
    return WEIVEL_C_STATUS_OK;
}

static void a_destroy_from_another_thread_ends_the_calls_of_the_rest(void) {
    static uint32_t heard_by_the_dispatcher;
    weivel_c_listener listener = {hear_and_count, &heard_by_the_dispatcher};
    caller_until_destroyed callers[THREADS];
    test_thread threads[THREADS];
    uint64_t registration = 0;
    uint32_t condition = 0;
    weivel_c_core *core = make_core(NULL, NULL, NULL);
    int index;
    CHECK(weivel_c_core_add_listener(core, &listener, &registration) == WEIVEL_C_STATUS_OK);
    for (index = 0; index < THREADS; index++) {
        weivel_handle watch = 0;
        callers[index] = (caller_until_destroyed){core, "", 0, 0};
        snprintf(callers[index].id, sizeof callers[index].id, "d%d", index);
        CHECK(weivel_c_core_watch_service(core, WEIVEL_HOST, callers[index].id, &watch, NULL) ==
              WEIVEL_C_STATUS_OK);
    }
    for (index = 0; index < THREADS; index++) {
        CHECK(start_test_thread(&threads[index], call_until_destroyed, &callers[index]));
    }
    for (index = 0; index < 200; index++) {
        CHECK(weivel_c_core_handle_state(core, 1, &condition, NULL) != WEIVEL_C_STATUS_DESTROYED);
    }
    CHECK(weivel_c_core_destroy(core) == WEIVEL_C_STATUS_OK);
    for (index = 0; index < THREADS; index++) {
        join_test_thread(&threads[index]);
        CHECK(callers[index].unexpected == 0 && callers[index].rounds >= 1);
    }
    CHECK(weivel_c_core_free(core) == WEIVEL_C_STATUS_OK);
}

typedef struct listener_churn {
    weivel_c_core *core;
    uint32_t heard;
    uint32_t failures;
} listener_churn;

static weivel_c_status hear_for_the_churn(void *data, weivel_c_core *core, const weivel_c_event *event) {
    (void)data;
    (void)core;
    (void)event;
    return WEIVEL_C_STATUS_OK;
}

static void add_and_remove_listeners(void *argument) {
    listener_churn *self = argument;
    weivel_c_listener listener = {hear_for_the_churn, self};
    uint32_t round;
    for (round = 0; round < ROUNDS; round++) {
        uint64_t registration = 0;
        self->failures += weivel_c_core_add_listener(self->core, &listener, &registration) != WEIVEL_C_STATUS_OK;
        let_other_threads_run();
        self->failures += weivel_c_core_remove_listener(self->core, registration) != WEIVEL_C_STATUS_OK;
    }
}

static void listeners_come_and_go_on_another_thread_while_events_are_dispatched(void) {
    static uint32_t heard_throughout;
    weivel_c_listener constant = {hear_and_count, &heard_throughout};
    listener_churn churn = {NULL, 0, 0};
    test_thread thread;
    uint64_t registration = 0;
    weivel_handle watch = 0;
    uint32_t round;
    weivel_c_core *core = make_core(NULL, NULL, NULL);
    churn.core = core;
    CHECK(weivel_c_core_add_listener(core, &constant, &registration) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_watch_service(core, WEIVEL_HOST, "c", &watch, NULL) == WEIVEL_C_STATUS_OK);
    forget_crossings();
    CHECK(start_test_thread(&thread, add_and_remove_listeners, &churn));
    for (round = 0; round < ROUNDS; round++) {
        weivel_handle service = 0;
        CHECK(weivel_c_core_register_service(core, WEIVEL_HOST, "c", &service, NULL) == WEIVEL_C_STATUS_OK);
        CHECK(weivel_c_core_unregister(core, service, NULL) == WEIVEL_C_STATUS_OK);
    }
    join_test_thread(&thread);
    CHECK(churn.failures == 0);
    CHECK(heard_throughout == crossed.events);
    end_core(core);
}

int main(int argument_count, char **arguments) {
    if (argument_count != 2 || !load_tables(arguments[1])) {
        fprintf(stderr, "usage: threads_test <core-library>\n");
        return 2;
    }
    calls_from_several_threads_are_dispatched_once_in_drained_order_by_one_thread();
    the_latest_deadline_is_followed_last_and_by_one_thread_at_a_time();
    a_deadline_drained_while_another_thread_follows_is_followed_by_that_thread();
    a_thread_sitting_in_a_follow_that_drained_nothing_keeps_no_event_waiting();
    a_calls_events_wait_until_follow_has_returned_with_its_deadline();
    a_destroy_from_another_thread_ends_the_calls_of_the_rest();
    listeners_come_and_go_on_another_thread_while_events_are_dispatched();
    CHECK(weivel_c_core_unload(&real_table) == WEIVEL_C_STATUS_OK);
    return check_summary("core threads");
}
