/**
 * @file
 * Several plugins on several threads, each registering its own service and wanting, getting and watching the
 * others' through its context: every want settles exactly once with its provider's own pointer, and every pointer a
 * get or a watch answers is the provider's.
 *
 * @implNote What a callback records is deliberately unguarded. Callbacks run only from the dispatch, one thread at a
 * time, handed on under the layer lock, so a callback run under another thread's feet is a data race the thread
 * sanitizer names.
 */

#include "world.h"

#define MEMBERS 4
#define ROUNDS 150

typedef struct want_slot {
    void *expected;
    uint32_t settled;
    uint32_t right;
} want_slot;

typedef struct watch_slot {
    void *expected;
    uint32_t registered;
    uint32_t unregistered;
    uint32_t wrong;
} watch_slot;

typedef struct member {
    world *in;
    struct member *all;
    plugin self;
    uint32_t index;
    char id[16];
    int object;
    want_slot wants[ROUNDS][MEMBERS];
    watch_slot watches[MEMBERS];
    weivel_handle watch_handles[MEMBERS];
    uint32_t failures;
    uint32_t wrong_pointers;
    test_thread thread;
} member;

static member members[MEMBERS];

static weivel_c_status settle_slot(void *data, const weivel_c_settlement *settlement) {
    want_slot *slot = data;
    slot->settled++;
    slot->right += settlement->service == slot->expected && settlement->refusal.code == WEIVEL_REFUSAL_NONE;
    return WEIVEL_C_STATUS_OK;
}

static weivel_c_status hear_slot(void *data, const weivel_c_service_change *change) {
    watch_slot *slot = data;
    if (change->change == WEIVEL_CHANGE_REGISTERED) {
        slot->registered++;
        slot->wrong += change->service != slot->expected;
    } else {
        slot->unregistered++;
        slot->wrong += change->service != NULL;
    }
    return WEIVEL_C_STATUS_OK;
}

static void want_and_get_the_others(member *self, uint32_t round) {
    uint32_t other;
    for (other = 0; other < MEMBERS; other++) {
        want_slot *slot = &self->wants[round][other];
        weivel_c_want_callback callback = {settle_slot, slot};
        weivel_c_status status;
        void *service = NULL;
        if (other == self->index) {
            continue;
        }
        slot->expected = &self->all[other].object;
        self->failures += weivel_c_context_want_service(self->self.context, self->all[other].id, NULL, &callback,
                                                        NULL, NULL) != WEIVEL_C_STATUS_OK;
        status = weivel_c_context_get_service(self->self.context, self->all[other].id, &service, NULL);
        self->failures += status != WEIVEL_C_STATUS_OK && status != WEIVEL_C_STATUS_NONE;
        self->wrong_pointers += status == WEIVEL_C_STATUS_OK && service != slot->expected;
    }
}

static void work(void *argument) {
    member *self = argument;
    uint32_t round;
    for (round = 0; round < ROUNDS; round++) {
        weivel_handle registration = 0;
        self->failures += weivel_c_context_register_service(self->self.context, self->id, &self->object, NULL,
                                                            &registration, NULL) != WEIVEL_C_STATUS_OK;
        want_and_get_the_others(self, round);
        let_other_threads_run();
        self->failures +=
            weivel_c_context_unregister_service(self->self.context, registration, NULL) != WEIVEL_C_STATUS_OK;
    }
}

static void open_members(world *in) {
    uint32_t index;
    uint32_t other;
    for (index = 0; index < MEMBERS; index++) {
        member *self = &members[index];
        memset(self, 0, sizeof *self);
        self->in = in;
        self->all = members;
        self->index = index;
        snprintf(self->id, sizeof self->id, "t%u:s", (unsigned)index);
    }
    for (index = 0; index < MEMBERS; index++) {
        member *self = &members[index];
        char name[8];
        snprintf(name, sizeof name, "t%u", (unsigned)index);
        name_plugin(&self->self, name);
        provides(&self->self, self->id);
        for (other = 0; other < MEMBERS; other++) {
            if (other != index) {
                consumes(&self->self, members[other].id);
            }
        }
        open_in(in, &self->self);
    }
}

static void watch_the_others(member *self) {
    uint32_t other;
    for (other = 0; other < MEMBERS; other++) {
        weivel_c_service_watcher watcher = {hear_slot, &self->watches[other]};
        if (other == self->index) {
            continue;
        }
        self->watches[other].expected = &self->all[other].object;
        CHECK(weivel_c_context_watch_service(self->self.context, self->all[other].id, &watcher,
                                             &self->watch_handles[other], NULL) == WEIVEL_C_STATUS_OK);
    }
}

/** Registers every service once more, so a want made while its provider was between two registrations settles. */
static void register_every_service_for_good(void) {
    uint32_t index;
    for (index = 0; index < MEMBERS; index++) {
        CHECK(weivel_c_context_register_service(members[index].self.context, members[index].id,
                                                &members[index].object, NULL, NULL, NULL) == WEIVEL_C_STATUS_OK);
    }
}

static void check_member(const member *self) {
    uint32_t settled_once = 0;
    uint32_t right = 0;
    uint32_t round;
    uint32_t other;
    for (round = 0; round < ROUNDS; round++) {
        for (other = 0; other < MEMBERS; other++) {
            settled_once += self->wants[round][other].settled == 1;
            right += self->wants[round][other].right == 1;
        }
    }
    CHECK(settled_once == ROUNDS * (MEMBERS - 1));
    CHECK(right == ROUNDS * (MEMBERS - 1));
    CHECK(self->failures == 0 && self->wrong_pointers == 0);
    for (other = 0; other < MEMBERS; other++) {
        if (other != self->index) {
            CHECK(self->watches[other].wrong == 0);
            CHECK(self->watches[other].registered == ROUNDS + 1);
            CHECK(self->watches[other].unregistered == ROUNDS);
        }
    }
}

static void plugins_on_several_threads_resolve_each_others_services_exactly_once_and_rightly(void) {
    world in;
    uint32_t index;
    begin_world(&in, NULL, 1);
    open_members(&in);
    for (index = 0; index < MEMBERS; index++) {
        watch_the_others(&members[index]);
    }
    for (index = 0; index < MEMBERS; index++) {
        CHECK(start_test_thread(&members[index].thread, work, &members[index]));
    }
    for (index = 0; index < MEMBERS; index++) {
        join_test_thread(&members[index].thread);
    }
    register_every_service_for_good();
    for (index = 0; index < MEMBERS; index++) {
        check_member(&members[index]);
    }
    CHECK(records_of(&in) == MEMBERS + MEMBERS * (MEMBERS - 1));
    for (index = 0; index < MEMBERS; index++) {
        CHECK(weivel_c_core_release_plugin(in.core, members[index].self.handle, NULL) == WEIVEL_C_STATUS_OK);
        end_entries_of(&in, &members[index].self);
    }
    CHECK(records_of(&in) == 0 && holdings_of(&in) == 0);
    CHECK(in.sink.count == 0);
    end_world(&in);
}

int main(int argument_count, char **arguments) {
    if (argument_count != 2 || !load_every_table(arguments[1])) {
        fprintf(stderr, "usage: context_threads_test <core-library>\n");
        return 2;
    }
    plugins_on_several_threads_resolve_each_others_services_exactly_once_and_rightly();
    CHECK(weivel_c_core_unload(&real_table) == WEIVEL_C_STATUS_OK);
    return check_summary("context threads");
}
