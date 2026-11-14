/**
 * @file
 * Activates that finish later, from several threads, while another thread releases some of their plugins: each
 * started plugin is told exactly one outcome, every finish is answered as a value, and nothing reaches the sink.
 *
 * @implNote What the outcome listener records is guarded by its own mutex; what an entry function records is not,
 * since the host calls each from the dispatch or from the thread that finishes it, so a call made under another
 * thread's feet is a race the thread sanitizer names.
 */

#include "plugins.h"

#define PLUGINS 12
#define FINISHERS 3

typedef struct crowd {
    host_world in;
    test_plugin plugins[PLUGINS];
    char names[PLUGINS][16];
    weivel_c_status finished_as[PLUGINS];
    test_thread finishers[FINISHERS];
    test_thread releaser;
    uint32_t releases_refused;
} crowd;

static crowd everyone;

static void finish_every_third(void *argument) {
    uint32_t first = (uint32_t)(size_t)argument;
    uint32_t index;
    for (index = first; index < PLUGINS; index += FINISHERS) {
        everyone.finished_as[index] =
            weivel_c_completion_finish(everyone.plugins[index].activation, WEIVEL_C_ENTRY_DONE, NULL);
        let_other_threads_run();
    }
}

static void release_every_fourth(void *argument) {
    uint32_t index;
    (void)argument;
    for (index = 0; index < PLUGINS; index += 4) {
        everyone.releases_refused += weivel_c_host_release(everyone.in.host, everyone.plugins[index].handle, NULL) !=
                                     WEIVEL_C_STATUS_OK;
        let_other_threads_run();
    }
}

static void open_everyone(void) {
    test_plugin *all[PLUGINS];
    uint32_t index;
    for (index = 0; index < PLUGINS; index++) {
        snprintf(everyone.names[index], sizeof everyone.names[index], "p%u", (unsigned)index);
        name_acting_plugin(&everyone.plugins[index], &everyone.in, everyone.names[index], activate_and_wait, NULL);
        everyone.plugins[index].entry.deactivate = deactivate_and_answer_done;
        register_test_plugin(&everyone.plugins[index]);
        all[index] = &everyone.plugins[index];
    }
    CHECK(activate_in_order(&everyone.in, all, PLUGINS, WEIVEL_EDGES_CAPABILITIES));
}

static void check_everyone_was_told_once(void) {
    uint32_t index;
    for (index = 0; index < PLUGINS; index++) {
        uint32_t kind = 0;
        weivel_c_status finished = everyone.finished_as[index];
        CHECK(outcomes_of(&everyone.in.told, everyone.plugins[index].handle, &kind) == 1);
        if (index % 4 != 0) {
            CHECK(finished == WEIVEL_C_STATUS_OK && kind == WEIVEL_C_OUTCOME_ACTIVE);
        } else {
            CHECK((finished == WEIVEL_C_STATUS_NONE && kind == WEIVEL_C_OUTCOME_RELEASED) ||
                  (finished == WEIVEL_C_STATUS_OK &&
                   (kind == WEIVEL_C_OUTCOME_ACTIVE || kind == WEIVEL_C_OUTCOME_RELEASED)));
        }
    }
}

static void activates_finished_on_several_threads_while_another_releases_are_each_told_once(void) {
    uint32_t index;
    memset(&everyone, 0, sizeof everyone);
    begin_host_world(&everyone.in);
    open_everyone();
    for (index = 0; index < FINISHERS; index++) {
        CHECK(start_test_thread(&everyone.finishers[index], finish_every_third, (void *)(size_t)index));
    }
    CHECK(start_test_thread(&everyone.releaser, release_every_fourth, NULL));
    for (index = 0; index < FINISHERS; index++) {
        join_test_thread(&everyone.finishers[index]);
    }
    join_test_thread(&everyone.releaser);
    CHECK(everyone.releases_refused == 0);
    check_everyone_was_told_once();
    CHECK(sink_count(&everyone.in) == 0);
    end_host_world(&everyone.in);
}

int main(int argument_count, char **arguments) {
    uint32_t round;
    if (argument_count != 2 || !load_tables(arguments[1])) {
        fprintf(stderr, "usage: host_threads_test <core-library>\n");
        return 2;
    }
    for (round = 0; round < 20; round++) {
        activates_finished_on_several_threads_while_another_releases_are_each_told_once();
    }
    CHECK(weivel_c_core_unload(&real_table) == WEIVEL_C_STATUS_OK);
    return check_summary("host threads");
}
