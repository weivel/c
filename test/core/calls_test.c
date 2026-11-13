/**
 * @file
 * Every call of the core, made once through the wrapper against the real core, and what each refuses to make.
 */

#include "fixture.h"

static const char *const PROVIDED_SERVICES[] = {"p:a"};
static const char *const PROVIDED_CAPABILITIES[] = {"cap.one"};

static weivel_manifest provider_manifest(weivel_service_offer *offer) {
    weivel_manifest manifest = WEIVEL_INIT(weivel_manifest);
    *offer = (weivel_service_offer)WEIVEL_INIT(weivel_service_offer);
    offer->id = PROVIDED_SERVICES[0];
    manifest.import_name = "p";
    manifest.api = 1;
    manifest.services.provides.items = offer;
    manifest.services.provides.count = 1;
    manifest.capabilities.provides.items = PROVIDED_CAPABILITIES;
    manifest.capabilities.provides.count = 1;
    return manifest;
}

static weivel_manifest consumer_manifest(weivel_service_want *want) {
    weivel_manifest manifest = WEIVEL_INIT(weivel_manifest);
    *want = want_of("p:a");
    manifest.import_name = "c";
    manifest.api = 1;
    manifest.services.consumes.items = want;
    manifest.services.consumes.count = 1;
    manifest.capabilities.consumes.items = PROVIDED_CAPABILITIES;
    manifest.capabilities.consumes.count = 1;
    return manifest;
}

static void answers_whether_a_manifest_is_supported(weivel_c_core *core, const weivel_manifest *manifest) {
    weivel_support support = WEIVEL_INIT(weivel_support);
    CHECK(weivel_c_core_supports(core, manifest, &support, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(support.supported == 1);
    CHECK(weivel_c_core_support_release(core, &support) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_support_release(core, NULL) == WEIVEL_C_STATUS_MISUSE);
}

static void plans_an_activation(weivel_c_core *core, weivel_handle provider, weivel_handle consumer) {
    weivel_handle plugins[2];
    weivel_activation_plan plan = WEIVEL_INIT(weivel_activation_plan);
    plugins[0] = consumer;
    plugins[1] = provider;
    CHECK(weivel_c_core_plan_activation(core, plugins, 2, WEIVEL_EDGES_SERVICES, NULL, &plan, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(plan.order_count == 2 && plan.order[0] == provider && plan.order[1] == consumer);
    CHECK(plan.edge_count == 1);
    CHECK(weivel_c_core_plan_activation(core, plugins, 2, WEIVEL_EDGES_SERVICES, NULL, &plan, NULL) ==
          WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_core_activation_plan_release(core, &plan) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_plan_activation(core, NULL, 2, WEIVEL_EDGES_SERVICES, NULL, &plan, NULL) ==
          WEIVEL_C_STATUS_MISUSE);
}

static void opens_a_plugin_with_its_payload(weivel_c_core *core, weivel_handle plugin, uint64_t token) {
    weivel_plugin_options options = WEIVEL_INIT(weivel_plugin_options);
    uint64_t payload = 0;
    options.payload = token;
    CHECK(weivel_c_core_open_plugin(core, plugin, &options, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_plugin_payload(core, plugin, &payload, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(payload == token);
    CHECK(weivel_c_core_plugin_payload(core, plugin, NULL, NULL) == WEIVEL_C_STATUS_MISUSE);
}

static void provides_and_consumes_a_capability(weivel_c_core *core, weivel_handle provider, weivel_handle consumer) {
    weivel_capability_list list = WEIVEL_INIT(weivel_capability_list);
    weivel_handle implementation = 0;
    weivel_handle watch = 0;
    uint32_t condition = 0;
    CHECK(weivel_c_core_watch_capability(core, consumer, "cap.one", &watch, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_handle_state(core, watch, &condition, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(condition == WEIVEL_HANDLE_WATCHING);
    CHECK(weivel_c_core_provide_capability(core, provider, "cap.one", &implementation, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_verify_activation(core, provider, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_all_capabilities(core, consumer, "cap.one", &list, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(list.count == 1 && list.items[0].implementation == implementation && list.items[0].provider == provider);
    CHECK(weivel_c_core_capability_list_release(core, &list) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_provide_capability(core, provider, "", &implementation, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_core_watch_capability(core, consumer, NULL, &watch, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_core_all_capabilities(core, consumer, "cap.one", NULL, NULL) == WEIVEL_C_STATUS_MISUSE);
}

static void registers_and_consumes_a_service(weivel_c_core *core, weivel_handle provider, weivel_handle consumer) {
    weivel_service_list list = WEIVEL_INIT(weivel_service_list);
    weivel_capacity capacity = WEIVEL_INIT(weivel_capacity);
    weivel_handle registration = 0;
    weivel_handle got = 0;
    weivel_handle want = 0;
    weivel_handle watch = 0;
    CHECK(weivel_c_core_get_service(core, consumer, "p:a", &got, NULL) == WEIVEL_C_STATUS_NONE);
    CHECK(weivel_c_core_watch_service(core, consumer, "p:a", &watch, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_register_service(core, provider, "p:a", &registration, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_get_service(core, consumer, "p:a", &got, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(got == registration);
    CHECK(weivel_c_core_want_service(core, consumer, "p:a", NULL, &want, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_service_ids(core, consumer, &list, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(list.ids.count == 1);
    CHECK(weivel_c_core_service_list_release(core, &list) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_unbound_services(core, consumer, &list, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(list.ids.count == 1);
    CHECK(weivel_c_core_service_list_release(core, &list) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_report_capacity(core, want, registration, &capacity, 0, NULL) == WEIVEL_C_STATUS_NONE);
    CHECK(weivel_c_core_report_capacity(core, want, registration, NULL, 0, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_core_unregister(core, watch, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_unregister(core, registration, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_register_service(core, provider, NULL, &registration, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_core_register_service(core, provider, "p:a", NULL, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_core_get_service(core, consumer, "", &got, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_core_want_service(core, consumer, "p:a", NULL, NULL, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_core_watch_service(core, consumer, "p:a", NULL, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_core_service_ids(core, consumer, NULL, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_core_unbound_services(core, consumer, NULL, NULL) == WEIVEL_C_STATUS_MISUSE);
}

static void reads_the_ledger(weivel_c_core *core, weivel_handle provider) {
    weivel_ledger_snapshot snapshot = WEIVEL_INIT(weivel_ledger_snapshot);
    weivel_ledger_row row = WEIVEL_INIT(weivel_ledger_row);
    CHECK(weivel_c_core_ledger(core, &snapshot, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(snapshot.row_count == 2);
    CHECK(weivel_c_core_ledger_snapshot_release(core, &snapshot) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_ledger_entry(core, provider, &row, NULL) == WEIVEL_C_STATUS_OK);
    check_text(row.import_name, "p", "the provider's row");
    CHECK(row.verified == 1);
    CHECK(weivel_c_core_ledger_row_release(core, &row) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_ledger(core, NULL, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_core_ledger_entry(core, provider, NULL, NULL) == WEIVEL_C_STATUS_MISUSE);
}

static void tells_the_time(weivel_c_core *core) {
    int64_t deadline = 7;
    CHECK(weivel_c_core_next_deadline(core, &deadline, NULL) == WEIVEL_C_STATUS_NONE);
    CHECK(weivel_c_core_next_deadline(core, NULL, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_core_advance(core, 1000, NULL) == WEIVEL_C_STATUS_OK);
}

static void ends_a_plugin(weivel_c_core *core, weivel_handle provider, weivel_handle consumer) {
    weivel_quarantine_reason reason = WEIVEL_INIT(weivel_quarantine_reason);
    weivel_c_refusal refusal = WEIVEL_INIT(weivel_c_refusal);
    reason.detail = "its activation failed";
    reason.fix = "fix the plugin";
    CHECK(weivel_c_core_quarantine_plugin(core, consumer, &reason, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_forget_plugin(core, provider, &refusal) == WEIVEL_C_STATUS_REFUSED);
    CHECK(refusal.code == WEIVEL_REFUSAL_PLUGIN_STATE);
    check_text(refusal.subject, "p", "the subject of a refused forget");
    CHECK(weivel_c_refusal_release(&refusal) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_release_plugin(core, provider, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_forget_plugin(core, provider, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_release_plugin(core, provider, &refusal) == WEIVEL_C_STATUS_REFUSED);
    CHECK(refusal.code == WEIVEL_REFUSAL_STALE_HANDLE);
    CHECK(weivel_c_refusal_release(&refusal) == WEIVEL_C_STATUS_OK);
}

static void refuses_a_call_it_was_handed_wrongly(weivel_c_core *core) {
    weivel_c_refusal small = WEIVEL_INIT(weivel_c_refusal);
    uint32_t condition = 0;
    small.size -= 1;
    forget_crossings();
    CHECK(weivel_c_core_handle_state(core, 1, &condition, &small) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_core_handle_state(core, 1, NULL, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_core_handle_state(NULL, 1, &condition, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_core_advance(NULL, 0, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(crossed.handle_state == 0 && crossed.advance == 0);
}

static void makes_every_call_of_the_core_through_the_wrapper(void) {
    telling sink = {0};
    weivel_service_offer offer;
    weivel_service_want want;
    weivel_manifest providing = provider_manifest(&offer);
    weivel_manifest consuming = consumer_manifest(&want);
    weivel_handle provider = 0;
    weivel_handle consumer = 0;
    weivel_c_core *core = make_core(NULL, NULL, &sink);
    answers_whether_a_manifest_is_supported(core, &providing);
    CHECK(weivel_c_core_register_manifest(core, &providing, &provider, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_register_manifest(core, &consuming, &consumer, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_register_manifest(core, NULL, &consumer, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_core_supports(core, &providing, NULL, NULL) == WEIVEL_C_STATUS_MISUSE);
    plans_an_activation(core, provider, consumer);
    opens_a_plugin_with_its_payload(core, provider, 41);
    opens_a_plugin_with_its_payload(core, consumer, 42);
    provides_and_consumes_a_capability(core, provider, consumer);
    registers_and_consumes_a_service(core, provider, consumer);
    reads_the_ledger(core, provider);
    tells_the_time(core);
    ends_a_plugin(core, provider, consumer);
    refuses_a_call_it_was_handed_wrongly(core);
    CHECK(sink.count == 0);
    end_core(core);
}

int main(int argument_count, char **arguments) {
    if (argument_count != 2 || !load_tables(arguments[1])) {
        fprintf(stderr, "usage: calls_test <core-library>\n");
        return 2;
    }
    makes_every_call_of_the_core_through_the_wrapper();
    CHECK(weivel_c_core_unload(&real_table) == WEIVEL_C_STATUS_OK);
    return check_summary("core calls");
}
