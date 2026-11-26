/**
 * @file
 * A JSON manifest read by the standard feeds the host's registration: the standard's manifest value is registered as
 * it is and released at once, since the host copies it; the module registers a document with the identity the caller
 * supplies, validating it first; a document the standard declines is answered as the standard's refusal with nothing
 * registered; and the standard's statuses translate into the layer's.
 */

#include <weivel/c/standard.h>

#include "host/plugins.h"

static const char PROBE[] = "{\"importName\": \"probe\", \"api\": 2, \"language\": \"c\", \"services\": {\"provides\": [\"probe:thing\"]}}";

/** Activates `plugin` alone and answers the manifest its context answers. */
static const weivel_manifest *manifest_seen_by(host_world *in, test_plugin *plugin) {
    test_plugin *only[1];
    const weivel_manifest *seen = NULL;
    uint32_t kind = 0;
    only[0] = plugin;
    CHECK(activate_in_order(in, only, 1, WEIVEL_EDGES_CAPABILITIES));
    CHECK(outcomes_of(&in->told, plugin->handle, &kind) == 1 && kind == WEIVEL_C_OUTCOME_ACTIVE);
    CHECK(plugin->context != NULL && weivel_c_context_manifest(plugin->context, &seen) == WEIVEL_C_STATUS_OK);
    return seen;
}

static void check_the_probe(const weivel_manifest *seen) {
    CHECK(seen != NULL);
    if (seen != NULL) {
        check_text(seen->import_name, "probe", "the registered import name");
        CHECK(seen->api == 2 && seen->services.provides.count == 1);
        check_text(seen->services.provides.items[0].id, "probe:thing", "the registered provided id");
    }
}

static void the_standards_manifest_value_is_registered_and_released_right_after(void) {
    host_world in;
    test_plugin plugin;
    weivel_standard_manifest *read = NULL;
    weivel_standard_validation validation = WEIVEL_INIT(weivel_standard_validation);
    begin_host_world(&in);
    name_acting_plugin(&plugin, &in, "probe", activate_and_answer_done, NULL);
    CHECK(weivel_standard_load_manifest(PROBE, sizeof PROBE - 1, NULL, &read, &validation) == WEIVEL_STATUS_OK);
    CHECK(weivel_c_host_register(in.host, weivel_standard_manifest_view(read), &plugin.entry, &plugin.handle, NULL) ==
          WEIVEL_C_STATUS_OK);
    weivel_standard_manifest_release(read);
    weivel_standard_validation_release(&validation);
    check_the_probe(manifest_seen_by(&in, &plugin));
    CHECK(sink_count(&in) == 0);
    end_host_world(&in);
}

static void a_document_is_validated_and_registered_with_the_identity_the_caller_supplies(void) {
    host_world in;
    test_plugin plugin;
    weivel_c_standard_manifest_document document = WEIVEL_INIT(weivel_c_standard_manifest_document);
    char json[sizeof PROBE + 8];
    char identity[] = "forge:7/probe";
    const weivel_manifest *seen;
    begin_host_world(&in);
    name_acting_plugin(&plugin, &in, "probe", activate_and_answer_done, NULL);
    memcpy(json, PROBE, sizeof PROBE - 1);
    memcpy(json + sizeof PROBE - 1, "trailing", 8);
    document.json = json;
    document.length = sizeof PROBE - 1;
    document.identity = identity;
    CHECK(weivel_c_standard_register_manifest(in.host, &document, &plugin.entry, &plugin.handle, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    memset(json, 'x', sizeof json);
    identity[0] = 'X';
    seen = manifest_seen_by(&in, &plugin);
    check_the_probe(seen);
    check_text(seen == NULL ? NULL : seen->identity, "forge:7/probe", "the identity the plugin is registered with");
    end_host_world(&in);
}

static void refused(host_world *in, const char *json, const char *path) {
    weivel_c_standard_manifest_document document = WEIVEL_INIT(weivel_c_standard_manifest_document);
    weivel_standard_validation validation = WEIVEL_INIT(weivel_standard_validation);
    weivel_c_entry entry = WEIVEL_INIT(weivel_c_entry);
    weivel_handle plugin = 7;
    entry.activate = activate_and_answer_done;
    document.json = json;
    document.length = strlen(json);
    CHECK(weivel_c_standard_register_manifest(in->host, &document, &entry, &plugin, &validation, NULL) ==
          WEIVEL_C_STATUS_REFUSED_BY_THE_STANDARD);
    CHECK(plugin == 0 && validation.valid == 0 && validation.refusal.code != WEIVEL_STANDARD_REFUSAL_NONE);
    check_text(validation.refusal.path, path, "the field the standard refused");
    weivel_standard_validation_release(&validation);
    plugin = 7;
    CHECK(weivel_c_standard_register_manifest(in->host, &document, &entry, &plugin, NULL, NULL) ==
          WEIVEL_C_STATUS_REFUSED_BY_THE_STANDARD);
    CHECK(plugin == 0);
}

static void a_document_the_standard_declines_is_its_refusal_with_nothing_registered(void) {
    host_world in;
    weivel_c_standard_manifest_document document = WEIVEL_INIT(weivel_c_standard_manifest_document);
    weivel_c_entry entry = WEIVEL_INIT(weivel_c_entry);
    weivel_handle plugin = 0;
    begin_host_world(&in);
    refused(&in, "{\"importName\": \"Probe!\", \"api\": 1, \"language\": \"c\"}", "importName");
    refused(&in, "{\"importName\": \"probe\", \"language\": \"c\"}", "api");
    refused(&in, "{\"importName\": \"probe\", \"api\": 1}", "language");
    refused(&in, "not a manifest", "");
    entry.activate = activate_and_answer_done;
    CHECK(weivel_c_standard_register_manifest(in.host, NULL, &entry, &plugin, NULL, NULL) == WEIVEL_C_STATUS_MISUSE);
    document.size = 4;
    document.json = PROBE;
    CHECK(weivel_c_standard_register_manifest(in.host, &document, &entry, &plugin, NULL, NULL) ==
          WEIVEL_C_STATUS_MISUSE);
    CHECK(sink_count(&in) == 0);
    end_host_world(&in);
}

static void the_standards_statuses_translate_into_the_layers(void) {
    CHECK(weivel_c_standard_status(WEIVEL_STATUS_OK) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_standard_status(WEIVEL_STATUS_NONE) == WEIVEL_C_STATUS_NONE);
    CHECK(weivel_c_standard_status(WEIVEL_STATUS_REFUSED) == WEIVEL_C_STATUS_REFUSED_BY_THE_STANDARD);
    CHECK(weivel_c_standard_status(WEIVEL_STATUS_MALFORMED) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_standard_status(WEIVEL_STATUS_OUT_OF_MEMORY) == WEIVEL_C_STATUS_OUT_OF_MEMORY);
    CHECK(weivel_c_standard_status(9999) == WEIVEL_C_STATUS_FAILED);
}

int main(int argument_count, char **arguments) {
    if (argument_count != 2 || !load_tables(arguments[1])) {
        fprintf(stderr, "usage: standard_manifest_test <core-library>\n");
        return 2;
    }
    the_standards_manifest_value_is_registered_and_released_right_after();
    a_document_is_validated_and_registered_with_the_identity_the_caller_supplies();
    a_document_the_standard_declines_is_its_refusal_with_nothing_registered();
    the_standards_statuses_translate_into_the_layers();
    CHECK(weivel_c_core_unload(&real_table) == WEIVEL_C_STATUS_OK);
    return check_summary("standard manifest");
}
