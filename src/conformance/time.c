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
}

void conformance_world_run_until(conformance_world *world, int64_t until) {
    virtual_task *task;
    while ((task = earliest_due(&world->time, until)) != NULL) {
        run_task(world, task);
    }
    conformance_world_move_clock_to(world, until);
}

void conformance_world_move_clock_to(conformance_world *world, int64_t until) {
    if (until > world->time.now) {
        world->time.now = until;
    }
}

weivel_c_status conformance_world_hear_timer_move(void *data, uint32_t armed, int64_t deadline) {
    conformance_world *world = data;
    timer_move moved = {world->time.now, armed ? deadline : 0, armed != 0};
    if (world->move_count < CONFORMANCE_MOVES) {
        world->moves[world->move_count++] = moved;
    }
    return WEIVEL_C_STATUS_OK;
}
