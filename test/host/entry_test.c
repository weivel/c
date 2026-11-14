/**
 * @file
 * A plugin's entry read from a shared library by the path and symbol name its caller gives, registered, activated
 * and released by the host, which closes the library once the plugin is forgotten.
 */

#include "entry_library.h"
#include "plugins.h"

static const char *library_path;
static const char *not_a_library_path;

/**
 * @implNote The test loads the library a second time for itself, so the host closing its own load once the plugin is
 * forgotten leaves the library mapped, and its state can still be read.
 */
static void an_entry_read_from_a_library_is_activated_and_deactivated_and_its_library_closed_by_the_host(void) {
    host_world in;
    test_plugin plugin;
    test_plugin *only[1];
    weivel_c_entry kept = WEIVEL_INIT(weivel_c_entry);
    entry_library_state *state;
    uint32_t kind = 0;
    begin_host_world(&in);
    name_test_plugin(&plugin, &in, "library");
    CHECK(weivel_c_entry_load(library_path, "test_plugin_entry", &plugin.entry, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(plugin.entry.activate != NULL && plugin.entry.library != NULL);
    CHECK(weivel_c_entry_load(library_path, "test_plugin_entry", &kept, NULL) == WEIVEL_C_STATUS_OK);
    state = kept.data;
    register_test_plugin(&plugin);
    only[0] = &plugin;
    CHECK(activate_in_order(&in, only, 1, WEIVEL_EDGES_CAPABILITIES));
    CHECK(outcomes_of(&in.told, plugin.handle, &kind) == 1 && kind == WEIVEL_C_OUTCOME_ACTIVE);
    CHECK(state->activations == 1 && state->context != NULL);
    CHECK(weivel_c_host_release(in.host, plugin.handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(state->deactivations == 1 && state_of(&in, &plugin) == 0);
    CHECK(sink_count(&in) == 0);
    end_host_world(&in);
    CHECK(weivel_c_entry_unload(&kept) == WEIVEL_C_STATUS_OK && kept.library == NULL && kept.activate == NULL);
}

static void a_library_that_is_none_lacks_the_symbol_or_cannot_provide_is_answered_as_a_value(void) {
    weivel_c_entry entry = WEIVEL_INIT(weivel_c_entry);
    weivel_c_entry_load_failure failure = WEIVEL_INIT(weivel_c_entry_load_failure);
    CHECK(weivel_c_entry_load(not_a_library_path, "test_plugin_entry", &entry, &failure) ==
          WEIVEL_C_STATUS_NOT_A_LIBRARY);
    CHECK(failure.missing_symbol == NULL && entry.library == NULL);
    CHECK(weivel_c_entry_load(library_path, "no_such_entry", &entry, &failure) == WEIVEL_C_STATUS_MISSING_SYMBOL);
    check_text(failure.missing_symbol, "no_such_entry", "the symbol a library lacks");
    CHECK(entry.library == NULL && entry.activate == NULL);
    CHECK(weivel_c_entry_load(library_path, "test_plugin_entry_that_cannot", &entry, &failure) ==
          WEIVEL_C_STATUS_FAILED);
    CHECK(failure.provided == WEIVEL_C_STATUS_OUT_OF_MEMORY && failure.missing_symbol == NULL);
    CHECK(entry.library == NULL);
    CHECK(weivel_c_entry_load(NULL, "test_plugin_entry", &entry, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_entry_load(library_path, "", &entry, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_entry_load(library_path, "test_plugin_entry", NULL, NULL) == WEIVEL_C_STATUS_MISUSE);
    entry.size = 4;
    CHECK(weivel_c_entry_load(library_path, "test_plugin_entry", &entry, NULL) == WEIVEL_C_STATUS_MISUSE);
    entry = (weivel_c_entry)WEIVEL_INIT(weivel_c_entry);
    CHECK(weivel_c_entry_unload(&entry) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_entry_unload(NULL) == WEIVEL_C_STATUS_MISUSE);
}

static void a_library_entry_never_opened_is_closed_by_the_destroy(void) {
    host_world in;
    test_plugin plugin;
    weivel_c_entry kept = WEIVEL_INIT(weivel_c_entry);
    entry_library_state *state;
    begin_host_world(&in);
    name_test_plugin(&plugin, &in, "library");
    CHECK(weivel_c_entry_load(library_path, "test_plugin_entry", &plugin.entry, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_entry_load(library_path, "test_plugin_entry", &kept, NULL) == WEIVEL_C_STATUS_OK);
    state = kept.data;
    state->activations = 0;
    state->deactivations = 0;
    register_test_plugin(&plugin);
    end_host_world(&in);
    CHECK(state->activations == 0 && state->deactivations == 0 && sink_count(&in) == 0);
    CHECK(weivel_c_entry_unload(&kept) == WEIVEL_C_STATUS_OK);
}

int main(int argument_count, char **arguments) {
    if (argument_count != 4 || !load_tables(arguments[1])) {
        fprintf(stderr, "usage: host_entry_test <core-library> <plugin-library> <a-file-that-is-no-library>\n");
        return 2;
    }
    library_path = arguments[2];
    not_a_library_path = arguments[3];
    an_entry_read_from_a_library_is_activated_and_deactivated_and_its_library_closed_by_the_host();
    a_library_that_is_none_lacks_the_symbol_or_cannot_provide_is_answered_as_a_value();
    a_library_entry_never_opened_is_closed_by_the_destroy();
    CHECK(weivel_c_core_unload(&real_table) == WEIVEL_C_STATUS_OK);
    return check_summary("host entry");
}
