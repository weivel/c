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

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#endif

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
        self->failures += weivel_c_core_defer(self->core, &self->steps[round]) != WEIVEL_C_STATUS_OK;
        self->failures += weivel_c_core_unregister(self->core, registration, NULL) != WEIVEL_C_STATUS_OK;
        self->failures += !is_refused_as_stale(self->core, registration);
    }
}

#if defined(_WIN32)
static DWORD WINAPI run_worker(LPVOID argument) {
    work(argument);
    return 0;
}

static void run_all(worker *workers) {
    HANDLE threads[THREADS - 1];
    int index;
    for (index = 1; index < THREADS; index++) {
        threads[index - 1] = CreateThread(NULL, 0, run_worker, &workers[index], 0, NULL);
        CHECK(threads[index - 1] != NULL);
    }
    work(&workers[0]);
    WaitForMultipleObjects(THREADS - 1, threads, TRUE, INFINITE);
    for (index = 0; index < THREADS - 1; index++) {
        CloseHandle(threads[index]);
    }
}
#else
static void *run_worker(void *argument) {
    work(argument);
    return NULL;
}

static void run_all(worker *workers) {
    pthread_t threads[THREADS - 1];
    int index;
    for (index = 1; index < THREADS; index++) {
        CHECK(pthread_create(&threads[index - 1], NULL, run_worker, &workers[index]) == 0);
    }
    work(&workers[0]);
    for (index = 0; index < THREADS - 1; index++) {
        pthread_join(threads[index], NULL);
    }
}
#endif

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

int main(int argument_count, char **arguments) {
    if (argument_count != 2 || !load_tables(arguments[1])) {
        fprintf(stderr, "usage: threads_test <core-library>\n");
        return 2;
    }
    calls_from_several_threads_are_dispatched_once_in_drained_order_by_one_thread();
    CHECK(weivel_c_core_unload(&real_table) == WEIVEL_C_STATUS_OK);
    return check_summary("core threads");
}
