/**
 * @file
 * When the host closes the library a plugin's entry was read from: at weivel_c_host_free and never before, so code of
 * the plugin's own that is still running once its release or the destroy has finished returns into a library still
 * open; and an entry that failed to load leaves no library open at all. Each close is counted by the testing build.
 *
 * @implNote The library is loaded once only, by the host, so a close made too early unmaps code the test then runs,
 * which is a crash rather than a quiet pass.
 */

#include "entry_library.h"
#include "libraries.h"

static const char *library_path;

static entry_library_state *load_the_plugin(host_world *in, test_plugin *plugin, uint32_t deactivate_waits) {
    entry_library_state *state;
    name_test_plugin(plugin, in, "library");
    CHECK(weivel_c_entry_load(library_path, "test_plugin_entry", &plugin->entry, NULL) == WEIVEL_C_STATUS_OK);
    state = plugin->entry.data;
    state->activations = 0;
    state->deactivations = 0;
    state->deactivate_waits = deactivate_waits;
    state->deactivation = NULL;
    state->finish = weivel_c_completion_finish;
    register_test_plugin(plugin);
    return state;
}

static void activate_the_plugin(host_world *in, test_plugin *plugin) {
    test_plugin *only[1];
    uint32_t kind = 0;
    only[0] = plugin;
    CHECK(activate_in_order(in, only, 1, WEIVEL_EDGES_CAPABILITIES));
    CHECK(outcomes_of(&in->told, plugin->handle, &kind) == 1 && kind == WEIVEL_C_OUTCOME_ACTIVE);
}

static void destroy_without_freeing(host_world *in) {
    uint32_t finished = 0;
    CHECK(weivel_c_host_destroy(in->host, &finished) == WEIVEL_C_STATUS_OK && finished == 1);
}

static void a_plugins_library_is_closed_by_the_free_and_not_by_its_release_or_the_destroy(void) {
    host_world in;
    test_plugin plugin;
    entry_library_state *state;
    uint32_t closes = weivel_c_testing_closes_made;
    begin_host_world(&in);
    state = load_the_plugin(&in, &plugin, 0);
    activate_the_plugin(&in, &plugin);
    CHECK(weivel_c_host_release(in.host, plugin.handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(state->deactivations == 1 && state_of(&in, &plugin) == 0);
    CHECK(weivel_c_testing_closes_made == closes);
    destroy_without_freeing(&in);
    CHECK(weivel_c_testing_closes_made == closes);
    CHECK(weivel_c_host_free(in.host) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_testing_closes_made == closes + 1 && sink_count(&in) == 0);
}

static void a_deactivation_finished_from_the_plugins_own_code_returns_into_a_library_still_open(void) {
    host_world in;
    test_plugin plugin;
    entry_library_state *state;
    uint32_t finished = 9;
    uint32_t closes = weivel_c_testing_closes_made;
    begin_host_world(&in);
    state = load_the_plugin(&in, &plugin, 1);
    activate_the_plugin(&in, &plugin);
    CHECK(weivel_c_host_release(in.host, plugin.handle, &finished) == WEIVEL_C_STATUS_OK && finished == 0);
    CHECK(state->deactivation != NULL);
    CHECK(state->finish_from_inside() == ENTRY_LIBRARY_SURVIVED);
    CHECK(state_of(&in, &plugin) == 0 && weivel_c_testing_closes_made == closes);
    end_host_world(&in);
    CHECK(weivel_c_testing_closes_made == closes + 1 && sink_count(&in) == 0);
}

static void a_destroy_finished_from_the_plugins_own_code_returns_into_a_library_still_open(void) {
    host_world in;
    test_plugin plugin;
    entry_library_state *state;
    uint32_t finished = 9;
    uint32_t closes = weivel_c_testing_closes_made;
    begin_host_world(&in);
    state = load_the_plugin(&in, &plugin, 1);
    activate_the_plugin(&in, &plugin);
    CHECK(weivel_c_host_destroy(in.host, &finished) == WEIVEL_C_STATUS_OK && finished == 0);
    CHECK(state->finish_from_inside() == ENTRY_LIBRARY_SURVIVED);
    CHECK(weivel_c_host_destroy(in.host, &finished) == WEIVEL_C_STATUS_OK && finished == 1);
    CHECK(weivel_c_testing_closes_made == closes);
    CHECK(weivel_c_host_free(in.host) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_testing_closes_made == closes + 1 && sink_count(&in) == 0);
}

static void an_entry_that_fails_to_load_leaves_no_library_open(void) {
    weivel_c_entry entry = WEIVEL_INIT(weivel_c_entry);
    uint32_t closes = weivel_c_testing_closes_made;
    CHECK(weivel_c_entry_load(library_path, "no_such_entry", &entry, NULL) == WEIVEL_C_STATUS_MISSING_SYMBOL);
    CHECK(weivel_c_testing_closes_made == closes + 1 && entry.library == NULL);
    CHECK(weivel_c_entry_load(library_path, "test_plugin_entry_that_cannot", &entry, NULL) ==
          WEIVEL_C_STATUS_FAILED);
    CHECK(weivel_c_testing_closes_made == closes + 2 && entry.library == NULL);
}

int main(int argument_count, char **arguments) {
    if (argument_count != 3 || !load_tables(arguments[1])) {
        fprintf(stderr, "usage: host_entry_closing_test <core-library> <plugin-library>\n");
        return 2;
    }
    library_path = arguments[2];
    a_plugins_library_is_closed_by_the_free_and_not_by_its_release_or_the_destroy();
    a_deactivation_finished_from_the_plugins_own_code_returns_into_a_library_still_open();
    a_destroy_finished_from_the_plugins_own_code_returns_into_a_library_still_open();
    an_entry_that_fails_to_load_leaves_no_library_open();
    CHECK(weivel_c_core_unload(&real_table) == WEIVEL_C_STATUS_OK);
    return check_summary("host entry closing");
}
