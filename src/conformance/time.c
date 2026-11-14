#include "world.h"

static int64_t virtual_now(void *data) {
    conformance_world *world = data;
    return world->time.now;
}

static void *virtual_schedule(void *data, int64_t at, weivel_c_task_callback callback, void *callback_data) {
    conformance_world *world = data;
    virtual_task *task;
    if (world->time.task_count == CONFORMANCE_TASKS) {
        return NULL;
    }
    task = &world->time.tasks[world->time.task_count++];
    task->at = at;
    task->callback = callback;
    task->callback_data = callback_data;
    task->waiting = 1;
    return task;
}

static int virtual_cancel(void *data, void *token) {
    virtual_task *task = token;
    (void)data;
    if (!task->waiting) {
        return 0;
    }
    task->waiting = 0;
    return 1;
}

weivel_c_scheduler conformance_virtual_scheduler(conformance_world *world) {
    weivel_c_scheduler scheduler = WEIVEL_INIT(weivel_c_scheduler);
    scheduler.now = virtual_now;
    scheduler.schedule = virtual_schedule;
    scheduler.cancel = virtual_cancel;
    scheduler.data = world;
    return scheduler;
}

static virtual_task *earliest_due(virtual_time *time, int64_t until) {
    virtual_task *earliest = NULL;
    uint32_t index;
    for (index = 0; index < time->task_count; index++) {
        virtual_task *task = &time->tasks[index];
        if (task->waiting && task->at <= until && (earliest == NULL || task->at < earliest->at)) {
            earliest = task;
        }
    }
    return earliest;
}

static void run_task(conformance_world *world, virtual_task *task) {
    virtual_time *time = &world->time;
    task->waiting = 0;
    time->now = task->at > time->now ? task->at : time->now;
    if (time->fired_count < CONFORMANCE_MOVES) {
        time->fired[time->fired_count++] = time->now;
    }
    task->callback(task->callback_data);
    conformance_world_note_timer(world);
}

void conformance_world_run_until(conformance_world *world, int64_t until) {
    virtual_task *task;
    while ((task = earliest_due(&world->time, until)) != NULL) {
        run_task(world, task);
    }
    conformance_world_move_clock_to(world, until);
    conformance_world_note_timer(world);
}

void conformance_world_move_clock_to(conformance_world *world, int64_t until) {
    if (until > world->time.now) {
        world->time.now = until;
    }
}

static int same_move(const timer_move *left, const timer_move *right) {
    return left->armed == right->armed && (!left->armed || left->deadline == right->deadline);
}

void conformance_world_note_timer(conformance_world *world) {
    weivel_c_timer *timer = NULL;
    timer_move now = {world->time.now, 0, 0};
    if (weivel_c_host_timer(world->host, &timer) != WEIVEL_C_STATUS_OK) {
        return;
    }
    now.armed = weivel_c_timer_deadline(timer, &now.deadline) == WEIVEL_C_STATUS_OK;
    if (same_move(&now, &world->noted)) {
        return;
    }
    world->noted = now;
    if (world->move_count < CONFORMANCE_MOVES) {
        world->moves[world->move_count++] = now;
    }
}
