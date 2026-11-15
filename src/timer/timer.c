#include <weivel/c/timer.h>

#include <stdlib.h>
#include <string.h>

#include "../core/mutex.h"
#include "../core/wrapper.h"

#define END_OF(type, field) (offsetof(type, field) + sizeof(((type *)0)->field))
#define FIRST_SCHEDULER_SIZE END_OF(weivel_c_scheduler, data)

/**
 * What one scheduled task is called back with, so a callback knows whether it is the task the timer still waits for.
 *
 * @implNote A ticket goes back to the timer's spare ones only when nothing can still call it: its callback has
 * begun, or its cancel answered that the callback never will. So a ticket in use is never handed to a second task,
 * and a callback of a cancelled task can never be taken for the current one. Every ticket lives until the timer is
 * freed.
 */
typedef struct ticket {
    weivel_c_timer *timer;
    struct ticket *next_spare;
    struct ticket *next_made;
} ticket;

/**
 * @implNote `lock` guards every field below the scheduler and is a leaf: it is never held while the scheduler or the
 * core is called. Only one thread at a time is in `follow`, which the wrapper sees to, so only a callback and a
 * destroy ever meet it here. `waited_for` is the ticket of the one task the timer waits for, NULL once that task's
 * callback began; `task` is its token once `schedule` has answered it. `told_armed` and `told_at` are the last move
 * the observer was told, so arming again for the same deadline tells it nothing.
 */
struct weivel_c_timer {
    weivel_c_mutex lock;
    weivel_c_core *core;
    weivel_c_scheduler scheduler;
    weivel_c_timer_observer observer;
    uint32_t told_armed;
    int64_t told_at;
    uint32_t keeps_time;
    uint32_t destroyed;
    uint32_t armed;
    int64_t armed_at;
    ticket *waited_for;
    void *task;
    uint32_t has_task;
    ticket *spare;
    ticket *made;
};

/** What `follow` takes out of the timer to cancel, outside the lock. */
typedef struct cancelled_task {
    void *task;
    ticket *called_with;
} cancelled_task;

static int64_t read_the_clock(void *data) {
    weivel_c_timer *timer = data;
    return timer->scheduler.now(timer->scheduler.data);
}

static void spare_ticket(weivel_c_timer *timer, ticket *spared) {
    spared->next_spare = timer->spare;
    timer->spare = spared;
}

/** Under the lock: a ticket nothing can still call, made when none is spare. NULL when memory ran out. */
static ticket *take_ticket(weivel_c_timer *timer) {
    ticket *taken = timer->spare;
    if (taken != NULL) {
        timer->spare = taken->next_spare;
        return taken;
    }
    taken = malloc(sizeof *taken);
    if (taken != NULL) {
        taken->timer = timer;
        taken->next_spare = NULL;
        taken->next_made = timer->made;
        timer->made = taken;
    }
    return taken;
}

/**
 * Under the lock: takes the task the timer waits for out of it, to be cancelled outside the lock.
 *
 * @implNote A task whose `schedule` has not answered yet has no token to cancel, so its ticket stays where it is:
 * the thread scheduling it finds it there, sees the timer destroyed, and cancels the task itself.
 */
static cancelled_task stop_waiting(weivel_c_timer *timer) {
    cancelled_task stopped = {NULL, NULL};
    if (timer->has_task) {
        stopped.task = timer->task;
        stopped.called_with = timer->waited_for;
        timer->waited_for = NULL;
    }
    timer->has_task = 0;
    timer->task = NULL;
    return stopped;
}

/** Outside the lock: cancels a task, and spares its ticket when the scheduler answers its callback will never run. */
static void cancel_task(weivel_c_timer *timer, cancelled_task stopped) {
    if (stopped.task == NULL) {
        return;
    }
    if (timer->scheduler.cancel(timer->scheduler.data, stopped.task)) {
        weivel_c_mutex_lock(&timer->lock);
        spare_ticket(timer, stopped.called_with);
        weivel_c_mutex_unlock(&timer->lock);
    }
}

/**
 * When to wake for a deadline: the deadline itself, or as far towards it as the scheduler can wait.
 *
 * @implNote The distance is taken in unsigned arithmetic, which cannot overflow however far apart the two times lie.
 */
static int64_t wake_time(weivel_c_timer *timer, int64_t deadline) {
    int64_t longest = timer->scheduler.maximum_wait;
    int64_t now;
    if (longest <= 0) {
        return deadline;
    }
    now = read_the_clock(timer);
    if (deadline > now && (uint64_t)deadline - (uint64_t)now > (uint64_t)longest) {
        return now + longest;
    }
    return deadline;
}

static void run_task(void *callback_data);

/** Under the lock: notes what is armed and takes the ticket the new task will be called with. */
static weivel_c_status begin_arming(weivel_c_timer *timer, int64_t at, uint32_t set, cancelled_task *stopped,
                                    ticket **called_with) {
    *called_with = NULL;
    *stopped = stop_waiting(timer);
    timer->armed = set;
    timer->armed_at = at;
    if (!set) {
        return WEIVEL_C_STATUS_OK;
    }
    *called_with = take_ticket(timer);
    timer->waited_for = *called_with;
    return *called_with != NULL ? WEIVEL_C_STATUS_OK : WEIVEL_C_STATUS_OUT_OF_MEMORY;
}

/**
 * Under the lock: keeps the token of the task just scheduled, unless its callback already began or the timer was
 * destroyed meanwhile. Answers what is left to cancel.
 */
static cancelled_task keep_task(weivel_c_timer *timer, ticket *called_with, void *task) {
    cancelled_task stopped = {NULL, NULL};
    if (timer->waited_for != called_with) {
        return stopped;
    }
    if (task == NULL) {
        timer->waited_for = NULL;
        spare_ticket(timer, called_with);
    } else if (timer->destroyed) {
        timer->waited_for = NULL;
        stopped = (cancelled_task){task, called_with};
    } else {
        timer->task = task;
        timer->has_task = 1;
    }
    return stopped;
}

static weivel_c_status schedule_task(weivel_c_timer *timer, int64_t at, ticket *called_with) {
    void *task = timer->scheduler.schedule(timer->scheduler.data, wake_time(timer, at), run_task, called_with);
    cancelled_task stopped;
    weivel_c_mutex_lock(&timer->lock);
    stopped = keep_task(timer, called_with, task);
    weivel_c_mutex_unlock(&timer->lock);
    cancel_task(timer, stopped);
    return task != NULL ? WEIVEL_C_STATUS_OK : WEIVEL_C_STATUS_FAILED;
}

/** Under the lock: 1 when arming for `at`, or disarming, differs from the last move the observer was told. */
static int take_a_move(weivel_c_timer *timer, int64_t at, uint32_t set) {
    int moved = set != timer->told_armed || (set && at != timer->told_at);
    timer->told_armed = set;
    timer->told_at = at;
    return moved;
}

/** Outside every lock: tells the observer a move, and the sink when it answers a failure. */
static void tell_the_observer(weivel_c_timer *timer, int64_t at, uint32_t set) {
    weivel_c_status heard;
    if (timer->observer.moved == NULL) {
        return;
    }
    heard = timer->observer.moved(timer->observer.data, set, set ? at : 0);
    if (heard != WEIVEL_C_STATUS_OK) {
        weivel_c_failure failure = WEIVEL_INIT(weivel_c_failure);
        failure.kind = WEIVEL_C_FAILURE_TIMER_OBSERVER;
        failure.status = (uint32_t)heard;
        failure.event_kind = WEIVEL_EVENT_NONE;
        failure.text = "the timer's observer answered a failure for a move of the timer";
        weivel_c_core_report(timer->core, &failure);
    }
}

/** The wrapper's `follow` hook: arms the scheduler for the latest deadline drained, or disarms it, and tells so. */
static weivel_c_status follow(void *data, int64_t at, uint32_t set) {
    weivel_c_timer *timer = data;
    cancelled_task stopped = {NULL, NULL};
    ticket *called_with = NULL;
    weivel_c_status status = WEIVEL_C_STATUS_OK;
    int destroyed;
    int moved = 0;
    weivel_c_mutex_lock(&timer->lock);
    destroyed = (int)timer->destroyed;
    if (!destroyed) {
        moved = take_a_move(timer, at, set);
        status = begin_arming(timer, at, set, &stopped, &called_with);
    }
    weivel_c_mutex_unlock(&timer->lock);
    cancel_task(timer, stopped);
    if (called_with != NULL) {
        status = schedule_task(timer, at, called_with);
    }
    if (moved) {
        tell_the_observer(timer, at, set);
    }
    return status;
}

static void report_a_failed_advance(weivel_c_timer *timer, weivel_c_status status) {
    weivel_c_failure failure = WEIVEL_INIT(weivel_c_failure);
    failure.kind = WEIVEL_C_FAILURE_DEADLINE_HOOK;
    failure.status = (uint32_t)status;
    failure.event_kind = WEIVEL_EVENT_NONE;
    failure.text = "the timer could not hand the core the time its deadline fell due at";
    weivel_c_core_report(timer->core, &failure);
}

/**
 * Under the lock: whether the callback called with `called_with` is the one the timer waits for, which it then no
 * longer does. A callback of any other task, cancelled or of a destroyed timer, is none of the timer's business.
 */
static int begin_callback(weivel_c_timer *timer, ticket *called_with, int64_t *armed_at) {
    int waited_for = timer->waited_for == called_with;
    if (waited_for) {
        timer->waited_for = NULL;
        timer->has_task = 0;
        timer->task = NULL;
    }
    spare_ticket(timer, called_with);
    *armed_at = timer->armed_at;
    return waited_for && !timer->destroyed && timer->armed;
}

/**
 * What the scheduler calls: hands the core the time when the deadline has come, and arms again when it has not,
 * which is how a scheduler that cannot wait the whole way is waited out.
 */
static void run_task(void *callback_data) {
    ticket *called_with = callback_data;
    weivel_c_timer *timer = called_with->timer;
    int64_t armed_at = 0;
    int64_t now;
    weivel_c_status status;
    int live;
    weivel_c_mutex_lock(&timer->lock);
    live = begin_callback(timer, called_with, &armed_at);
    weivel_c_mutex_unlock(&timer->lock);
    if (!live) {
        return;
    }
    now = read_the_clock(timer);
    if (now < armed_at) {
        weivel_c_core_follow_deadline_again(timer->core);
        return;
    }
    status = weivel_c_core_advance(timer->core, now, NULL);
    if (status != WEIVEL_C_STATUS_OK && status != WEIVEL_C_STATUS_DESTROYED) {
        report_a_failed_advance(timer, status);
    }
}

static int is_usable(const weivel_c_scheduler *scheduler) {
    if (scheduler == NULL) {
        return 1;
    }
    if (scheduler->size < FIRST_SCHEDULER_SIZE || scheduler->now == NULL || scheduler->schedule == NULL ||
        scheduler->cancel == NULL) {
        return 0;
    }
    return scheduler->size < END_OF(weivel_c_scheduler, maximum_wait) || scheduler->maximum_wait >= 0;
}

static void adopt_scheduler(weivel_c_timer *timer, const weivel_c_scheduler *scheduler) {
    size_t covered = scheduler->size < sizeof timer->scheduler ? scheduler->size : sizeof timer->scheduler;
    memcpy(&timer->scheduler, scheduler, covered);
    timer->scheduler.size = (uint32_t)sizeof timer->scheduler;
    timer->keeps_time = 1;
}

static weivel_c_deadline_hooks hooks_of(weivel_c_timer *timer) {
    weivel_c_deadline_hooks hooks = WEIVEL_INIT(weivel_c_deadline_hooks);
    hooks.now = read_the_clock;
    hooks.follow = follow;
    hooks.data = timer;
    return hooks;
}

static void discard(weivel_c_timer *timer) {
    ticket *made = timer->made;
    while (made != NULL) {
        ticket *next = made->next_made;
        free(made);
        made = next;
    }
    weivel_c_mutex_end(&timer->lock);
    free(timer);
}

weivel_c_status weivel_c_timer_create(weivel_c_core *core, const weivel_c_scheduler *scheduler,
                                      const weivel_c_timer_observer *observer, weivel_c_timer **timer) {
    weivel_c_timer *created;
    weivel_c_status status = WEIVEL_C_STATUS_OK;
    if (timer == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    *timer = NULL;
    if (core == NULL || !is_usable(scheduler)) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    created = calloc(1, sizeof *created);
    if (created == NULL || !weivel_c_mutex_start(&created->lock)) {
        free(created);
        return WEIVEL_C_STATUS_OUT_OF_MEMORY;
    }
    created->core = core;
    if (observer != NULL) {
        created->observer = *observer;
    }
    if (scheduler != NULL) {
        weivel_c_deadline_hooks hooks = hooks_of(created);
        adopt_scheduler(created, scheduler);
        status = weivel_c_core_install_deadline_hooks(core, &hooks);
    }
    if (status != WEIVEL_C_STATUS_OK) {
        discard(created);
        return status;
    }
    *timer = created;
    return WEIVEL_C_STATUS_OK;
}

weivel_c_status weivel_c_timer_destroy(weivel_c_timer *timer) {
    cancelled_task stopped = {NULL, NULL};
    int first;
    if (timer == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    weivel_c_mutex_lock(&timer->lock);
    first = !timer->destroyed;
    if (first) {
        timer->destroyed = 1;
        timer->armed = 0;
        stopped = stop_waiting(timer);
    }
    weivel_c_mutex_unlock(&timer->lock);
    if (first && timer->keeps_time) {
        weivel_c_deadline_hooks hooks = hooks_of(timer);
        weivel_c_core_remove_deadline_hooks(timer->core, &hooks);
        cancel_task(timer, stopped);
    }
    return WEIVEL_C_STATUS_OK;
}

weivel_c_status weivel_c_timer_free(weivel_c_timer *timer) {
    int destroyed;
    if (timer == NULL) {
        return WEIVEL_C_STATUS_OK;
    }
    weivel_c_mutex_lock(&timer->lock);
    destroyed = (int)timer->destroyed;
    weivel_c_mutex_unlock(&timer->lock);
    if (!destroyed) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    discard(timer);
    return WEIVEL_C_STATUS_OK;
}

weivel_c_status weivel_c_timer_now(weivel_c_timer *timer, int64_t *now) {
    int destroyed;
    if (timer == NULL || now == NULL || !timer->keeps_time) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    weivel_c_mutex_lock(&timer->lock);
    destroyed = (int)timer->destroyed;
    weivel_c_mutex_unlock(&timer->lock);
    if (destroyed) {
        return WEIVEL_C_STATUS_DESTROYED;
    }
    *now = read_the_clock(timer);
    return WEIVEL_C_STATUS_OK;
}

weivel_c_status weivel_c_timer_deadline(weivel_c_timer *timer, int64_t *deadline) {
    int armed;
    if (timer == NULL || deadline == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    weivel_c_mutex_lock(&timer->lock);
    armed = (int)timer->armed;
    if (armed) {
        *deadline = timer->armed_at;
    }
    weivel_c_mutex_unlock(&timer->lock);
    return armed ? WEIVEL_C_STATUS_OK : WEIVEL_C_STATUS_NONE;
}
