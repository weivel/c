/**
 * @file
 * The timer, each rule of the contract's `timer` section, on a clock and a scheduler this file owns: a task runs only
 * when a test moves time over the time it was scheduled for, at that time, and never on its own.
 */

#include <stddef.h>

#include "core/fixture.h"
#include "threads.h"

#define TASKS 64
#define MOVES 16

enum { TASK_UNUSED = 0, TASK_WAITING = 1, TASK_CANCELLED = 2, TASK_RAN = 3 };

typedef struct owned_task {
    int64_t at;
    weivel_c_task_callback callback;
    void *callback_data;
    int state;
} owned_task;

/** The clock and the scheduler of one test, and what the layer did with them. */
typedef struct owned_scheduler {
    int64_t clock;
    owned_task tasks[TASKS];
    uint32_t begun;
    uint32_t clock_reads;
    uint32_t schedules;
    uint32_t cancels;
    uint32_t cancels_of_a_task_not_waiting;
    int64_t fired[MOVES];
    uint32_t fired_count;
    int refuses_to_schedule;
    /** When set, the timer `schedule` destroys before it answers, as another thread's destroy would land. */
    weivel_c_timer **destroyed_while_scheduling;
    /** When not 0, the time `schedule` moves the clock to before it runs the callback itself, at once. */
    int64_t runs_at_once_at;
} owned_scheduler;

static int64_t owned_now(void *data) {
    owned_scheduler *self = data;
    self->clock_reads++;
    return self->clock;
}

static void *owned_schedule(void *data, int64_t at, weivel_c_task_callback callback, void *callback_data) {
    owned_scheduler *self = data;
    owned_task *task;
    self->schedules++;
    if (self->refuses_to_schedule || self->begun == TASKS) {
        return NULL;
    }
    task = &self->tasks[self->begun++];
    *task = (owned_task){at, callback, callback_data, TASK_WAITING};
    if (self->destroyed_while_scheduling != NULL) {
        CHECK(weivel_c_timer_destroy(*self->destroyed_while_scheduling) == WEIVEL_C_STATUS_OK);
    }
    if (self->runs_at_once_at != 0) {
        self->clock = self->runs_at_once_at;
        task->state = TASK_RAN;
        callback(callback_data);
    }
    return task;
}

static int owned_cancel(void *data, void *token) {
    owned_scheduler *self = data;
    owned_task *task = token;
    self->cancels++;
    if (task->state != TASK_WAITING) {
        self->cancels_of_a_task_not_waiting++;
        return 0;
    }
    task->state = TASK_CANCELLED;
    return 1;
}

static owned_task *earliest_waiting(owned_scheduler *self, int64_t until) {
    owned_task *earliest = NULL;
    uint32_t index;
    for (index = 0; index < self->begun; index++) {
        owned_task *task = &self->tasks[index];
        if (task->state == TASK_WAITING && task->at <= until && (earliest == NULL || task->at < earliest->at)) {
            earliest = task;
        }
    }
    return earliest;
}

/** Runs the earliest task due by `until`, at the time it was scheduled for. Answers 0 when none is due. */
static int run_the_next_task(owned_scheduler *self, int64_t until) {
    owned_task *task = earliest_waiting(self, until);
    if (task == NULL) {
        return 0;
    }
    self->clock = task->at > self->clock ? task->at : self->clock;
    task->state = TASK_RAN;
    if (self->fired_count < MOVES) {
        self->fired[self->fired_count] = self->clock;
    }
    self->fired_count++;
    task->callback(task->callback_data);
    return 1;
}

static uint32_t waiting_tasks(const owned_scheduler *self) {
    uint32_t waiting = 0;
    uint32_t index;
    for (index = 0; index < self->begun; index++) {
        waiting += self->tasks[index].state == TASK_WAITING;
    }
    return waiting;
}

static int waits_for_one_task_at(const owned_scheduler *self, int64_t at) {
    uint32_t index;
    if (waiting_tasks(self) != 1) {
        return 0;
    }
    for (index = 0; index < self->begun; index++) {
        if (self->tasks[index].state == TASK_WAITING) {
            return self->tasks[index].at == at;
        }
    }
    return 0;
}

static weivel_c_scheduler scheduler_of(owned_scheduler *self) {
    weivel_c_scheduler scheduler = WEIVEL_INIT(weivel_c_scheduler);
    scheduler.now = owned_now;
    scheduler.schedule = owned_schedule;
    scheduler.cancel = owned_cancel;
    scheduler.data = self;
    return scheduler;
}

/** One move the timer says it made: the clock's time, and the deadline it armed, or `DISARMED`. */
typedef struct timer_move {
    int64_t at;
    int64_t deadline;
} timer_move;

#define DISARMED (-1)

/** A core with its timer on an owned scheduler, a provider of `accounts` and a consumer of it. */
typedef struct rig {
    owned_scheduler time;
    hearing listener;
    telling sink;
    weivel_c_core *core;
    weivel_c_timer *timer;
    weivel_handle provider;
    weivel_handle consumer;
    timer_move moves[MOVES];
    uint32_t move_count;
    int64_t last_deadline;
} rig;

static const char *const ACCOUNTS[] = {"accounts"};

static int64_t armed_deadline(weivel_c_timer *timer) {
    int64_t deadline = DISARMED;
    return weivel_c_timer_deadline(timer, &deadline) == WEIVEL_C_STATUS_OK ? deadline : DISARMED;
}

/** Notes a move when what the timer says it is armed for changed since the last note. */
static void note_move(rig *self) {
    int64_t deadline = armed_deadline(self->timer);
    if (deadline != self->last_deadline && self->move_count < MOVES) {
        self->moves[self->move_count++] = (timer_move){self->time.clock, deadline};
    }
    self->last_deadline = deadline;
}

static void begin_rig(rig *self, const weivel_c_scheduler *scheduler) {
    weivel_service_want consumed = want_of("accounts");
    memset(self, 0, sizeof *self);
    self->last_deadline = DISARMED;
    self->core = make_core(NULL, &self->listener, &self->sink);
    self->provider = open_plugin(self->core, "provider", ACCOUNTS, 0, NULL, 0);
    self->consumer = open_plugin(self->core, "consumer", NULL, 0, &consumed, 1);
    if (scheduler != NULL) {
        weivel_c_scheduler given = *scheduler;
        given.data = &self->time;
        CHECK(weivel_c_timer_create(self->core, &given, &self->timer) == WEIVEL_C_STATUS_OK);
    } else {
        CHECK(weivel_c_timer_create(self->core, NULL, &self->timer) == WEIVEL_C_STATUS_OK);
    }
    self->listener.count = 0;
    forget_crossings();
}

static void end_rig(rig *self) {
    CHECK(self->time.cancels_of_a_task_not_waiting == 0);
    CHECK(weivel_c_timer_destroy(self->timer) == WEIVEL_C_STATUS_OK);
    CHECK(waiting_tasks(&self->time) == 0);
    end_core(self->core);
    CHECK(weivel_c_timer_free(self->timer) == WEIVEL_C_STATUS_OK);
}

/** Wants `id` as `asker` with a timeout counted from the rig's clock, as the layer's later modules will. */
static weivel_handle want_within(rig *self, weivel_handle asker, const char *id, int64_t timeout) {
    weivel_want_options options = WEIVEL_INIT(weivel_want_options);
    weivel_handle want = 0;
    options.timed = WEIVEL_CHOICE_YES;
    options.now = self->time.clock;
    options.timeout_millis = timeout;
    CHECK(weivel_c_core_want_service(self->core, asker, id, &options, &want, NULL) == WEIVEL_C_STATUS_OK);
    note_move(self);
    return want;
}

static weivel_handle register_accounts(rig *self) {
    weivel_handle registration = 0;
    CHECK(weivel_c_core_register_service(self->core, WEIVEL_HOST, "accounts", &registration, NULL) ==
          WEIVEL_C_STATUS_OK);
    note_move(self);
    return registration;
}

/** Moves time to `until`, running each task due on the way and noting what the timer says after each. */
static void run_rig_to(rig *self, int64_t until) {
    while (run_the_next_task(&self->time, until)) {
        note_move(self);
    }
    self->time.clock = until;
    note_move(self);
}

static const heard *settlement_of(const rig *self) {
    uint32_t index;
    for (index = 0; index < self->listener.count; index++) {
        if (self->listener.items[index].kind == WEIVEL_EVENT_WANT_SETTLED) {
            return &self->listener.items[index];
        }
    }
    return NULL;
}

static int timed_out(const rig *self) {
    const heard *settled = settlement_of(self);
    return settled != NULL && settled->implementation == 0 && settled->code == WEIVEL_REFUSAL_TIMED_OUT &&
           count_heard(&self->listener, WEIVEL_EVENT_WANT_SETTLED) == 1;
}

static int moved(const rig *self, uint32_t index, int64_t at, int64_t deadline) {
    return index < self->move_count && self->moves[index].at == at && self->moves[index].deadline == deadline;
}

static void the_latest_deadline_drained_arms_the_timer_and_a_later_one_replaces_or_disarms_it(void) {
    rig tested;
    weivel_c_scheduler scheduler = scheduler_of(&tested.time);
    weivel_handle first;
    weivel_handle second;
    begin_rig(&tested, &scheduler);
    CHECK(waiting_tasks(&tested.time) == 0 && armed_deadline(tested.timer) == DISARMED);
    first = want_within(&tested, WEIVEL_HOST, "one", 1000);
    CHECK(waits_for_one_task_at(&tested.time, 1000) && armed_deadline(tested.timer) == 1000);
    CHECK(tested.time.schedules == 1 && tested.time.cancels == 0);
    second = want_within(&tested, WEIVEL_HOST, "two", 500);
    CHECK(waits_for_one_task_at(&tested.time, 500) && armed_deadline(tested.timer) == 500);
    CHECK(tested.time.schedules == 2 && tested.time.cancels == 1);
    want_within(&tested, WEIVEL_HOST, "three", 2000);
    CHECK(tested.time.schedules == 2 && tested.time.cancels == 1);
    CHECK(weivel_c_core_unregister(tested.core, second, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(waits_for_one_task_at(&tested.time, 1000) && tested.time.cancels == 2);
    CHECK(weivel_c_core_unregister(tested.core, first, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(waits_for_one_task_at(&tested.time, 2000) && tested.time.cancels == 3);
    run_rig_to(&tested, 2000);
    CHECK(waiting_tasks(&tested.time) == 0 && armed_deadline(tested.timer) == DISARMED);
    CHECK(tested.time.fired_count == 1 && tested.time.cancels == 3);
    CHECK(tested.sink.count == 0);
    end_rig(&tested);
}

/** What a listener saw of the scheduler as it heard a deadline, and as a want it made itself returned. */
typedef struct at_drain_time {
    rig *tested;
    uint32_t deadlines_heard;
    uint32_t armed_when_heard;
    int wanted;
    uint32_t armed_when_the_want_returned;
    uint32_t deadlines_heard_by_then;
} at_drain_time;

static weivel_c_status look_at_the_scheduler(hearing *self, weivel_c_core *core, const weivel_c_event *event) {
    at_drain_time *more = self->more;
    (void)core;
    if (event->kind == WEIVEL_EVENT_DEADLINE && event->data.deadline.set) {
        more->deadlines_heard++;
        more->armed_when_heard += (uint32_t)waits_for_one_task_at(&more->tested->time, event->data.deadline.at);
    } else if (event->kind == WEIVEL_EVENT_WATCH && !more->wanted) {
        more->wanted = 1;
        want_within(more->tested, WEIVEL_HOST, "inside", 300);
        more->armed_when_the_want_returned = (uint32_t)waits_for_one_task_at(&more->tested->time, 300);
        more->deadlines_heard_by_then = more->deadlines_heard;
    }
    return WEIVEL_C_STATUS_OK;
}

static void the_timer_is_armed_as_the_deadline_is_drained_and_never_as_late_as_its_dispatch(void) {
    rig tested;
    at_drain_time more = {0};
    weivel_c_scheduler scheduler = scheduler_of(&tested.time);
    weivel_handle watch = 0;
    begin_rig(&tested, &scheduler);
    more.tested = &tested;
    tested.listener.react = look_at_the_scheduler;
    tested.listener.more = &more;
    want_within(&tested, WEIVEL_HOST, "outside", 1000);
    CHECK(more.deadlines_heard == 1 && more.armed_when_heard == 1);
    CHECK(weivel_c_core_watch_service(tested.core, WEIVEL_HOST, "accounts", &watch, NULL) == WEIVEL_C_STATUS_OK);
    register_accounts(&tested);
    CHECK(more.wanted == 1 && more.armed_when_the_want_returned == 1);
    CHECK(more.deadlines_heard_by_then == 1 && more.deadlines_heard == 2 && more.armed_when_heard == 2);
    end_rig(&tested);
}

static void the_task_hands_the_core_the_time_and_what_that_queues_disarms_the_timer(void) {
    rig tested;
    weivel_c_scheduler scheduler = scheduler_of(&tested.time);
    begin_rig(&tested, &scheduler);
    want_within(&tested, tested.consumer, "accounts", 1000);
    CHECK(tested.time.clock_reads == 0);
    run_rig_to(&tested, 999);
    CHECK(tested.time.fired_count == 0 && crossed.advance == 0 && !timed_out(&tested));
    run_rig_to(&tested, 1000);
    CHECK(tested.time.fired_count == 1 && tested.time.fired[0] == 1000);
    CHECK(crossed.advance == 1 && crossed.advanced_to == 1000);
    CHECK(tested.time.clock_reads == 1);
    CHECK(timed_out(&tested));
    CHECK(waiting_tasks(&tested.time) == 0 && tested.time.cancels == 0 && armed_deadline(tested.timer) == DISARMED);
    end_rig(&tested);
}

static void a_scheduler_that_cannot_wait_the_whole_way_waits_as_long_as_it_can_and_arms_again(void) {
    rig tested;
    weivel_c_scheduler scheduler = scheduler_of(&tested.time);
    scheduler.maximum_wait = 300;
    begin_rig(&tested, &scheduler);
    want_within(&tested, tested.consumer, "accounts", 1000);
    CHECK(waits_for_one_task_at(&tested.time, 300) && armed_deadline(tested.timer) == 1000);
    run_rig_to(&tested, 300);
    CHECK(tested.time.fired_count == 1 && crossed.advance == 0 && waits_for_one_task_at(&tested.time, 600));
    run_rig_to(&tested, 650);
    CHECK(tested.time.fired_count == 2 && crossed.advance == 0 && waits_for_one_task_at(&tested.time, 900));
    run_rig_to(&tested, 900);
    CHECK(tested.time.fired_count == 3 && crossed.advance == 0 && waits_for_one_task_at(&tested.time, 1000));
    CHECK(!timed_out(&tested) && armed_deadline(tested.timer) == 1000);
    run_rig_to(&tested, 1000);
    CHECK(tested.time.fired_count == 4 && crossed.advance == 1 && crossed.advanced_to == 1000);
    CHECK(timed_out(&tested) && waiting_tasks(&tested.time) == 0);
    CHECK(tested.time.cancels == 0);
    end_rig(&tested);
}

static void the_wrapper_advances_first_when_the_deadline_is_due_and_the_task_has_not_run(void) {
    rig tested;
    weivel_c_scheduler scheduler = scheduler_of(&tested.time);
    weivel_handle found = 0;
    begin_rig(&tested, &scheduler);
    want_within(&tested, tested.consumer, "accounts", 1000);
    tested.time.clock = 1000;
    CHECK(tested.time.clock_reads == 0);
    register_accounts(&tested);
    CHECK(tested.time.clock_reads == 1);
    CHECK(crossed.advance == 1 && crossed.advanced_to == 1000);
    CHECK(timed_out(&tested));
    CHECK(tested.time.fired_count == 0 && tested.time.cancels == 1 && waiting_tasks(&tested.time) == 0);
    CHECK(weivel_c_core_get_service(tested.core, WEIVEL_HOST, "accounts", &found, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(tested.time.clock_reads == 1 && crossed.advance == 1);
    end_rig(&tested);
}

static void no_clock_is_read_while_nothing_is_armed(void) {
    rig tested;
    weivel_c_scheduler scheduler = scheduler_of(&tested.time);
    weivel_handle want;
    weivel_handle registration;
    begin_rig(&tested, &scheduler);
    registration = register_accounts(&tested);
    CHECK(weivel_c_core_unregister(tested.core, registration, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(tested.time.clock_reads == 0 && tested.time.schedules == 0);
    want = want_within(&tested, WEIVEL_HOST, "one", 1000);
    CHECK(tested.time.clock_reads == 0);
    CHECK(weivel_c_core_unregister(tested.core, want, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(tested.time.clock_reads == 1);
    register_accounts(&tested);
    CHECK(tested.time.clock_reads == 1 && crossed.advance == 0);
    end_rig(&tested);
}

static void without_a_scheduler_nothing_is_armed_and_a_call_that_needs_the_time_is_the_callers_mistake(void) {
    rig tested;
    weivel_c_deadline_hooks none = WEIVEL_INIT(weivel_c_deadline_hooks);
    int64_t now = 77;
    uint32_t crossings;
    begin_rig(&tested, NULL);
    crossings = crossed.next_event + crossed.advance + crossed.handle_state + crossed.next_deadline;
    CHECK(weivel_c_timer_now(tested.timer, &now) == WEIVEL_C_STATUS_MISUSE);
    CHECK(now == 77);
    CHECK(crossed.next_event + crossed.advance + crossed.handle_state + crossed.next_deadline == crossings);
    want_within(&tested, tested.consumer, "accounts", 1000);
    tested.time.clock = 5000;
    register_accounts(&tested);
    CHECK(armed_deadline(tested.timer) == DISARMED && tested.move_count == 0);
    CHECK(tested.time.clock_reads == 0 && tested.time.schedules == 0 && crossed.advance == 0);
    CHECK(count_heard(&tested.listener, WEIVEL_EVENT_WANT_SETTLED) == 1 && !timed_out(&tested));
    CHECK(weivel_c_core_set_deadline_hooks(tested.core, &none) == WEIVEL_C_STATUS_OK);
    end_rig(&tested);
}

static void a_timer_with_a_scheduler_answers_the_time_for_a_call_that_needs_it(void) {
    rig tested;
    weivel_c_scheduler scheduler = scheduler_of(&tested.time);
    int64_t now = 0;
    begin_rig(&tested, &scheduler);
    tested.time.clock = 123;
    CHECK(weivel_c_timer_now(tested.timer, &now) == WEIVEL_C_STATUS_OK && now == 123);
    CHECK(weivel_c_timer_now(tested.timer, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_timer_now(NULL, &now) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_timer_destroy(tested.timer) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_timer_now(tested.timer, &now) == WEIVEL_C_STATUS_DESTROYED);
    end_rig(&tested);
}

static void destroy_cancels_the_task_so_nothing_of_the_layers_fires_afterwards(void) {
    rig tested;
    weivel_c_scheduler scheduler = scheduler_of(&tested.time);
    owned_task *task;
    begin_rig(&tested, &scheduler);
    want_within(&tested, tested.consumer, "accounts", 1000);
    CHECK(waits_for_one_task_at(&tested.time, 1000));
    task = &tested.time.tasks[0];
    CHECK(weivel_c_timer_free(tested.timer) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_timer_destroy(tested.timer) == WEIVEL_C_STATUS_OK);
    CHECK(waiting_tasks(&tested.time) == 0 && tested.time.cancels == 1);
    CHECK(armed_deadline(tested.timer) == DISARMED);
    CHECK(weivel_c_timer_destroy(tested.timer) == WEIVEL_C_STATUS_OK);
    CHECK(tested.time.cancels == 1);
    run_rig_to(&tested, 5000);
    CHECK(tested.time.fired_count == 0 && crossed.advance == 0 && tested.time.clock_reads == 0);
    register_accounts(&tested);
    CHECK(tested.time.clock_reads == 0 && tested.time.schedules == 1);
    CHECK(task->state == TASK_CANCELLED);
    CHECK(weivel_c_core_destroy(tested.core) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_timer_destroy(NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_core_free(tested.core) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_timer_free(tested.timer) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_timer_free(NULL) == WEIVEL_C_STATUS_OK);
}

/** A cancel that came too late: the scheduler says so, and runs the callback all the same. */
static int cancel_too_late(void *data, void *token) {
    owned_scheduler *self = data;
    (void)token;
    self->cancels++;
    return 0;
}

static void run_a_task_the_scheduler_could_not_cancel(owned_task *task) {
    task->state = TASK_RAN;
    task->callback(task->callback_data);
}

static void a_callback_of_a_task_cancelled_too_late_is_not_taken_for_the_task_waited_for(void) {
    rig tested;
    weivel_c_scheduler scheduler = scheduler_of(&tested.time);
    weivel_handle sooner;
    scheduler.cancel = cancel_too_late;
    begin_rig(&tested, &scheduler);
    want_within(&tested, WEIVEL_HOST, "later", 1000);
    sooner = want_within(&tested, WEIVEL_HOST, "sooner", 400);
    CHECK(tested.time.cancels == 1 && tested.time.schedules == 2);
    CHECK(weivel_c_core_unregister(tested.core, sooner, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(tested.time.cancels == 2 && tested.time.schedules == 3 && armed_deadline(tested.timer) == 1000);
    tested.time.clock = 1200;
    tested.time.clock_reads = 0;
    run_a_task_the_scheduler_could_not_cancel(&tested.time.tasks[0]);
    run_a_task_the_scheduler_could_not_cancel(&tested.time.tasks[1]);
    CHECK(crossed.advance == 0 && tested.time.clock_reads == 0 && tested.time.schedules == 3);
    CHECK(armed_deadline(tested.timer) == 1000);
    run_rig_to(&tested, 1200);
    CHECK(crossed.advance == 1 && crossed.advanced_to == 1200 && tested.time.fired_count == 1);
    CHECK(armed_deadline(tested.timer) == DISARMED);
    end_rig(&tested);
}

static void a_task_that_runs_late_hands_the_core_the_time_the_clock_answers(void) {
    rig tested;
    weivel_c_scheduler scheduler = scheduler_of(&tested.time);
    begin_rig(&tested, &scheduler);
    want_within(&tested, tested.consumer, "accounts", 1000);
    tested.time.clock = 1250;
    run_rig_to(&tested, 1250);
    CHECK(tested.time.fired_count == 1 && tested.time.fired[0] == 1250);
    CHECK(crossed.advance == 1 && crossed.advanced_to == 1250 && timed_out(&tested));
    end_rig(&tested);
}

static void a_callback_that_begins_after_the_destroy_reads_no_clock_and_calls_nothing(void) {
    rig tested;
    weivel_c_scheduler scheduler = scheduler_of(&tested.time);
    owned_task *late;
    scheduler.cancel = cancel_too_late;
    begin_rig(&tested, &scheduler);
    want_within(&tested, tested.consumer, "accounts", 1000);
    late = &tested.time.tasks[0];
    tested.time.clock = 5000;
    CHECK(weivel_c_timer_destroy(tested.timer) == WEIVEL_C_STATUS_OK);
    CHECK(tested.time.cancels == 1);
    run_a_task_the_scheduler_could_not_cancel(late);
    CHECK(tested.time.clock_reads == 0 && crossed.advance == 0 && tested.time.schedules == 1);
    CHECK(!timed_out(&tested));
    end_rig(&tested);
}

static void a_destroy_that_lands_while_a_task_is_scheduled_leaves_that_task_cancelled(void) {
    rig tested;
    weivel_c_scheduler scheduler = scheduler_of(&tested.time);
    begin_rig(&tested, &scheduler);
    tested.time.destroyed_while_scheduling = &tested.timer;
    want_within(&tested, tested.consumer, "accounts", 1000);
    CHECK(tested.time.schedules == 1 && tested.time.cancels == 1);
    CHECK(tested.time.tasks[0].state == TASK_CANCELLED && waiting_tasks(&tested.time) == 0);
    CHECK(armed_deadline(tested.timer) == DISARMED);
    end_rig(&tested);
}

static void a_callback_that_begins_while_its_task_is_scheduled_and_after_a_destroy_does_nothing(void) {
    rig tested;
    weivel_c_scheduler scheduler = scheduler_of(&tested.time);
    begin_rig(&tested, &scheduler);
    tested.time.destroyed_while_scheduling = &tested.timer;
    tested.time.runs_at_once_at = 9000;
    want_within(&tested, tested.consumer, "accounts", 1000);
    CHECK(tested.time.schedules == 1 && tested.time.cancels == 0);
    CHECK(tested.time.clock_reads == 0 && crossed.advance == 0 && !timed_out(&tested));
    end_rig(&tested);
}

static void a_task_that_runs_before_schedule_answers_is_never_cancelled_afterwards(void) {
    rig tested;
    weivel_c_scheduler scheduler = scheduler_of(&tested.time);
    begin_rig(&tested, &scheduler);
    tested.time.runs_at_once_at = 1000;
    want_within(&tested, tested.consumer, "accounts", 1000);
    tested.time.runs_at_once_at = 0;
    CHECK(tested.time.schedules == 1 && crossed.advance == 1 && crossed.advanced_to == 1000);
    CHECK(timed_out(&tested) && armed_deadline(tested.timer) == DISARMED);
    register_accounts(&tested);
    CHECK(tested.time.cancels == 0);
    end_rig(&tested);
}

static void a_scheduler_that_cannot_schedule_goes_to_the_sink_and_the_wrapper_still_hands_over_the_time(void) {
    rig tested;
    weivel_c_scheduler scheduler = scheduler_of(&tested.time);
    begin_rig(&tested, &scheduler);
    tested.time.refuses_to_schedule = 1;
    want_within(&tested, tested.consumer, "accounts", 1000);
    CHECK(tested.sink.count == 1 && tested.sink.items[0].failure.kind == WEIVEL_C_FAILURE_DEADLINE_HOOK);
    CHECK(waiting_tasks(&tested.time) == 0 && armed_deadline(tested.timer) == 1000);
    tested.time.clock = 1200;
    register_accounts(&tested);
    CHECK(timed_out(&tested) && crossed.advance == 1 && crossed.advanced_to == 1200);
    CHECK(tested.time.cancels == 0);
    end_rig(&tested);
}

static void a_timer_made_while_the_core_waits_on_a_deadline_is_armed_at_once(void) {
    rig tested;
    owned_scheduler time;
    weivel_c_scheduler scheduler = scheduler_of(&time);
    weivel_c_timer *second = NULL;
    memset(&time, 0, sizeof time);
    begin_rig(&tested, NULL);
    want_within(&tested, tested.consumer, "accounts", 1000);
    CHECK(weivel_c_timer_create(tested.core, &scheduler, &second) == WEIVEL_C_STATUS_OK);
    CHECK(waits_for_one_task_at(&time, 1000) && armed_deadline(second) == 1000);
    CHECK(weivel_c_timer_destroy(second) == WEIVEL_C_STATUS_OK);
    CHECK(waiting_tasks(&time) == 0);
    CHECK(weivel_c_timer_free(second) == WEIVEL_C_STATUS_OK);
    end_rig(&tested);
}

/** A `follow` hook that is nobody's timer, and what it was told. */
typedef struct plain_follower {
    uint32_t follows;
    int64_t at;
    uint32_t set;
} plain_follower;

static weivel_c_status follow_plainly(void *data, int64_t at, uint32_t set) {
    plain_follower *self = data;
    self->follows++;
    self->at = at;
    self->set = set;
    return WEIVEL_C_STATUS_OK;
}

static void set_plain_hooks(weivel_c_core *core, plain_follower *follower) {
    weivel_c_deadline_hooks hooks = WEIVEL_INIT(weivel_c_deadline_hooks);
    hooks.follow = follow_plainly;
    hooks.data = follower;
    CHECK(weivel_c_core_set_deadline_hooks(core, &hooks) == WEIVEL_C_STATUS_OK);
}

static void a_second_timer_that_keeps_time_is_refused_and_the_first_still_follows(void) {
    rig tested;
    owned_scheduler other_time;
    weivel_c_scheduler scheduler = scheduler_of(&tested.time);
    weivel_c_scheduler other = scheduler_of(&other_time);
    weivel_c_timer *second = (weivel_c_timer *)&other_time;
    memset(&other_time, 0, sizeof other_time);
    begin_rig(&tested, &scheduler);
    CHECK(weivel_c_timer_create(tested.core, &other, &second) == WEIVEL_C_STATUS_MISUSE);
    CHECK(second == NULL);
    want_within(&tested, tested.consumer, "accounts", 1000);
    CHECK(waits_for_one_task_at(&tested.time, 1000) && armed_deadline(tested.timer) == 1000);
    CHECK(other_time.schedules == 0 && other_time.clock_reads == 0);
    end_rig(&tested);
}

static void a_destroyed_timer_takes_only_its_own_hooks_off_the_core(void) {
    rig tested;
    plain_follower follower = {0, 0, 0};
    weivel_c_scheduler scheduler = scheduler_of(&tested.time);
    begin_rig(&tested, &scheduler);
    set_plain_hooks(tested.core, &follower);
    CHECK(weivel_c_timer_destroy(tested.timer) == WEIVEL_C_STATUS_OK);
    want_within(&tested, tested.consumer, "accounts", 1000);
    CHECK(follower.follows == 1 && follower.set == 1 && follower.at == 1000);
    CHECK(tested.time.schedules == 0);
    end_rig(&tested);
}

static void a_timer_without_a_scheduler_leaves_the_hooks_of_the_core_alone(void) {
    rig tested;
    plain_follower follower = {0, 0, 0};
    weivel_c_timer *second = NULL;
    begin_rig(&tested, NULL);
    set_plain_hooks(tested.core, &follower);
    CHECK(weivel_c_timer_create(tested.core, NULL, &second) == WEIVEL_C_STATUS_OK);
    want_within(&tested, tested.consumer, "accounts", 1000);
    CHECK(follower.follows == 1 && follower.at == 1000);
    CHECK(weivel_c_timer_destroy(second) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_timer_free(second) == WEIVEL_C_STATUS_OK);
    register_accounts(&tested);
    CHECK(follower.follows == 2 && follower.set == 0);
    end_rig(&tested);
}

static void a_misused_creation_makes_no_timer(void) {
    owned_scheduler time;
    weivel_c_scheduler scheduler = scheduler_of(&time);
    weivel_c_scheduler small = scheduler_of(&time);
    weivel_c_timer *timer = (weivel_c_timer *)&time;
    weivel_c_core *core = make_core(NULL, NULL, NULL);
    int64_t deadline = 0;
    memset(&time, 0, sizeof time);
    CHECK(weivel_c_timer_create(NULL, &scheduler, &timer) == WEIVEL_C_STATUS_MISUSE);
    CHECK(timer == NULL);
    CHECK(weivel_c_timer_create(core, &scheduler, NULL) == WEIVEL_C_STATUS_MISUSE);
    scheduler.now = NULL;
    CHECK(weivel_c_timer_create(core, &scheduler, &timer) == WEIVEL_C_STATUS_MISUSE);
    scheduler = scheduler_of(&time);
    scheduler.schedule = NULL;
    CHECK(weivel_c_timer_create(core, &scheduler, &timer) == WEIVEL_C_STATUS_MISUSE);
    scheduler = scheduler_of(&time);
    scheduler.cancel = NULL;
    CHECK(weivel_c_timer_create(core, &scheduler, &timer) == WEIVEL_C_STATUS_MISUSE);
    scheduler = scheduler_of(&time);
    scheduler.maximum_wait = -1;
    CHECK(weivel_c_timer_create(core, &scheduler, &timer) == WEIVEL_C_STATUS_MISUSE);
    small.size = (uint32_t)offsetof(weivel_c_scheduler, data);
    CHECK(weivel_c_timer_create(core, &small, &timer) == WEIVEL_C_STATUS_MISUSE);
    small.size = (uint32_t)offsetof(weivel_c_scheduler, maximum_wait);
    small.maximum_wait = -1;
    CHECK(weivel_c_timer_create(core, &small, &timer) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_timer_deadline(timer, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_timer_deadline(NULL, &deadline) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_timer_destroy(timer) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_timer_free(timer) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_destroy(core) == WEIVEL_C_STATUS_OK);
    scheduler = scheduler_of(&time);
    CHECK(weivel_c_timer_create(core, &scheduler, &timer) == WEIVEL_C_STATUS_DESTROYED);
    CHECK(timer == NULL);
    CHECK(weivel_c_core_free(core) == WEIVEL_C_STATUS_OK);
    CHECK(time.clock_reads == 0 && time.schedules == 0);
}

/** `want/a-timed-want-arms-the-timer-at-its-deadline`, as far as the timer and the wrapper alone show it. */
static void a_timed_want_arms_the_timer_at_its_deadline(void) {
    rig tested;
    weivel_c_scheduler scheduler = scheduler_of(&tested.time);
    begin_rig(&tested, &scheduler);
    want_within(&tested, tested.consumer, "accounts", 1000);
    run_rig_to(&tested, 2000);
    CHECK(tested.move_count == 2 && moved(&tested, 0, 0, 1000) && moved(&tested, 1, 1000, DISARMED));
    CHECK(timed_out(&tested) && crossed.advance == 1 && crossed.advanced_to == 1000);
    CHECK(tested.time.fired_count == 1 && tested.time.fired[0] == 1000);
    end_rig(&tested);
}

static weivel_c_status check_the_refusal_of_a_time_out(hearing *self, weivel_c_core *core,
                                                       const weivel_c_event *event) {
    uint32_t *checked = self->more;
    (void)core;
    if (event->kind == WEIVEL_EVENT_WANT_SETTLED) {
        CHECK(event->data.want.refusal.code == WEIVEL_REFUSAL_TIMED_OUT);
        check_text(event->data.want.subject, "accounts", "the subject of a want that timed out");
        CHECK(event->data.want.has_fix == 1 && event->data.want.refusal.fix[0] != '\0');
        *checked += 1;
    }
    return WEIVEL_C_STATUS_OK;
}

/** `want/a-settled-want-disarms-the-timer`. */
static void a_settled_want_disarms_the_timer(void) {
    rig tested;
    weivel_c_scheduler scheduler = scheduler_of(&tested.time);
    const heard *settled;
    weivel_handle registration;
    begin_rig(&tested, &scheduler);
    want_within(&tested, tested.consumer, "accounts", 1000);
    run_rig_to(&tested, 500);
    registration = register_accounts(&tested);
    run_rig_to(&tested, 2000);
    settled = settlement_of(&tested);
    CHECK(tested.move_count == 2 && moved(&tested, 0, 0, 1000) && moved(&tested, 1, 500, DISARMED));
    CHECK(settled != NULL && settled->implementation == registration && settled->code == WEIVEL_REFUSAL_NONE);
    CHECK(count_heard(&tested.listener, WEIVEL_EVENT_WANT_SETTLED) == 1);
    CHECK(tested.time.fired_count == 0 && crossed.advance == 0 && tested.time.cancels == 1);
    end_rig(&tested);
}

/** `want/a-registration-at-the-deadline-loses`. */
static void a_registration_at_the_deadline_loses(void) {
    rig tested;
    weivel_c_scheduler scheduler = scheduler_of(&tested.time);
    uint32_t checked = 0;
    begin_rig(&tested, &scheduler);
    tested.listener.react = check_the_refusal_of_a_time_out;
    tested.listener.more = &checked;
    want_within(&tested, tested.consumer, "accounts", 1000);
    run_rig_to(&tested, 1000);
    register_accounts(&tested);
    run_rig_to(&tested, 2000);
    CHECK(timed_out(&tested) && checked == 1);
    CHECK(crossed.advance == 1 && crossed.advanced_to == 1000);
    end_rig(&tested);
}

/** `want/a-registration-past-an-unfired-deadline-loses`: the scheduler runs nothing before the registration. */
static void a_registration_past_an_unfired_deadline_loses(void) {
    rig tested;
    weivel_c_scheduler scheduler = scheduler_of(&tested.time);
    uint32_t checked = 0;
    begin_rig(&tested, &scheduler);
    tested.listener.react = check_the_refusal_of_a_time_out;
    tested.listener.more = &checked;
    want_within(&tested, tested.consumer, "accounts", 1000);
    tested.time.clock = 1500;
    register_accounts(&tested);
    run_rig_to(&tested, 2000);
    CHECK(timed_out(&tested) && checked == 1);
    CHECK(crossed.advance == 1 && crossed.advanced_to == 1500);
    CHECK(tested.move_count == 2 && moved(&tested, 0, 0, 1000) && moved(&tested, 1, 1500, DISARMED));
    CHECK(tested.time.fired_count == 0);
    end_rig(&tested);
}

#define RACING_SLOTS 256
#define RACING_ROUNDS 2000

typedef struct racing_slot {
    weivel_c_task_callback callback;
    void *callback_data;
    int waiting;
} racing_slot;

/**
 * A scheduler other threads may use at once: its own mutex guards it and is never held while a callback runs. A
 * second thread runs whatever waits, whenever it likes, so a callback races the cancel and the scheduling of others.
 */
typedef struct racing_scheduler {
    test_mutex mutex;
    racing_slot slots[RACING_SLOTS];
    int64_t clock;
    int stopping;
    uint32_t ran;
    uint32_t refused;
} racing_scheduler;

static int64_t racing_now(void *data) {
    racing_scheduler *self = data;
    int64_t now;
    lock_test_mutex(&self->mutex);
    now = self->clock;
    unlock_test_mutex(&self->mutex);
    return now;
}

static void *racing_schedule(void *data, int64_t at, weivel_c_task_callback callback, void *callback_data) {
    racing_scheduler *self = data;
    racing_slot *taken = NULL;
    int index;
    (void)at;
    lock_test_mutex(&self->mutex);
    for (index = 0; index < RACING_SLOTS && taken == NULL; index++) {
        if (!self->slots[index].waiting) {
            taken = &self->slots[index];
            *taken = (racing_slot){callback, callback_data, 1};
        }
    }
    self->refused += taken == NULL;
    unlock_test_mutex(&self->mutex);
    return taken;
}

static int racing_cancel(void *data, void *token) {
    racing_scheduler *self = data;
    racing_slot *slot = token;
    int taken_out;
    lock_test_mutex(&self->mutex);
    taken_out = slot->waiting;
    slot->waiting = 0;
    unlock_test_mutex(&self->mutex);
    return taken_out;
}

static int run_one_waiting_task(racing_scheduler *self) {
    racing_slot taken = {NULL, NULL, 0};
    int stopping;
    int index;
    lock_test_mutex(&self->mutex);
    for (index = 0; index < RACING_SLOTS && taken.callback == NULL; index++) {
        if (self->slots[index].waiting) {
            taken = self->slots[index];
            self->slots[index].waiting = 0;
            self->ran++;
        }
    }
    stopping = self->stopping;
    unlock_test_mutex(&self->mutex);
    if (taken.callback != NULL) {
        taken.callback(taken.callback_data);
    }
    return !stopping;
}

static void run_waiting_tasks(void *argument) {
    while (run_one_waiting_task(argument)) {
        let_other_threads_run();
    }
}

static uint32_t racing_waiting(racing_scheduler *self) {
    uint32_t waiting = 0;
    int index;
    lock_test_mutex(&self->mutex);
    for (index = 0; index < RACING_SLOTS; index++) {
        waiting += (uint32_t)self->slots[index].waiting;
    }
    unlock_test_mutex(&self->mutex);
    return waiting;
}

typedef struct deadline_churn {
    weivel_c_core *core;
    char id[16];
    uint32_t unexpected;
    /** 1 when the clock stands past every deadline, so a want may have timed out before it is withdrawn. */
    int wants_may_time_out;
} deadline_churn;

static void churn_the_deadline(void *argument) {
    deadline_churn *self = argument;
    uint32_t round;
    for (round = 0; round < RACING_ROUNDS; round++) {
        weivel_want_options options = WEIVEL_INIT(weivel_want_options);
        weivel_handle want = 0;
        weivel_c_status status;
        options.timed = WEIVEL_CHOICE_YES;
        options.timeout_millis = 1000 + round;
        status = weivel_c_core_want_service(self->core, WEIVEL_HOST, self->id, &options, &want, NULL);
        if (status == WEIVEL_C_STATUS_OK) {
            status = weivel_c_core_unregister(self->core, want, NULL);
        }
        self->unexpected += status != WEIVEL_C_STATUS_OK && status != WEIVEL_C_STATUS_DESTROYED &&
                            !(self->wants_may_time_out && status == WEIVEL_C_STATUS_REFUSED);
    }
}

static void a_callback_racing_a_re_arm_never_advances_before_the_deadline(void) {
    static racing_scheduler time;
    weivel_c_scheduler scheduler = WEIVEL_INIT(weivel_c_scheduler);
    deadline_churn churns[2];
    test_thread threads[2];
    test_thread runner;
    weivel_c_timer *timer = NULL;
    weivel_c_core *core = make_core(NULL, NULL, NULL);
    int index;
    memset(&time, 0, sizeof time);
    start_test_mutex(&time.mutex);
    scheduler.now = racing_now;
    scheduler.schedule = racing_schedule;
    scheduler.cancel = racing_cancel;
    scheduler.data = &time;
    CHECK(weivel_c_timer_create(core, &scheduler, &timer) == WEIVEL_C_STATUS_OK);
    forget_crossings();
    CHECK(start_test_thread(&runner, run_waiting_tasks, &time));
    for (index = 0; index < 2; index++) {
        churns[index] = (deadline_churn){core, "", 0, 0};
        snprintf(churns[index].id, sizeof churns[index].id, "r%d", index);
        CHECK(start_test_thread(&threads[index], churn_the_deadline, &churns[index]));
    }
    for (index = 0; index < 2; index++) {
        join_test_thread(&threads[index]);
        CHECK(churns[index].unexpected == 0);
    }
    lock_test_mutex(&time.mutex);
    time.stopping = 1;
    unlock_test_mutex(&time.mutex);
    join_test_thread(&runner);
    CHECK(crossed.advance == 0);
    CHECK(time.refused == 0);
    CHECK(racing_waiting(&time) == 0);
    CHECK(weivel_c_timer_destroy(timer) == WEIVEL_C_STATUS_OK);
    end_core(core);
    CHECK(weivel_c_timer_free(timer) == WEIVEL_C_STATUS_OK);
}

static void a_destroy_racing_a_callback_and_a_re_arm_leaves_nothing_scheduled(void) {
    static racing_scheduler time;
    weivel_c_scheduler scheduler = WEIVEL_INIT(weivel_c_scheduler);
    deadline_churn churn;
    test_thread thread;
    test_thread runner;
    weivel_c_timer *timer = NULL;
    weivel_c_core *core = make_core(NULL, NULL, NULL);
    uint32_t condition = 0;
    int index;
    memset(&time, 0, sizeof time);
    start_test_mutex(&time.mutex);
    time.clock = 1000000;
    scheduler.now = racing_now;
    scheduler.schedule = racing_schedule;
    scheduler.cancel = racing_cancel;
    scheduler.data = &time;
    CHECK(weivel_c_timer_create(core, &scheduler, &timer) == WEIVEL_C_STATUS_OK);
    churn = (deadline_churn){core, "d", 0, 1};
    CHECK(start_test_thread(&runner, run_waiting_tasks, &time));
    CHECK(start_test_thread(&thread, churn_the_deadline, &churn));
    for (index = 0; index < 50; index++) {
        weivel_c_core_handle_state(core, 1, &condition, NULL);
        let_other_threads_run();
    }
    CHECK(weivel_c_timer_destroy(timer) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_destroy(core) == WEIVEL_C_STATUS_OK);
    join_test_thread(&thread);
    CHECK(churn.unexpected == 0);
    CHECK(racing_waiting(&time) == 0);
    lock_test_mutex(&time.mutex);
    time.stopping = 1;
    unlock_test_mutex(&time.mutex);
    join_test_thread(&runner);
    CHECK(time.refused == 0);
    CHECK(weivel_c_core_free(core) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_timer_free(timer) == WEIVEL_C_STATUS_OK);
}

int main(int argument_count, char **arguments) {
    if (argument_count != 2 || !load_tables(arguments[1])) {
        fprintf(stderr, "usage: timer_test <core-library>\n");
        return 2;
    }
    the_latest_deadline_drained_arms_the_timer_and_a_later_one_replaces_or_disarms_it();
    the_timer_is_armed_as_the_deadline_is_drained_and_never_as_late_as_its_dispatch();
    the_task_hands_the_core_the_time_and_what_that_queues_disarms_the_timer();
    a_scheduler_that_cannot_wait_the_whole_way_waits_as_long_as_it_can_and_arms_again();
    the_wrapper_advances_first_when_the_deadline_is_due_and_the_task_has_not_run();
    no_clock_is_read_while_nothing_is_armed();
    without_a_scheduler_nothing_is_armed_and_a_call_that_needs_the_time_is_the_callers_mistake();
    a_timer_with_a_scheduler_answers_the_time_for_a_call_that_needs_it();
    destroy_cancels_the_task_so_nothing_of_the_layers_fires_afterwards();
    a_callback_of_a_task_cancelled_too_late_is_not_taken_for_the_task_waited_for();
    a_task_that_runs_late_hands_the_core_the_time_the_clock_answers();
    a_callback_that_begins_after_the_destroy_reads_no_clock_and_calls_nothing();
    a_destroy_that_lands_while_a_task_is_scheduled_leaves_that_task_cancelled();
    a_callback_that_begins_while_its_task_is_scheduled_and_after_a_destroy_does_nothing();
    a_task_that_runs_before_schedule_answers_is_never_cancelled_afterwards();
    a_scheduler_that_cannot_schedule_goes_to_the_sink_and_the_wrapper_still_hands_over_the_time();
    a_timer_made_while_the_core_waits_on_a_deadline_is_armed_at_once();
    a_second_timer_that_keeps_time_is_refused_and_the_first_still_follows();
    a_destroyed_timer_takes_only_its_own_hooks_off_the_core();
    a_timer_without_a_scheduler_leaves_the_hooks_of_the_core_alone();
    a_misused_creation_makes_no_timer();
    a_timed_want_arms_the_timer_at_its_deadline();
    a_settled_want_disarms_the_timer();
    a_registration_at_the_deadline_loses();
    a_registration_past_an_unfired_deadline_loses();
    a_callback_racing_a_re_arm_never_advances_before_the_deadline();
    a_destroy_racing_a_callback_and_a_re_arm_leaves_nothing_scheduled();
    CHECK(deadline_polls == 0);
    CHECK(weivel_c_core_unload(&real_table) == WEIVEL_C_STATUS_OK);
    return check_summary("timer");
}
