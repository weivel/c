/**
 * @file
 * The host held at a moment only an allocation of the layer's reaches, through the allocator seams of the testing
 * build: a thread stopped inside an allocation, or an allocation failed, at the step the test names.
 */

#include <stdlib.h>

#include "gates.h"
#include "plugins.h"

#include "../../src/context/contexts.h"
#include "../../src/handles/table.h"
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

#define SEAM_PLUGINS 40
#define SEAM_ATTEMPTS 3

/** An allocator that fails the armed allocation, counted from 1 since it was armed, and allocates plainly otherwise. */
typedef struct failing_allocation {
    uint32_t fail_at;
    uint32_t made;
} failing_allocation;

static void *allocate_unless_armed(void *data, size_t size) {
    failing_allocation *self = data;
    self->made++;
    return self->made == self->fail_at ? NULL : malloc(size);
}

static void arm_the_failure(failing_allocation *self, uint32_t fail_at) {
    self->fail_at = fail_at;
    self->made = 0;
}

/** How often weivel_register_manifest reached the core since the process began. */
static uint32_t registrations_reached;
static weivel_status (*register_manifest_of_the_core)(weivel_core *core, const weivel_manifest *manifest,
                                                      weivel_handle *plugin, uint32_t *pending_events);

static weivel_status count_the_registration(weivel_core *core, const weivel_manifest *manifest, weivel_handle *plugin,
                                            uint32_t *pending_events) {
    registrations_reached++;
    return register_manifest_of_the_core(core, manifest, plugin, pending_events);
}

static void seam_every_allocation_of_a_registration(host_world *in, failing_allocation *failing) {
    weivel_c_allocator allocator = {allocate_unless_armed, release_plainly, failing};
    weivel_c_contexts_replace_allocator(in->host->contexts, &allocator);
    weivel_c_handles_replace_allocator(in->host->handles, &allocator);
}

static int has_its_entry(host_world *in, weivel_handle plugin) {
    weivel_c_handle_entry entry = WEIVEL_INIT(weivel_c_handle_entry);
    return weivel_c_handles_find(in->host->handles, plugin, &entry) == WEIVEL_C_STATUS_OK &&
           entry.kind == WEIVEL_C_HANDLE_PLUGIN;
}

/** Registers with the allocation `fail_at` failing: answers 1 once registered, checking what each answer leaves. */
static int register_with_a_failure(host_world *in, test_plugin *plugin, failing_allocation *failing,
                                   uint32_t fail_at) {
    uint32_t reached = registrations_reached;
    weivel_c_status status;
    arm_the_failure(failing, fail_at);
    lay_out_manifest(plugin);
    status = weivel_c_host_register(in->host, &plugin->manifest, &plugin->entry, &plugin->handle, NULL);
    CHECK(weivel_c_handles_reserved(in->host->handles) == 0);
    if (status == WEIVEL_C_STATUS_OK) {
        CHECK(has_its_entry(in, plugin->handle));
        return 1;
    }
    if (status != WEIVEL_C_STATUS_OUT_OF_MEMORY || registrations_reached != reached) {
        fprintf(stderr, "allocation %u failing: answered %u after %u registrations reached the core\n",
                (unsigned)fail_at, (unsigned)status, (unsigned)(registrations_reached - reached));
        CHECK(status == WEIVEL_C_STATUS_OUT_OF_MEMORY && registrations_reached == reached);
    }
    return 0;
}

/**
 * Fails each allocation of a registration in turn, over enough plugins that the handles table grows: an answer short
 * of memory has reached nothing of the core, and one that registered has the plugin's entry already made.
 */
static void a_registration_short_of_memory_reaches_nothing_and_one_that_registers_has_its_entry(void) {
    static test_plugin plugins[SEAM_PLUGINS];
    static char names[SEAM_PLUGINS][16];
    static failing_allocation failing;
    host_world in;
    uint32_t index;
    uint32_t attempt;
    register_manifest_of_the_core = counting_table.register_manifest;
    counting_table.register_manifest = count_the_registration;
    begin_host_world(&in);
    seam_every_allocation_of_a_registration(&in, &failing);
    for (index = 0; index < SEAM_PLUGINS; index++) {
        int registered = 0;
        snprintf(names[index], sizeof names[index], "seamed%u", (unsigned)index);
        name_acting_plugin(&plugins[index], &in, names[index], activate_and_answer_done, NULL);
        for (attempt = 1; attempt <= SEAM_ATTEMPTS && !registered; attempt++) {
            registered = register_with_a_failure(&in, &plugins[index], &failing, attempt);
        }
        if (!registered) {
            CHECK(register_with_a_failure(&in, &plugins[index], &failing, 0));
        }
    }
    arm_the_failure(&failing, 0);
    CHECK(sink_count(&in) == 0);
    end_host_world(&in);
    counting_table.register_manifest = register_manifest_of_the_core;
}

int main(int argument_count, char **arguments) {
    if (argument_count != 2 || !load_tables(arguments[1])) {
        fprintf(stderr, "usage: host_seams_test <core-library>\n");
        return 2;
    }
    a_destroy_made_while_an_activation_is_planned_waits_to_tell_its_released_outcome();
    a_registration_short_of_memory_reaches_nothing_and_one_that_registers_has_its_entry();
    CHECK(weivel_c_core_unload(&real_table) == WEIVEL_C_STATUS_OK);
    return check_summary("host seams");
}
