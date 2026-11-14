/**
 * @file
 * The table from the core's handles to the layer's objects, each rule of the contract's `handles` section against
 * handles the real core answers.
 */

#include "core/fixture.h"
#include "threads.h"

#if !defined(WEIVEL_C_USING_SHARED)
#include "../../src/core/calls.h"
#include "../../src/handles/table.h"
#endif

static const char *const PROVIDED[] = {"p:a"};

/** Objects of the layer's that the entries stand for; only their addresses matter. */
static int plugin_record;
static int other_record;
static int service_object;
static int watch_object;
static int want_object;

static weivel_c_handles *make_table(void) {
    weivel_c_handles *table = NULL;
    CHECK(weivel_c_handles_create(&table) == WEIVEL_C_STATUS_OK);
    return table;
}

static weivel_c_handle_entry entry_of(uint32_t kind, weivel_handle handle, weivel_handle owner, void *object) {
    weivel_c_handle_entry entry = WEIVEL_INIT(weivel_c_handle_entry);
    entry.kind = kind;
    entry.handle = handle;
    entry.owner = owner;
    entry.object = object;
    return entry;
}

static weivel_c_status add(weivel_c_handles *table, uint32_t kind, weivel_handle handle, weivel_handle owner,
                           void *object) {
    weivel_c_handle_entry entry = entry_of(kind, handle, owner, object);
    return weivel_c_handles_add(table, &entry, NULL);
}

static int holds(weivel_c_handles *table, weivel_handle handle, uint32_t kind, weivel_handle owner, void *object) {
    weivel_c_handle_entry found = WEIVEL_INIT(weivel_c_handle_entry);
    return weivel_c_handles_find(table, handle, &found) == WEIVEL_C_STATUS_OK && found.kind == kind &&
           found.handle == handle && found.owner == owner && found.object == object && found.token != 0;
}

static int answers_nothing(weivel_c_handles *table, weivel_handle handle) {
    weivel_c_handle_entry found = entry_of(77, 77, 77, &other_record);
    return weivel_c_handles_find(table, handle, &found) == WEIVEL_C_STATUS_NONE && found.kind == 77 &&
           found.object == &other_record;
}

/** 1 when the core refuses to end `handle`, which it does for a registration, watch or want it no longer names. */
static int is_ended_in_the_core(weivel_c_core *core, weivel_handle handle) {
    return weivel_c_core_unregister(core, handle, NULL) == WEIVEL_C_STATUS_REFUSED;
}

static int is_a_plugin_the_core_names(weivel_c_core *core, weivel_handle plugin) {
    uint64_t payload = 0;
    return weivel_c_core_plugin_payload(core, plugin, &payload, NULL) == WEIVEL_C_STATUS_OK;
}

static uint32_t entries_in(weivel_c_handles *table) {
    uint32_t entries = 99;
    CHECK(weivel_c_handles_count(table, &entries, NULL) == WEIVEL_C_STATUS_OK);
    return entries;
}

static uint32_t holdings_in(weivel_c_handles *table) {
    uint32_t holdings = 99;
    CHECK(weivel_c_handles_count(table, NULL, &holdings) == WEIVEL_C_STATUS_OK);
    return holdings;
}

static void an_entry_is_made_when_the_core_answers_the_handle(void) {
    weivel_c_handles *table = make_table();
    weivel_c_core *core = make_core(NULL, NULL, NULL);
    weivel_handle plugin = open_plugin(core, "p", PROVIDED, 1, NULL, 0);
    weivel_handle registration = 0;
    CHECK(answers_nothing(table, plugin));
    CHECK(add(table, WEIVEL_C_HANDLE_PLUGIN, plugin, 0, &plugin_record) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_register_service(core, plugin, "p:a", &registration, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(add(table, WEIVEL_C_HANDLE_SERVICE_REGISTRATION, registration, plugin, &service_object) ==
          WEIVEL_C_STATUS_OK);
    CHECK(holds(table, plugin, WEIVEL_C_HANDLE_PLUGIN, 0, &plugin_record));
    CHECK(holds(table, registration, WEIVEL_C_HANDLE_SERVICE_REGISTRATION, plugin, &service_object));
    CHECK(entries_in(table) == 2 && holdings_in(table) == 0);
    end_core(core);
    CHECK(weivel_c_handles_free(table) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_handles_free(NULL) == WEIVEL_C_STATUS_OK);
}

/** Ends an entry as its owner does once the core stopped naming the handle, and checks both agree. */
static void check_it_ended(weivel_c_core *core, weivel_c_handles *table, weivel_handle handle, void *object) {
    weivel_c_handle_entry ended = WEIVEL_INIT(weivel_c_handle_entry);
    CHECK(is_ended_in_the_core(core, handle));
    CHECK(weivel_c_handles_end(table, handle, &ended) == WEIVEL_C_STATUS_OK);
    CHECK(ended.handle == handle && ended.object == object);
    CHECK(answers_nothing(table, handle));
    CHECK(weivel_c_handles_end(table, handle, NULL) == WEIVEL_C_STATUS_NONE);
}

static void an_entry_ends_when_what_its_handle_stands_for_ends(void) {
    static const char *const CAPABILITIES[] = {"cap.one"};
    weivel_c_handles *table = make_table();
    weivel_c_core *core = make_core(NULL, NULL, NULL);
    weivel_manifest manifest = WEIVEL_INIT(weivel_manifest);
    weivel_service_offer offer = WEIVEL_INIT(weivel_service_offer);
    weivel_handle plugin = 0;
    weivel_handle registration = 0;
    weivel_handle implementation = 0;
    weivel_handle watch = 0;
    weivel_handle want = 0;
    offer.id = "p:a";
    manifest.import_name = "p";
    manifest.api = 1;
    manifest.services.provides.items = &offer;
    manifest.services.provides.count = 1;
    manifest.capabilities.provides.items = CAPABILITIES;
    manifest.capabilities.provides.count = 1;
    CHECK(weivel_c_core_register_manifest(core, &manifest, &plugin, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_open_plugin(core, plugin, NULL, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(add(table, WEIVEL_C_HANDLE_PLUGIN, plugin, 0, &plugin_record) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_register_service(core, plugin, "p:a", &registration, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(add(table, WEIVEL_C_HANDLE_SERVICE_REGISTRATION, registration, plugin, &service_object) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_unregister(core, registration, NULL) == WEIVEL_C_STATUS_OK);
    check_it_ended(core, table, registration, &service_object);
    CHECK(weivel_c_core_watch_service(core, WEIVEL_HOST, "p:a", &watch, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(add(table, WEIVEL_C_HANDLE_WATCH, watch, WEIVEL_HOST, &watch_object) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_unregister(core, watch, NULL) == WEIVEL_C_STATUS_OK);
    check_it_ended(core, table, watch, &watch_object);
    CHECK(weivel_c_core_want_service(core, WEIVEL_HOST, "p:a", NULL, &want, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(add(table, WEIVEL_C_HANDLE_WANT, want, WEIVEL_HOST, &want_object) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_unregister(core, want, NULL) == WEIVEL_C_STATUS_OK);
    check_it_ended(core, table, want, &want_object);
    CHECK(weivel_c_core_provide_capability(core, plugin, "cap.one", &implementation, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(add(table, WEIVEL_C_HANDLE_CAPABILITY_IMPLEMENTATION, implementation, plugin, &service_object) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_release_plugin(core, plugin, NULL) == WEIVEL_C_STATUS_OK);
    check_it_ended(core, table, implementation, &service_object);
    CHECK(is_a_plugin_the_core_names(core, plugin));
    CHECK(weivel_c_core_forget_plugin(core, plugin, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(!is_a_plugin_the_core_names(core, plugin));
    check_it_ended(core, table, plugin, &plugin_record);
    CHECK(entries_in(table) == 0);
    end_core(core);
    weivel_c_handles_free(table);
}

static weivel_c_status end_a_want_that_settled(hearing *self, weivel_c_core *core, const weivel_c_event *event) {
    weivel_c_handles *table = self->more;
    (void)core;
    if (event->kind == WEIVEL_EVENT_WANT_SETTLED) {
        CHECK(weivel_c_handles_end(table, event->data.want.want, NULL) == WEIVEL_C_STATUS_OK);
    }
    return WEIVEL_C_STATUS_OK;
}

static void a_want_that_settles_ends_its_entry_when_the_settlement_is_heard(void) {
    hearing listener = {0};
    weivel_c_handles *table = make_table();
    weivel_c_core *core = make_core(NULL, &listener, NULL);
    weivel_handle want = 0;
    weivel_handle registration = 0;
    listener.react = end_a_want_that_settled;
    listener.more = table;
    CHECK(weivel_c_core_want_service(core, WEIVEL_HOST, "p:a", NULL, &want, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(add(table, WEIVEL_C_HANDLE_WANT, want, WEIVEL_HOST, &want_object) == WEIVEL_C_STATUS_OK);
    CHECK(holds(table, want, WEIVEL_C_HANDLE_WANT, WEIVEL_HOST, &want_object));
    CHECK(weivel_c_core_register_service(core, WEIVEL_HOST, "p:a", &registration, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(count_heard(&listener, WEIVEL_EVENT_WANT_SETTLED) == 1);
    CHECK(answers_nothing(table, want));
    end_core(core);
    weivel_c_handles_free(table);
}

static void a_handle_the_table_does_not_hold_answers_nothing_and_is_never_matched_to_a_newer_object(void) {
    const weivel_handle older = 0x0000000100000005u;
    const weivel_handle newer = 0x0000000200000005u;
    weivel_c_handles *table = make_table();
    weivel_c_core *core = make_core(NULL, NULL, NULL);
    weivel_handle first = open_plugin(core, "p", NULL, 0, NULL, 0);
    weivel_handle second;
    uint64_t first_token = 0;
    uint64_t second_token = 0;
    weivel_c_handle_entry entry = entry_of(WEIVEL_C_HANDLE_PLUGIN, first, 0, &plugin_record);
    weivel_c_handle_entry found = WEIVEL_INIT(weivel_c_handle_entry);
    CHECK(answers_nothing(table, 0x123456789u));
    CHECK(weivel_c_handles_add(table, &entry, &first_token) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_release_plugin(core, first, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_forget_plugin(core, first, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_handles_end(table, first, NULL) == WEIVEL_C_STATUS_OK);
    second = open_plugin(core, "p", NULL, 0, NULL, 0);
    entry = entry_of(WEIVEL_C_HANDLE_PLUGIN, second, 0, &other_record);
    CHECK(weivel_c_handles_add(table, &entry, &second_token) == WEIVEL_C_STATUS_OK);
    CHECK(first != second && first_token != second_token);
    CHECK(answers_nothing(table, first));
    CHECK(holds(table, second, WEIVEL_C_HANDLE_PLUGIN, 0, &other_record));
    CHECK(weivel_c_handles_find_token(table, first_token, &found) == WEIVEL_C_STATUS_NONE);
    CHECK(weivel_c_handles_find_token(table, second_token, &found) == WEIVEL_C_STATUS_OK);
    CHECK(found.object == &other_record && found.handle == second);
    CHECK(add(table, WEIVEL_C_HANDLE_WATCH, older, 0, &watch_object) == WEIVEL_C_STATUS_OK);
    CHECK(answers_nothing(table, newer));
    CHECK(weivel_c_handles_end(table, older, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(add(table, WEIVEL_C_HANDLE_WATCH, newer, 0, &want_object) == WEIVEL_C_STATUS_OK);
    CHECK(answers_nothing(table, older));
    CHECK(holds(table, newer, WEIVEL_C_HANDLE_WATCH, 0, &want_object));
    end_core(core);
    weivel_c_handles_free(table);
}

/** What a listener that applies each revocation to the table saw. */
typedef struct revocations {
    weivel_c_handles *table;
    weivel_handle plugin;
    uint32_t dropped;
    uint32_t unknown;
    void *last_object;
    uint32_t crossings_made_by_the_table;
} revocations;

static weivel_c_status apply_revocations(hearing *self, weivel_c_core *core, const weivel_c_event *event) {
    revocations *more = self->more;
    void *object = NULL;
    uint32_t before = crossed.handle_state + crossed.next_event + crossed.advance;
    weivel_c_status status = weivel_c_handles_revoke(more->table, event, &object);
    (void)core;
    more->crossings_made_by_the_table += crossed.handle_state + crossed.next_event + crossed.advance - before;
    if (event->kind != WEIVEL_EVENT_REVOKED) {
        CHECK(status == WEIVEL_C_STATUS_NONE);
        return WEIVEL_C_STATUS_OK;
    }
    if (status == WEIVEL_C_STATUS_OK) {
        CHECK(event->data.revoked.plugin == more->plugin);
        more->dropped++;
        more->last_object = object;
    } else {
        more->unknown++;
    }
    return WEIVEL_C_STATUS_OK;
}

static void a_revocation_naming_a_forgotten_plugin_drops_that_plugins_holding(void) {
    hearing listener = {0};
    revocations more = {0};
    weivel_service_want consumed = want_of("p:a");
    weivel_c_handles *table = make_table();
    weivel_c_core *core = make_core(NULL, &listener, NULL);
    weivel_handle provider = open_plugin(core, "p", PROVIDED, 1, NULL, 0);
    weivel_handle consumer = open_plugin(core, "c", NULL, 0, &consumed, 1);
    weivel_handle registration = 0;
    weivel_handle got = 0;
    void *held = NULL;
    more.table = table;
    more.plugin = consumer;
    listener.react = apply_revocations;
    listener.more = &more;
    CHECK(weivel_c_core_register_service(core, provider, "p:a", &registration, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_get_service(core, consumer, "p:a", &got, NULL) == WEIVEL_C_STATUS_OK && got == registration);
    CHECK(weivel_c_handles_hold(table, consumer, got, &service_object) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_handles_held(table, consumer, got, &held) == WEIVEL_C_STATUS_OK && held == &service_object);
    CHECK(weivel_c_handles_held(table, provider, got, &held) == WEIVEL_C_STATUS_NONE);
    CHECK(weivel_c_core_release_plugin(core, consumer, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(more.dropped == 1 && holdings_in(table) == 0);
    CHECK(weivel_c_core_get_service(core, consumer, "p:a", &got, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_handles_hold(table, consumer, got, &other_record) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_handles_hold(table, provider, got, &watch_object) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_forget_plugin(core, consumer, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(!is_a_plugin_the_core_names(core, consumer));
    CHECK(more.dropped == 2 && more.unknown == 0 && more.last_object == &other_record);
    CHECK(weivel_c_handles_held(table, consumer, got, &held) == WEIVEL_C_STATUS_NONE);
    CHECK(weivel_c_handles_held(table, provider, got, &held) == WEIVEL_C_STATUS_OK && held == &watch_object);
    CHECK(holdings_in(table) == 1);
    CHECK(more.crossings_made_by_the_table == 0);
    end_core(core);
    weivel_c_handles_free(table);
}

static void the_payload_a_plugin_was_opened_with_reaches_its_record_without_a_search(void) {
    weivel_c_handles *table = make_table();
    weivel_c_core *core = make_core(NULL, NULL, NULL);
    weivel_manifest manifest = WEIVEL_INIT(weivel_manifest);
    weivel_plugin_options options = WEIVEL_INIT(weivel_plugin_options);
    weivel_c_handle_entry entry;
    weivel_c_handle_entry found = WEIVEL_INIT(weivel_c_handle_entry);
    weivel_handle plugin = 0;
    uint64_t token = 0;
    uint64_t other_token = 0;
    uint64_t payload = 0;
    manifest.import_name = "p";
    manifest.api = 1;
    CHECK(weivel_c_core_register_manifest(core, &manifest, &plugin, NULL) == WEIVEL_C_STATUS_OK);
    entry = entry_of(WEIVEL_C_HANDLE_PLUGIN, plugin + 1, 0, &other_record);
    CHECK(weivel_c_handles_add(table, &entry, &other_token) == WEIVEL_C_STATUS_OK);
    entry = entry_of(WEIVEL_C_HANDLE_PLUGIN, plugin, 0, &plugin_record);
    CHECK(weivel_c_handles_add(table, &entry, &token) == WEIVEL_C_STATUS_OK);
    CHECK(token != 0 && other_token != 0 && token != other_token);
    options.payload = token;
    CHECK(weivel_c_core_open_plugin(core, plugin, &options, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_plugin_payload(core, plugin, &payload, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_handles_find_token(table, payload, &found) == WEIVEL_C_STATUS_OK);
    CHECK(found.object == &plugin_record && found.handle == plugin && found.kind == WEIVEL_C_HANDLE_PLUGIN);
    CHECK(found.token == token);
    CHECK(weivel_c_handles_find_token(table, 0, &found) == WEIVEL_C_STATUS_NONE);
    CHECK(weivel_c_handles_find_token(table, token + 1, &found) == WEIVEL_C_STATUS_NONE);
    CHECK(weivel_c_handles_find_token(table, token + (1ull << 32), &found) == WEIVEL_C_STATUS_NONE);
    CHECK(weivel_c_handles_end(table, plugin, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_handles_find_token(table, payload, &found) == WEIVEL_C_STATUS_NONE);
    end_core(core);
    weivel_c_handles_free(table);
}

static void the_entries_of_one_owner_are_ended_one_by_one(void) {
    weivel_c_handles *table = make_table();
    weivel_c_handle_entry ended = WEIVEL_INIT(weivel_c_handle_entry);
    const weivel_handle plugin = 11;
    const weivel_handle other = 12;
    uint32_t seen = 0;
    CHECK(add(table, WEIVEL_C_HANDLE_PLUGIN, plugin, 0, &plugin_record) == WEIVEL_C_STATUS_OK);
    CHECK(add(table, WEIVEL_C_HANDLE_SERVICE_REGISTRATION, 21, plugin, &service_object) == WEIVEL_C_STATUS_OK);
    CHECK(add(table, WEIVEL_C_HANDLE_WATCH, 22, plugin, &watch_object) == WEIVEL_C_STATUS_OK);
    CHECK(add(table, WEIVEL_C_HANDLE_WANT, 23, plugin, &want_object) == WEIVEL_C_STATUS_OK);
    CHECK(add(table, WEIVEL_C_HANDLE_WATCH, 31, other, &other_record) == WEIVEL_C_STATUS_OK);
    CHECK(add(table, WEIVEL_C_HANDLE_WATCH, 41, WEIVEL_HOST, &other_record) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_handles_end(table, 22, NULL) == WEIVEL_C_STATUS_OK);
    while (weivel_c_handles_end_owned(table, plugin, &ended) == WEIVEL_C_STATUS_OK) {
        CHECK(ended.owner == plugin && (ended.handle == 21 || ended.handle == 23));
        seen |= ended.handle == 21 ? 1u : 2u;
        CHECK(answers_nothing(table, ended.handle));
    }
    CHECK(seen == 3);
    CHECK(holds(table, plugin, WEIVEL_C_HANDLE_PLUGIN, 0, &plugin_record));
    CHECK(holds(table, 31, WEIVEL_C_HANDLE_WATCH, other, &other_record));
    CHECK(weivel_c_handles_end_owned(table, WEIVEL_HOST, &ended) == WEIVEL_C_STATUS_OK && ended.handle == 41);
    CHECK(weivel_c_handles_end_owned(table, WEIVEL_HOST, NULL) == WEIVEL_C_STATUS_NONE);
    CHECK(weivel_c_handles_end_owned(table, other, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_handles_end_owned(table, 0, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(entries_in(table) == 1);
    weivel_c_handles_free(table);
}

static void a_misused_call_changes_nothing(void) {
    weivel_c_handles *table = make_table();
    weivel_c_handle_entry entry = entry_of(WEIVEL_C_HANDLE_WATCH, 5, 0, &watch_object);
    weivel_c_handle_entry found = WEIVEL_INIT(weivel_c_handle_entry);
    weivel_c_event event;
    void *held = NULL;
    memset(&event, 0, sizeof event);
    CHECK(weivel_c_handles_create(NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_handles_add(NULL, &entry, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_handles_add(table, NULL, NULL) == WEIVEL_C_STATUS_MISUSE);
    entry.size -= 1;
    CHECK(weivel_c_handles_add(table, &entry, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(add(table, WEIVEL_C_HANDLE_WATCH, 0, 0, &watch_object) == WEIVEL_C_STATUS_MISUSE);
    CHECK(add(table, WEIVEL_C_HANDLE_WATCH, 5, 0, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(add(table, 0, 5, 0, &watch_object) == WEIVEL_C_STATUS_MISUSE);
    CHECK(add(table, WEIVEL_C_HANDLE_WANT + 1, 5, 0, &watch_object) == WEIVEL_C_STATUS_MISUSE);
    CHECK(entries_in(table) == 0);
    CHECK(add(table, WEIVEL_C_HANDLE_WATCH, 5, 0, &watch_object) == WEIVEL_C_STATUS_OK);
    CHECK(add(table, WEIVEL_C_HANDLE_WANT, 5, 0, &want_object) == WEIVEL_C_STATUS_MISUSE);
    CHECK(holds(table, 5, WEIVEL_C_HANDLE_WATCH, 0, &watch_object));
    CHECK(weivel_c_handles_find(table, 5, NULL) == WEIVEL_C_STATUS_MISUSE);
    found.size -= 1;
    CHECK(weivel_c_handles_find(table, 5, &found) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_handles_find_token(table, 1, &found) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_handles_end(table, 5, &found) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_handles_end(NULL, 5, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_handles_hold(table, 0, 5, &watch_object) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_handles_hold(table, 5, 0, &watch_object) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_handles_hold(table, 5, 6, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_handles_held(table, 5, 6, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_handles_revoke(table, NULL, &held) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_handles_revoke(NULL, &event, &held) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_handles_count(NULL, NULL, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(entries_in(table) == 1 && holdings_in(table) == 0);
    weivel_c_handles_free(table);
}

#define MANY 20000

static void many_entries_are_each_found_while_others_come_and_go(void) {
    static int objects[MANY];
    weivel_c_handles *table = make_table();
    uint32_t wrong = 0;
    uint32_t index;
    for (index = 0; index < MANY; index++) {
        wrong += add(table, WEIVEL_C_HANDLE_WATCH, 1000 + index, 1 + index % 7, &objects[index]) !=
                 WEIVEL_C_STATUS_OK;
        wrong += weivel_c_handles_hold(table, 1 + index % 7, 1000 + index, &objects[index]) != WEIVEL_C_STATUS_OK;
    }
    for (index = 0; index < MANY; index += 2) {
        wrong += weivel_c_handles_end(table, 1000 + index, NULL) != WEIVEL_C_STATUS_OK;
    }
    for (index = 0; index < MANY; index++) {
        void *held = NULL;
        wrong += index % 2 == 0 ? !answers_nothing(table, 1000 + index)
                                : !holds(table, 1000 + index, WEIVEL_C_HANDLE_WATCH, 1 + index % 7, &objects[index]);
        wrong += weivel_c_handles_held(table, 1 + index % 7, 1000 + index, &held) != WEIVEL_C_STATUS_OK ||
                 held != &objects[index];
    }
    CHECK(wrong == 0);
    CHECK(entries_in(table) == MANY / 2 && holdings_in(table) == MANY);
    for (index = 1; index <= 7; index++) {
        while (weivel_c_handles_end_owned(table, index, NULL) == WEIVEL_C_STATUS_OK) {
        }
    }
    CHECK(entries_in(table) == 0);
    weivel_c_handles_free(table);
}

#define TABLE_THREADS 4
#define TABLE_ROUNDS 3000

typedef struct table_worker {
    weivel_c_handles *table;
    uint64_t base;
    uint32_t failures;
} table_worker;

static void churn_the_table(void *argument) {
    table_worker *self = argument;
    weivel_c_event revoked;
    uint32_t round;
    memset(&revoked, 0, sizeof revoked);
    revoked.kind = WEIVEL_EVENT_REVOKED;
    for (round = 0; round < TABLE_ROUNDS; round++) {
        weivel_handle handle = self->base + round;
        self->failures += add(self->table, WEIVEL_C_HANDLE_WANT, handle, self->base, self) != WEIVEL_C_STATUS_OK;
        self->failures += weivel_c_handles_hold(self->table, self->base, handle, self) != WEIVEL_C_STATUS_OK;
        self->failures += !holds(self->table, handle, WEIVEL_C_HANDLE_WANT, self->base, self);
        self->failures += !answers_nothing(self->table, handle + 0x100000000u);
        if (round % 3 != 0) {
            revoked.data.revoked.plugin = self->base;
            revoked.data.revoked.implementation = handle;
            self->failures += weivel_c_handles_end(self->table, handle, NULL) != WEIVEL_C_STATUS_OK;
            self->failures += weivel_c_handles_revoke(self->table, &revoked, NULL) != WEIVEL_C_STATUS_OK;
        }
    }
}

static void calls_from_several_threads_leave_the_table_whole(void) {
    weivel_c_handles *table = make_table();
    table_worker workers[TABLE_THREADS];
    test_thread threads[TABLE_THREADS];
    uint32_t failures = 0;
    uint32_t kept = (TABLE_ROUNDS + 2) / 3;
    int index;
    for (index = 0; index < TABLE_THREADS; index++) {
        workers[index] = (table_worker){table, (uint64_t)(index + 1) << 40, 0};
        CHECK(start_test_thread(&threads[index], churn_the_table, &workers[index]));
    }
    for (index = 0; index < TABLE_THREADS; index++) {
        join_test_thread(&threads[index]);
        failures += workers[index].failures;
    }
    CHECK(failures == 0);
    CHECK(entries_in(table) == kept * TABLE_THREADS && holdings_in(table) == kept * TABLE_THREADS);
    for (index = 0; index < TABLE_THREADS; index++) {
        uint32_t ended = 0;
        while (weivel_c_handles_end_owned(table, workers[index].base, NULL) == WEIVEL_C_STATUS_OK) {
            ended++;
        }
        CHECK(ended == kept);
    }
    CHECK(entries_in(table) == 0);
    weivel_c_handles_free(table);
}

#if !defined(WEIVEL_C_USING_SHARED)

/** What a call's answered step records, and where the handle it records is answered. */
typedef struct recording {
    weivel_c_handles *table;
    const weivel_handle *answered;
    uint32_t kind;
    weivel_handle owner;
    void *object;
} recording;

static weivel_c_status record_the_handle(void *data) {
    recording *self = data;
    return add(self->table, self->kind, *self->answered, self->owner, self->object);
}

/** What a listener found in the table for the handles an event named, as the event was heard. */
typedef struct found_when_heard {
    weivel_c_handles *table;
    uint32_t registrations_found;
    uint32_t registrations_missed;
    uint32_t wants_found;
    uint32_t wants_missed;
    uint32_t rejections;
} found_when_heard;

static void look_up(weivel_c_handles *table, weivel_handle handle, void *object, uint32_t *found, uint32_t *missed) {
    weivel_c_handle_entry entry = WEIVEL_INIT(weivel_c_handle_entry);
    if (weivel_c_handles_find(table, handle, &entry) == WEIVEL_C_STATUS_OK && entry.object == object) {
        *found += 1;
    } else {
        *missed += 1;
    }
}

static weivel_c_status look_up_what_an_event_names(hearing *self, weivel_c_core *core, const weivel_c_event *event) {
    found_when_heard *more = self->more;
    (void)core;
    if (event->kind == WEIVEL_EVENT_WATCH && event->data.watch.change == WEIVEL_CHANGE_REGISTERED) {
        look_up(more->table, event->data.watch.implementation, &service_object, &more->registrations_found,
                &more->registrations_missed);
    } else if (event->kind == WEIVEL_EVENT_WANT_SETTLED) {
        look_up(more->table, event->data.want.want, &want_object, &more->wants_found, &more->wants_missed);
        more->rejections += event->data.want.implementation == 0;
        if (event->data.want.implementation != 0) {
            look_up(more->table, event->data.want.implementation, &service_object, &more->registrations_found,
                    &more->registrations_missed);
        }
        CHECK(weivel_c_handles_end(more->table, event->data.want.want, NULL) == WEIVEL_C_STATUS_OK);
    }
    return WEIVEL_C_STATUS_OK;
}

static void an_entry_made_as_the_core_answers_is_there_when_an_event_naming_the_handle_is_heard(void) {
    hearing listener = {0};
    found_when_heard more = {0};
    weivel_service_want consumed = want_of("p:a");
    weivel_c_handles *table = make_table();
    weivel_c_core *core = make_core(NULL, &listener, NULL);
    weivel_handle provider = open_plugin(core, "p", PROVIDED, 1, NULL, 0);
    weivel_handle consumer = open_plugin(core, "c", NULL, 0, &consumed, 1);
    weivel_handle watch = 0;
    weivel_handle registration = 0;
    weivel_handle want = 0;
    recording of_the_service = {table, &registration, WEIVEL_C_HANDLE_SERVICE_REGISTRATION, provider,
                                &service_object};
    recording of_the_want = {table, &want, WEIVEL_C_HANDLE_WANT, consumer, &want_object};
    weivel_c_answered_step record_service = {record_the_handle, &of_the_service};
    weivel_c_answered_step record_want = {record_the_handle, &of_the_want};
    more.table = table;
    listener.react = look_up_what_an_event_names;
    listener.more = &more;
    CHECK(weivel_c_core_watch_service(core, WEIVEL_HOST, "p:a", &watch, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_register_service_then(core, provider, "p:a", &registration, NULL, &record_service) ==
          WEIVEL_C_STATUS_OK);
    CHECK(more.registrations_found == 1 && more.registrations_missed == 0);
    CHECK(weivel_c_core_want_service_then(core, consumer, "p:a", NULL, &want, NULL, &record_want) ==
          WEIVEL_C_STATUS_OK);
    CHECK(more.wants_found == 1 && more.wants_missed == 0 && more.registrations_found == 2);
    CHECK(weivel_c_core_want_service_then(core, consumer, "p:undeclared", NULL, &want, NULL, &record_want) ==
          WEIVEL_C_STATUS_OK);
    CHECK(more.rejections == 1 && more.wants_found == 2 && more.wants_missed == 0);
    CHECK(more.registrations_missed == 0);
    end_core(core);
    weivel_c_handles_free(table);
}

static weivel_c_status fail_to_record(void *data) {
    uint32_t *runs = data;
    *runs += 1;
    return WEIVEL_C_STATUS_OUT_OF_MEMORY;
}

static void a_step_runs_only_for_a_call_the_core_answered_and_its_failure_is_the_calls_answer(void) {
    uint32_t runs = 0;
    weivel_c_answered_step failing = {fail_to_record, &runs};
    weivel_c_core *core = make_core(NULL, NULL, NULL);
    weivel_handle plugin = open_plugin(core, "p", PROVIDED, 1, NULL, 0);
    weivel_handle registration = 0;
    CHECK(weivel_c_core_register_service_then(core, plugin, "elsewhere:x", &registration, NULL, &failing) ==
          WEIVEL_C_STATUS_REFUSED);
    CHECK(runs == 0);
    CHECK(weivel_c_core_register_service_then(core, plugin, "p:a", &registration, NULL, &failing) ==
          WEIVEL_C_STATUS_OUT_OF_MEMORY);
    CHECK(runs == 1 && registration != 0);
    CHECK(weivel_c_core_unregister(core, registration, NULL) == WEIVEL_C_STATUS_OK);
    end_core(core);
}

typedef struct scarce_memory {
    uint32_t allocations;
    uint32_t fail_from;
    int32_t held;
} scarce_memory;

static void *allocate_scarcely(void *data, size_t size) {
    scarce_memory *self = data;
    self->allocations++;
    if (self->fail_from != 0 && self->allocations >= self->fail_from) {
        return NULL;
    }
    self->held++;
    return malloc(size);
}

static void release_scarcely(void *data, void *memory) {
    scarce_memory *self = data;
    self->held--;
    free(memory);
}

static void an_addition_that_runs_out_of_memory_changes_nothing(void) {
    scarce_memory memory = {0, 0, 0};
    weivel_c_allocator allocator = {allocate_scarcely, release_scarcely, &memory};
    weivel_c_handles *table = make_table();
    uint32_t added = 0;
    weivel_c_handles_replace_allocator(table, &allocator);
    memory.fail_from = 1;
    CHECK(add(table, WEIVEL_C_HANDLE_WATCH, 5, 9, &watch_object) == WEIVEL_C_STATUS_OUT_OF_MEMORY);
    CHECK(weivel_c_handles_hold(table, 9, 5, &watch_object) == WEIVEL_C_STATUS_OUT_OF_MEMORY);
    CHECK(entries_in(table) == 0 && holdings_in(table) == 0 && answers_nothing(table, 5));
    memory.fail_from = 0;
    CHECK(add(table, WEIVEL_C_HANDLE_WATCH, 5, 9, &watch_object) == WEIVEL_C_STATUS_OK);
    memory.fail_from = memory.allocations + 1;
    while (add(table, WEIVEL_C_HANDLE_WATCH, 100 + added, 9, &watch_object) == WEIVEL_C_STATUS_OK) {
        added++;
    }
    CHECK(added >= 1 && added < 64);
    CHECK(entries_in(table) == added + 1 && answers_nothing(table, 100 + added));
    CHECK(holds(table, 5, WEIVEL_C_HANDLE_WATCH, 9, &watch_object));
    CHECK(holds(table, 100 + added - 1, WEIVEL_C_HANDLE_WATCH, 9, &watch_object));
    weivel_c_handles_free(table);
    CHECK(memory.held == 0);
}

static void a_reserved_addition_needs_no_memory_and_an_unused_reservation_is_given_back(void) {
    scarce_memory memory = {0, 0, 0};
    weivel_c_allocator allocator = {allocate_scarcely, release_scarcely, &memory};
    weivel_c_handle_entry entry = entry_of(WEIVEL_C_HANDLE_WATCH, 5, 9, &watch_object);
    weivel_c_handles *table = make_table();
    uint32_t reserved = 0;
    uint32_t index;
    weivel_c_handles_replace_allocator(table, &allocator);
    CHECK(weivel_c_handles_add_reserved(table, &entry, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_handles_hold_reserved(table, 9, 5, &watch_object) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_handles_unreserve(table) == WEIVEL_C_STATUS_MISUSE);
    memory.fail_from = 1;
    CHECK(weivel_c_handles_reserve(table) == WEIVEL_C_STATUS_OUT_OF_MEMORY);
    memory.fail_from = 0;
    for (index = 0; index < 40; index++) {
        CHECK(weivel_c_handles_reserve(table) == WEIVEL_C_STATUS_OK);
    }
    memory.fail_from = memory.allocations + 1;
    for (index = 0; index < 20; index++) {
        entry.handle = 100 + index;
        CHECK(weivel_c_handles_add_reserved(table, &entry, NULL) == WEIVEL_C_STATUS_OK);
        CHECK(weivel_c_handles_hold_reserved(table, 9, 100 + index, &watch_object) == WEIVEL_C_STATUS_OK);
    }
    CHECK(entries_in(table) == 20 && holdings_in(table) == 20);
    CHECK(holds(table, 119, WEIVEL_C_HANDLE_WATCH, 9, &watch_object));
    CHECK(weivel_c_handles_add_reserved(table, &entry, NULL) == WEIVEL_C_STATUS_MISUSE);
    memory.fail_from = 0;
    CHECK(weivel_c_handles_reserve(table) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_handles_unreserve(table) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_handles_unreserve(table) == WEIVEL_C_STATUS_MISUSE);
    memory.fail_from = memory.allocations + 1;
    while (weivel_c_handles_reserve(table) == WEIVEL_C_STATUS_OK) {
        reserved++;
    }
    entry.handle = 500;
    CHECK(reserved < 64 && weivel_c_handles_add(table, &entry, NULL) == WEIVEL_C_STATUS_OUT_OF_MEMORY);
    for (index = 0; index < reserved; index++) {
        entry.handle = 600 + index;
        CHECK(weivel_c_handles_add_reserved(table, &entry, NULL) == WEIVEL_C_STATUS_OK);
    }
    CHECK(entries_in(table) == 20 + reserved);
    weivel_c_handles_free(table);
    CHECK(memory.held == 0);
}

/** What the step of a get or of a listing saw of the core's answer as it ran. */
typedef struct seen_answer {
    const weivel_handle *registration;
    const weivel_capability_list *list;
    uint32_t runs;
    weivel_handle registration_seen;
    uint32_t listed_seen;
    uint32_t events_heard_before;
    const hearing *listener;
} seen_answer;

static weivel_c_status see_the_answer(void *data) {
    seen_answer *self = data;
    self->runs++;
    self->registration_seen = self->registration == NULL ? 0 : *self->registration;
    self->listed_seen = self->list == NULL ? 0 : self->list->count;
    self->events_heard_before = self->listener->count;
    return WEIVEL_C_STATUS_OK;
}

static void the_step_of_a_get_and_of_a_listing_runs_with_the_answer_before_any_event_is_heard(void) {
    static const char *const CAPABILITY[] = {"screens"};
    hearing listener = {0};
    weivel_service_want consumed = want_of("p:a");
    weivel_manifest manifest = WEIVEL_INIT(weivel_manifest);
    weivel_capability_list list = WEIVEL_INIT(weivel_capability_list);
    weivel_c_core *core = make_core(NULL, &listener, NULL);
    weivel_handle provider = open_plugin(core, "p", PROVIDED, 1, NULL, 0);
    weivel_handle consumer = open_plugin(core, "c", NULL, 0, &consumed, 1);
    weivel_handle maker = 0;
    weivel_handle registration = 0;
    weivel_handle answered = 0;
    weivel_handle implementation = 0;
    seen_answer seen = {&answered, NULL, 0, 0, 0, 0, &listener};
    weivel_c_answered_step then = {see_the_answer, &seen};
    manifest.import_name = "m";
    manifest.api = 1;
    manifest.capabilities.provides.items = CAPABILITY;
    manifest.capabilities.provides.count = 1;
    manifest.capabilities.consumes.items = CAPABILITY;
    manifest.capabilities.consumes.count = 1;
    CHECK(weivel_c_core_get_service_then(core, consumer, "p:a", &answered, NULL, &then) == WEIVEL_C_STATUS_NONE);
    CHECK(seen.runs == 0);
    CHECK(weivel_c_core_get_service_then(core, consumer, "p:undeclared", &answered, NULL, &then) ==
          WEIVEL_C_STATUS_REFUSED);
    CHECK(seen.runs == 0);
    CHECK(weivel_c_core_register_service(core, provider, "p:a", &registration, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_get_service_then(core, consumer, "p:a", &answered, NULL, &then) == WEIVEL_C_STATUS_OK);
    CHECK(seen.runs == 1 && seen.registration_seen == registration && answered == registration);
    CHECK(weivel_c_core_register_manifest(core, &manifest, &maker, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_open_plugin(core, maker, NULL, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_provide_capability(core, maker, "screens", &implementation, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_verify_activation(core, maker, NULL) == WEIVEL_C_STATUS_OK);
    seen = (seen_answer){NULL, &list, 0, 0, 0, 0, &listener};
    CHECK(weivel_c_core_all_capabilities_then(core, consumer, "screens", &list, NULL, &then) ==
          WEIVEL_C_STATUS_REFUSED);
    CHECK(seen.runs == 0 && seen.events_heard_before == 0);
    CHECK(weivel_c_core_all_capabilities_then(core, maker, "screens", &list, NULL, &then) == WEIVEL_C_STATUS_OK);
    CHECK(seen.runs == 1 && seen.listed_seen == 1 && list.count == 1);
    CHECK(weivel_c_core_capability_list_release(core, &list) == WEIVEL_C_STATUS_OK);
    end_core(core);
}

static void run_the_tests_of_the_layers_own_seams(void) {
    an_entry_made_as_the_core_answers_is_there_when_an_event_naming_the_handle_is_heard();
    a_step_runs_only_for_a_call_the_core_answered_and_its_failure_is_the_calls_answer();
    an_addition_that_runs_out_of_memory_changes_nothing();
    a_reserved_addition_needs_no_memory_and_an_unused_reservation_is_given_back();
    the_step_of_a_get_and_of_a_listing_runs_with_the_answer_before_any_event_is_heard();
}

#else

static void run_the_tests_of_the_layers_own_seams(void) {
}

#endif

int main(int argument_count, char **arguments) {
    if (argument_count != 2 || !load_tables(arguments[1])) {
        fprintf(stderr, "usage: table_test <core-library>\n");
        return 2;
    }
    an_entry_is_made_when_the_core_answers_the_handle();
    an_entry_ends_when_what_its_handle_stands_for_ends();
    a_want_that_settles_ends_its_entry_when_the_settlement_is_heard();
    a_handle_the_table_does_not_hold_answers_nothing_and_is_never_matched_to_a_newer_object();
    a_revocation_naming_a_forgotten_plugin_drops_that_plugins_holding();
    the_payload_a_plugin_was_opened_with_reaches_its_record_without_a_search();
    the_entries_of_one_owner_are_ended_one_by_one();
    a_misused_call_changes_nothing();
    many_entries_are_each_found_while_others_come_and_go();
    calls_from_several_threads_leave_the_table_whole();
    run_the_tests_of_the_layers_own_seams();
    CHECK(weivel_c_core_unload(&real_table) == WEIVEL_C_STATUS_OK);
    return check_summary("handles table");
}
