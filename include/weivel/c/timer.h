#ifndef WEIVEL_C_TIMER_H
#define WEIVEL_C_TIMER_H

/**
 * @file
 * The `timer` module of the weivel C language implementation: it arms the core's next deadline on a scheduler the
 * caller gives. The layer owns no thread, no clock and no timer of its own.
 *
 * - The timer is armed from the latest WEIVEL_EVENT_DEADLINE the wrapper drained and from nothing else: an
 *   announcement with a deadline schedules one task for it, cancelling the task of any deadline armed before, and an
 *   announcement of none cancels it. That happens as the call that drained the announcement leaves the wrapper,
 *   before any of its events is dispatched.
 * - When the task runs it reads the clock and hands the core that time with weivel_advance, through the wrapper. The
 *   events that advance queues arm or disarm the timer as any others do.
 * - A scheduler that cannot wait as long as the deadline lies away says so in `maximum_wait`: the timer then waits as
 *   long as it may and arms again when that wait ends, never advancing the core before the deadline.
 * - The core learns that a deadline has passed before any other call reaches it, whether or not the task has run:
 *   while a deadline is armed the wrapper reads this clock before each call, once, and advances the core first when
 *   the clock has reached the deadline. While nothing is armed no clock is read.
 * - The timer keeps no list of the calls that move a deadline, never asks the core for its deadline, and hands the
 *   core no time but the one the caller's clock answered.
 *
 * A host created without a scheduler has a timer that arms nothing, checks nothing and reads no clock. A call that
 * needs the time is then the caller's mistake: weivel_c_timer_now is where the layer takes the time for a want with a
 * timeout or with constraints, and it answers WEIVEL_C_STATUS_MISUSE before anything is asked of the core.
 */

#include <stdint.h>

#include <weivel/c/core.h>

#ifdef __cplusplus
extern "C" {
#endif

/** What the timer has a scheduler run: called once with the `callback_data` it was scheduled with. */
typedef void (*weivel_c_task_callback)(void *callback_data);

/**
 * The caller's clock and scheduler. The three functions are required and are called with `data`, on whichever thread
 * calls into the core, never under a lock of the layer's, so each may itself call into the core.
 *
 * What the scheduler must hold to:
 *
 * - It runs a task's callback at most once, on any thread, and not before the clock has reached the time the task
 *   was scheduled for by more than it must: a callback that runs early is harmless and is armed again.
 * - While it runs a callback it holds no lock that its own `schedule` or `cancel` take, because the callback calls
 *   into the core, and what that queues calls `cancel` and `schedule` from inside the callback.
 * - Whatever lock `now`, `schedule` or `cancel` take is never held while calling into the core.
 * - It stays usable until the timer is freed.
 *
 * A scheduler whose `cancel` answers 0 for a task it did take out can never show the timer that the task's callback
 * is past, so the timer keeps what that callback would be called with, and makes more for each task it schedules
 * after: the timer's memory then grows with every deadline it follows, until weivel_c_timer_free. The host of such a
 * scheduler has the same burden: it keeps the timer until it knows by its own means that no callback can still
 * arrive. A scheduler that can tell answers 1 whenever it took the task out, and costs neither.
 */
typedef struct weivel_c_scheduler {
    uint32_t size;
    /** The time now, in milliseconds on a monotonic scale of the caller's: the scale every deadline is counted on. */
    int64_t (*now)(void *data);
    /**
     * Schedules `callback` to be called once with `callback_data` when the clock reaches `at`, at once when it
     * already has. Answers the task, a token only `cancel` reads, or NULL when it could not, which is reported to
     * the error sink and leaves the deadline to the wrapper's check before each call.
     */
    void *(*schedule)(void *data, int64_t at, weivel_c_task_callback callback, void *callback_data);
    /**
     * Cancels a task `schedule` answered, whose callback the timer has not seen begin. Answers 1 when the task was
     * taken out before its callback began, so that callback will never be called; and 0 when the callback has begun
     * or has returned, or when the scheduler cannot tell, which is always safe to answer.
     */
    int (*cancel)(void *data, void *task);
    void *data;
    /**
     * The longest the scheduler can wait, in milliseconds; 0, or a field beyond `size`, when it can wait any time.
     * A deadline further away is waited for in steps of this length, the clock read once as each step is scheduled.
     */
    int64_t maximum_wait;
} weivel_c_scheduler;

/** One core's timer. Opaque; made by weivel_c_timer_create, ended by weivel_c_timer_destroy and freed by
 * weivel_c_timer_free. */
typedef struct weivel_c_timer weivel_c_timer;

/**
 * Makes the timer of a core. With a scheduler it installs itself as the core's deadline hooks, and a deadline the
 * core already waits on is armed at once.
 *
 * A core is followed by one thing at a time, and a timer displaces nothing: on a core that already has deadline
 * hooks, another timer's or ones set with weivel_c_core_set_deadline_hooks, a timer with a scheduler is refused and
 * the hooks in place stay. Whoever wants another scheduler destroys the first timer, or removes the hooks, first.
 * Hooks set over a timer's afterwards replace them, and the timer then follows nothing; destroying it leaves those
 * hooks alone.
 *
 * A timer without a scheduler installs nothing, so it is never refused and leaves whatever hooks the core has.
 *
 * @param core the core to follow. Required. It must not be freed before the timer is destroyed.
 * @param scheduler the caller's clock and scheduler, copied; NULL for a timer that arms nothing and reads no clock.
 * @param timer receives the timer, owned by the caller until weivel_c_timer_free. Required; NULL on every answer but
 * OK.
 * @return WEIVEL_C_STATUS_OK; WEIVEL_C_STATUS_MISUSE for a NULL `core` or `timer`, a scheduler smaller than its first
 * shape or missing a function, a negative `maximum_wait`, or a scheduler given for a core that already has deadline
 * hooks; WEIVEL_C_STATUS_OUT_OF_MEMORY; WEIVEL_C_STATUS_DESTROYED for a destroyed core.
 * Threading: any thread.
 */
WEIVEL_C_API weivel_c_status weivel_c_timer_create(weivel_c_core *core, const weivel_c_scheduler *scheduler,
                                                   weivel_c_timer **timer);

/**
 * Disarms the timer for good: it cancels the one task it holds and takes its hooks off the core, when the core's
 * hooks are still the timer's own, so nothing of the layer's is left for the scheduler to run. An owner calls it
 * before it destroys the core.
 *
 * A callback of the timer's that the scheduler begins all the same, because it had begun before the cancel or
 * because the scheduler could not take it out, takes the timer's lock, sees the timer destroyed and does nothing: it
 * reads no clock and calls nothing. A callback that had already passed that check when this was called goes on to a
 * call into the core, which a destroyed core answers WEIVEL_C_STATUS_DESTROYED without doing anything. So the timer
 * and the core both stay until their free calls.
 *
 * @return WEIVEL_C_STATUS_OK, also for a timer destroyed already; WEIVEL_C_STATUS_MISUSE for NULL.
 * Threading: any thread. A call into the core in flight on another thread may still read the clock once, or
 * schedule one task and cancel it itself, as this returns.
 */
WEIVEL_C_API weivel_c_status weivel_c_timer_destroy(weivel_c_timer *timer);

/**
 * Frees a destroyed timer.
 *
 * @return WEIVEL_C_STATUS_OK, also for NULL; WEIVEL_C_STATUS_MISUSE, freeing nothing, for a timer not destroyed.
 * Threading: no call on the timer or its core may be in flight or follow, and the scheduler must be past running any
 * callback of the timer's: each task it was asked to cancel was either taken out or has returned.
 */
WEIVEL_C_API weivel_c_status weivel_c_timer_free(weivel_c_timer *timer);

/**
 * Answers the time now on the caller's clock: what the layer hands the core as `now` for a want with a timeout or
 * with constraints. It is the one place such a call is refused when there is no clock to time it by.
 *
 * @param now receives the time. Required.
 * @return WEIVEL_C_STATUS_OK; WEIVEL_C_STATUS_MISUSE, with nothing asked of the core, for a timer made without a
 * scheduler, or a NULL argument; WEIVEL_C_STATUS_DESTROYED.
 * Threading: any thread.
 */
WEIVEL_C_API weivel_c_status weivel_c_timer_now(weivel_c_timer *timer, int64_t *now);

/**
 * Answers the deadline the timer is armed for, as it was last told: what the layer says it did with its timer.
 *
 * @param deadline receives the deadline. Required.
 * @return WEIVEL_C_STATUS_OK; WEIVEL_C_STATUS_NONE when it is disarmed, which a timer without a scheduler and a
 * destroyed one always are; WEIVEL_C_STATUS_MISUSE.
 * Threading: any thread.
 */
WEIVEL_C_API weivel_c_status weivel_c_timer_deadline(weivel_c_timer *timer, int64_t *deadline);

#ifdef __cplusplus
}
#endif

#endif
