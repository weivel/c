/**
 * @file
 * Plugins laid out on disk as the standard lays them out, discovered by the standard and registered through the
 * module: each found plugin is registered with its manifest, the identity its identity file gives, and the entry read
 * from the file its manifest names by the symbol the caller names; a plugin whose entry is not there, or that names
 * none, opens nothing; the discovery may be released once its plugins are registered; and the identity the standard
 * takes apart compares as the standard orders identities; an entry loaded for a registration the host refuses is
 * closed again.
 *
 * @implNote MSVC calls the standard `fopen` unsafe unless told not to before any header is read, and a POSIX system
 * declares `mkdir` and `rmdir` under C11 only when asked for POSIX; macOS hides `RTLD_NOLOAD` once POSIX is asked for
 * unless Darwin's own names are asked for too.
 */

#define _CRT_SECURE_NO_WARNINGS
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE

#include <weivel/c/standard.h>
#include <weivel/identity.h>

#include "disk.h"
#include "host/entry_library.h"
#include "host/plugins.h"

#if defined(_WIN32)
#include <windows.h>

/** 1 while the process has the library at `path` loaded, as the platform's loader answers. */
static int library_is_loaded(const char *path) {
    char native[DISK_PATH];
    size_t at;
    snprintf(native, sizeof native, "%s", path);
    for (at = 0; native[at] != '\0'; at++) {
        native[at] = native[at] == '/' ? '\\' : native[at];
    }
    return GetModuleHandleA(native) != NULL;
}
#else
#include <dlfcn.h>

/** 1 while the process has the library at `path` loaded, as the platform's loader answers. */
static int library_is_loaded(const char *path) {
    void *handle = dlopen(path, RTLD_LAZY | RTLD_NOLOAD);
    if (handle == NULL) {
        return 0;
    }
    dlclose(handle);
    return 1;
}
#endif

static const char *entry_library_path;
static char directory[DISK_PATH];
static char entry_file[DISK_PATH];

static int lay_out_the_plugins(void) {
    char probe[DISK_PATH * 2];
    snprintf(probe, sizeof probe, "{\"importName\": \"probe\", \"api\": 1, \"language\": \"c\", \"entry\": \"%s\"}", entry_file);
    return disk_make_directory(directory) && disk_copy(entry_library_path, directory, entry_file) &&
           disk_write(directory, "probe.json", probe) &&
           disk_write(directory, "probe_identity.json", "{\"id\": \"forge:7/probe\", \"name\": \"owner/probe\"}") &&
           disk_write(directory, "bare.json", "{\"importName\": \"bare\", \"api\": 1, \"language\": \"c\"}") &&
           disk_write(directory, "gone.json", "{\"importName\": \"gone\", \"api\": 1, \"language\": \"c\", \"entry\": \"gone.so\"}");
}

static void remove_the_plugins(void) {
    CHECK(disk_remove(directory, "probe.json") && disk_remove(directory, "probe_identity.json"));
    CHECK(disk_remove(directory, "bare.json") && disk_remove(directory, "gone.json"));
    CHECK(disk_remove(directory, entry_file) && disk_remove_directory(directory));
}

static const weivel_standard_discovered_plugin *found_named(const weivel_standard_discovery *discovery,
                                                           const char *import_name) {
    size_t at;
    for (at = 0; at < discovery->plugin_count; at++) {
        const weivel_manifest *view = weivel_standard_manifest_view(discovery->plugins[at].manifest);
        if (view != NULL && strcmp(view->import_name, import_name) == 0) {
            return &discovery->plugins[at];
        }
    }
    return NULL;
}

static void the_found_identity_compares_as_the_standard_orders(const weivel_standard_discovered_plugin *probe) {
    weivel_standard_identity parsed = WEIVEL_INIT(weivel_standard_identity);
    weivel_standard_identity later = WEIVEL_INIT(weivel_standard_identity);
    CHECK(weivel_standard_identity_parse(probe->identity_id, strlen(probe->identity_id), &parsed, NULL) ==
          WEIVEL_STATUS_OK);
    CHECK(weivel_standard_identity_parse("forge:10", 8, &later, NULL) == WEIVEL_STATUS_OK);
    CHECK(weivel_standard_identity_compare(&parsed, &probe->identity) == 0);
    CHECK(weivel_standard_identity_compare(&parsed, &later) < 0);
    CHECK(weivel_standard_identity_compare(&later, &parsed) > 0);
    CHECK(parsed.kind == WEIVEL_STANDARD_IDENTITY_FORGE && parsed.number == 7);
    weivel_standard_identity_release(&parsed);
    weivel_standard_identity_release(&later);
}

static void what_opens_nothing(host_world *in, const weivel_standard_discovery *discovery) {
    weivel_handle plugin = 9;
    weivel_c_entry_load_failure failure = WEIVEL_INIT(weivel_c_entry_load_failure);
    weivel_standard_discovered_plugin through_a_link = *found_named(discovery, "probe");
    through_a_link.entry_present = 0;
    CHECK(weivel_c_standard_register_discovered(in->host, &through_a_link, "test_plugin_entry", &plugin, NULL, NULL) ==
          WEIVEL_C_STATUS_NOT_A_LIBRARY);
    CHECK(weivel_c_standard_register_discovered(in->host, found_named(discovery, "bare"), "test_plugin_entry", &plugin,
                                                NULL, NULL) == WEIVEL_C_STATUS_NOT_A_LIBRARY);
    CHECK(plugin == 0);
    CHECK(weivel_c_standard_register_discovered(in->host, found_named(discovery, "gone"), "test_plugin_entry", &plugin,
                                                NULL, NULL) == WEIVEL_C_STATUS_NOT_A_LIBRARY);
    CHECK(weivel_c_standard_register_discovered(in->host, found_named(discovery, "probe"), "no_such_entry", &plugin,
                                                &failure, NULL) == WEIVEL_C_STATUS_MISSING_SYMBOL);
    check_text(failure.missing_symbol, "no_such_entry", "the entry symbol the library lacks");
    CHECK(weivel_c_standard_register_discovered(in->host, NULL, "test_plugin_entry", &plugin, NULL, NULL) ==
          WEIVEL_C_STATUS_MISUSE);
}

/** Registers the probe the standard found, and releases the discovery before anything else reads it. */
static void register_the_probe(host_world *in, test_plugin *probe) {
    weivel_standard_discovery discovery = WEIVEL_INIT(weivel_standard_discovery);
    const weivel_standard_discovered_plugin *found;
    CHECK(weivel_standard_discover(directory, NULL, &discovery) == WEIVEL_STATUS_OK);
    CHECK(discovery.plugin_count == 3 && discovery.failure_count == 0);
    found = found_named(&discovery, "probe");
    CHECK(found != NULL && found->entry_present == 1);
    if (found != NULL) {
        the_found_identity_compares_as_the_standard_orders(found);
        CHECK(weivel_c_standard_register_discovered(in->host, found, "test_plugin_entry", &probe->handle, NULL,
                                                    NULL) == WEIVEL_C_STATUS_OK);
        what_opens_nothing(in, &discovery);
    }
    weivel_standard_discovery_release(&discovery);
}

static void a_discovered_plugin_is_registered_with_its_identity_and_the_entry_its_manifest_names(void) {
    host_world in;
    test_plugin probe;
    test_plugin *only[1];
    weivel_c_entry kept = WEIVEL_INIT(weivel_c_entry);
    entry_library_state *state;
    const weivel_manifest *seen = NULL;
    char copied[DISK_PATH];
    uint32_t kind = 0;
    begin_host_world(&in);
    name_test_plugin(&probe, &in, "probe");
    register_the_probe(&in, &probe);
    disk_join(copied, directory, entry_file);
    CHECK(weivel_c_entry_load(copied, "test_plugin_entry", &kept, NULL) == WEIVEL_C_STATUS_OK);
    state = kept.data;
    only[0] = &probe;
    CHECK(activate_in_order(&in, only, 1, WEIVEL_EDGES_CAPABILITIES));
    CHECK(outcomes_of(&in.told, probe.handle, &kind) == 1 && kind == WEIVEL_C_OUTCOME_ACTIVE);
    CHECK(state != NULL && state->activations == 1 && state->context != NULL);
    CHECK(state != NULL && weivel_c_context_manifest(state->context, &seen) == WEIVEL_C_STATUS_OK);
    check_text(seen == NULL ? NULL : seen->import_name, "probe", "the discovered import name");
    check_text(seen == NULL ? NULL : seen->identity, "forge:7/probe", "the discovered identity");
    CHECK(sink_count(&in) == 0);
    end_host_world(&in);
    CHECK(weivel_c_entry_unload(&kept) == WEIVEL_C_STATUS_OK);
}

static void a_discovered_manifest_is_filled_within_the_callers_size(void) {
    weivel_standard_discovery discovery = WEIVEL_INIT(weivel_standard_discovery);
    weivel_manifest manifest = WEIVEL_INIT(weivel_manifest);
    CHECK(weivel_standard_discover(directory, NULL, &discovery) == WEIVEL_STATUS_OK);
    CHECK(weivel_c_standard_discovered_manifest(found_named(&discovery, "bare"), &manifest) == WEIVEL_C_STATUS_OK);
    CHECK(manifest.size == sizeof manifest && manifest.identity == NULL && manifest.entry == NULL);
    check_text(manifest.import_name, "bare", "the bare plugin's import name");
    check_text(weivel_standard_manifest_language(found_named(&discovery, "bare")->manifest), "c",
               "the bare plugin's language");
    manifest.size = 4;
    CHECK(weivel_c_standard_discovered_manifest(found_named(&discovery, "bare"), &manifest) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_standard_discovered_manifest(NULL, &manifest) == WEIVEL_C_STATUS_MISUSE);
    weivel_standard_discovery_release(&discovery);
}

static void a_discovered_manifest_leaves_the_rest_of_a_larger_struct_zero(void) {
    weivel_standard_discovery discovery = WEIVEL_INIT(weivel_standard_discovery);
    union {
        weivel_manifest manifest;
        unsigned char bytes[sizeof(weivel_manifest) + 16];
    } larger;
    size_t at;
    uint32_t left_over = 0;
    memset(&larger, 0xAB, sizeof larger);
    larger.manifest.size = (uint32_t)sizeof larger;
    CHECK(weivel_standard_discover(directory, NULL, &discovery) == WEIVEL_STATUS_OK);
    CHECK(weivel_c_standard_discovered_manifest(found_named(&discovery, "bare"), &larger.manifest) ==
          WEIVEL_C_STATUS_OK);
    for (at = sizeof(weivel_manifest); at < sizeof larger; at++) {
        left_over += larger.bytes[at] != 0;
    }
    CHECK(larger.manifest.size == sizeof larger && left_over == 0 && larger.manifest.identity == NULL);
    check_text(larger.manifest.import_name, "bare", "the bare plugin's import name");
    weivel_standard_discovery_release(&discovery);
}

static void a_refused_registration_closes_the_entry_it_loaded(void) {
    host_world in;
    weivel_standard_discovery discovery = WEIVEL_INIT(weivel_standard_discovery);
    const weivel_standard_discovered_plugin *found;
    weivel_handle plugin = 9;
    begin_host_world(&in);
    CHECK(weivel_standard_discover(directory, NULL, &discovery) == WEIVEL_STATUS_OK);
    found = found_named(&discovery, "probe");
    CHECK(found != NULL && found->entry_path != NULL && !library_is_loaded(found->entry_path));
    CHECK(weivel_c_host_destroy(in.host, NULL) == WEIVEL_C_STATUS_OK);
    if (found != NULL && found->entry_path != NULL) {
        CHECK(weivel_c_standard_register_discovered(in.host, found, "test_plugin_entry", &plugin, NULL, NULL) ==
              WEIVEL_C_STATUS_DESTROYED);
        CHECK(plugin == 0 && !library_is_loaded(found->entry_path));
    }
    CHECK(weivel_c_host_free(in.host) == WEIVEL_C_STATUS_OK);
    weivel_standard_discovery_release(&discovery);
}

int main(int argument_count, char **arguments) {
    if (argument_count != 4 || !load_tables(arguments[1])) {
        fprintf(stderr, "usage: standard_discovery_test <core-library> <entry-library> <work-directory>\n");
        return 2;
    }
    entry_library_path = arguments[2];
    snprintf(directory, sizeof directory, "%s", arguments[3]);
    snprintf(entry_file, sizeof entry_file, "%s", disk_base_name(entry_library_path));
    CHECK(lay_out_the_plugins());
    a_discovered_plugin_is_registered_with_its_identity_and_the_entry_its_manifest_names();
    a_discovered_manifest_is_filled_within_the_callers_size();
    a_discovered_manifest_leaves_the_rest_of_a_larger_struct_zero();
    a_refused_registration_closes_the_entry_it_loaded();
    remove_the_plugins();
    CHECK(weivel_c_core_unload(&real_table) == WEIVEL_C_STATUS_OK);
    return check_summary("standard discovery");
}
