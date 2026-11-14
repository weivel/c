/**
 * @file
 * The host held at a moment only an allocation of the layer's reaches, through the allocator seams of the testing
 * build: a thread stopped inside an allocation, or an allocation failed, at the step the test names.
 */

#include <stdlib.h>

#include "gates.h"
#include "plugins.h"

#include "../../src/host/host.h"

/** An allocator that holds the thread making the armed allocation at a gate, and allocates plainly otherwise. */
typedef struct held_allocation {
    test_mutex mutex;
    /** Guarded by `mutex`: 1 while the next allocation is the one held. */
    uint32_t armed;
    test_gate reached;
    test_gate free_to_go;
} held_allocation;

static void *allocate_after_the_gate(void *data, size_t size) {
    held_allocation *self = data;
    uint32_t holding;
    lock_test_mutex(&self->mutex);
    holding = self->armed;
    self->armed = 0;
    unlock_test_mutex(&self->mutex);
    if (holding) {
        open_gate(&self->reached);
        pass_gate(&self->free_to_go);
    }
    return malloc(size);
}

static void release_plainly(void *data, void *memory) {
    (void)data;
    free(memory);
}

static void hold_the_next_allocation(held_allocation *self, weivel_c_host *host) {
    weivel_c_allocator allocator = {allocate_after_the_gate, release_plainly, self};
    start_test_mutex(&self->mutex);
    start_gate(&self->reached);
    start_gate(&self->free_to_go);
    self->armed = 1;
    weivel_c_host_replace_allocator(host, &allocator);
}

typedef struct activating {
    host_world *in;
    test_plugin *plugin;
    weivel_c_status answered;
} activating;

static void activate_on_its_own_thread(void *argument) {
    activating *self = argument;
    weivel_c_activation activation = WEIVEL_INIT(weivel_c_activation);
    weivel_activation_plan plan = WEIVEL_INIT(weivel_activation_plan);
    activation.plugins = &self->plugin->handle;
    activation.count = 1;
    activation.edge_source = WEIVEL_EDGES_CAPABILITIES;
    activation.listener.hear = hear_outcome;
    activation.listener.data = &self->in->told;
    self->answered = weivel_c_host_activate(self->in->host, &activation, &plan, NULL);
    if (self->answered == WEIVEL_C_STATUS_OK) {
        CHECK(weivel_c_host_plan_release(self->in->host, &plan) == WEIVEL_C_STATUS_OK);
    }
}

/**
 * Holds an activation after the core answered its plan and before it starts a plugin, at the allocation of its
 * completions, while another thread destroys the host: with nothing opened the destroy would otherwise end and destroy
 * the core, and the plugin's `released` outcome could no longer be queued.
 */
static void a_destroy_made_while_an_activation_is_planned_waits_to_tell_its_released_outcome(void) {
    static held_allocation held;
    host_world in;
    test_plugin late;
    test_thread thread;
    activating activation = {&in, &late, WEIVEL_C_STATUS_FAILED};
    uint32_t finished = 9;
    uint32_t kind = 0;
    begin_host_world(&in);
    name_acting_plugin(&late, &in, "late", activate_and_answer_done, NULL);
    register_test_plugin(&late);
    hold_the_next_allocation(&held, in.host);
    CHECK(start_test_thread(&thread, activate_on_its_own_thread, &activation));
    pass_gate(&held.reached);
    CHECK(weivel_c_host_destroy(in.host, &finished) == WEIVEL_C_STATUS_OK && finished == 0);
    open_gate(&held.free_to_go);
    join_test_thread(&thread);
    CHECK(activation.answered == WEIVEL_C_STATUS_OK);
    CHECK(place_of(&in.heard, "activate late") < 0);
    CHECK(outcomes_of(&in.told, late.handle, &kind) == 1 && kind == WEIVEL_C_OUTCOME_RELEASED);
    CHECK(weivel_c_host_destroy(in.host, &finished) == WEIVEL_C_STATUS_OK && finished == 1);
    CHECK(sink_count(&in) == 0);
    CHECK(weivel_c_host_free(in.host) == WEIVEL_C_STATUS_OK);
}

int main(int argument_count, char **arguments) {
    if (argument_count != 2 || !load_tables(arguments[1])) {
        fprintf(stderr, "usage: host_seams_test <core-library>\n");
        return 2;
    }
    a_destroy_made_while_an_activation_is_planned_waits_to_tell_its_released_outcome();
    CHECK(weivel_c_core_unload(&real_table) == WEIVEL_C_STATUS_OK);
    return check_summary("host seams");
}
