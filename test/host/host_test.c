/**
 * @file
 * The host's registration, its own services, and activation with each of its five outcomes, against the real core.
 * This target links the layer and nothing of the standard's: registration needs only a manifest value and an entry.
 */

#include "plugins.h"

#if !defined(WEIVEL_C_USING_SHARED)
#include "../../src/host/host.h"
#endif

static int clock_object;

static void a_plugin_is_registered_from_a_manifest_value_and_an_entry_alone(void) {
    host_world in;
    test_plugin plugin;
    test_plugin twin;
    weivel_c_entry without = WEIVEL_INIT(weivel_c_entry);
    weivel_c_refusal refusal = WEIVEL_INIT(weivel_c_refusal);
    weivel_handle handle = 77;
    begin_host_world(&in);
    name_acting_plugin(&plugin, &in, "agenda", activate_and_answer_done, NULL);
    register_test_plugin(&plugin);
    CHECK(plugin.handle != 0 && state_of(&in, &plugin) == WEIVEL_C_PLUGIN_REGISTERED);
    CHECK(ledger_status_of(&in, &plugin, NULL) == WEIVEL_PLUGIN_STATUS_REGISTERED);
    name_acting_plugin(&twin, &in, "agenda", activate_and_answer_done, NULL);
    CHECK(weivel_c_host_register(in.host, &twin.manifest, &twin.entry, &handle, &refusal) ==
          WEIVEL_C_STATUS_REFUSED);
    CHECK(handle == 0 && refusal.code == WEIVEL_REFUSAL_NAME_TAKEN);
    CHECK(weivel_c_refusal_release(&refusal) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_host_register(in.host, &twin.manifest, &without, &handle, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_host_register(in.host, NULL, &twin.entry, &handle, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_host_register(NULL, &twin.manifest, &twin.entry, &handle, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(sink_count(&in) == 0);
    end_host_world(&in);
}

/** Gets the host's clock service through its context and remembers the pointer it was answered. */
static uint32_t activate_and_get_the_clock(void *data, weivel_c_context *context, weivel_c_completion *completion,
                                           const char **reason) {
    test_plugin *self = data;
    (void)completion;
    (void)reason;
    CHECK(weivel_c_context_get_service(context, "clock", &self->more, NULL) == WEIVEL_C_STATUS_OK);
    return WEIVEL_C_ENTRY_DONE;
}

static uint32_t planned_count(host_world *in, test_plugin *plugin, uint32_t *refusals) {
    weivel_c_activation activation = WEIVEL_INIT(weivel_c_activation);
    weivel_activation_plan plan = WEIVEL_INIT(weivel_activation_plan);
    uint32_t order;
    activation.plugins = &plugin->handle;
    activation.count = 1;
    activation.edge_source = WEIVEL_EDGES_SERVICES;
    activation.listener.hear = hear_outcome;
    activation.listener.data = &in->told;
    CHECK(weivel_c_host_activate(in->host, &activation, &plan, NULL) == WEIVEL_C_STATUS_OK);
    order = plan.order_count;
    *refusals = plan.refusal_count;
    CHECK(weivel_c_host_plan_release(in->host, &plan) == WEIVEL_C_STATUS_OK);
    return order;
}

static void a_service_of_the_hosts_own_is_offered_to_a_plan_and_answered_as_the_hosts_pointer(void) {
    host_world in;
    test_plugin consumer;
    weivel_handle registration = 0;
    uint32_t refusals = 0;
    uint32_t kind = 0;
    begin_host_world(&in);
    name_acting_plugin(&consumer, &in, "c", activate_and_get_the_clock, NULL);
    wants_service(&consumer, "clock")->optional = WEIVEL_CHOICE_NO;
    register_test_plugin(&consumer);
    CHECK(planned_count(&in, &consumer, &refusals) == 0 && refusals == 1);
    CHECK(weivel_c_host_register_service(in.host, "clock", &clock_object, NULL, &registration, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(planned_count(&in, &consumer, &refusals) == 1 && refusals == 0);
    CHECK(consumer.more == &clock_object);
    CHECK(outcomes_of(&in.told, consumer.handle, &kind) == 1 && kind == WEIVEL_C_OUTCOME_ACTIVE);
    CHECK(weivel_c_host_unregister_service(in.host, registration, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_host_unregister_service(in.host, registration, NULL) == WEIVEL_C_STATUS_NONE);
    CHECK(sink_count(&in) == 0);
    end_host_world(&in);
}

static void an_activate_that_finishes_is_verified_and_one_that_fails_is_quarantined_with_a_reason(void) {
    host_world in;
    test_plugin good;
    test_plugin bad;
    test_plugin *both[2];
    weivel_ledger_row row = WEIVEL_INIT(weivel_ledger_row);
    weivel_c_core *core = NULL;
    uint32_t kind = 0;
    begin_host_world(&in);
    name_acting_plugin(&good, &in, "good", activate_and_answer_done, NULL);
    name_acting_plugin(&bad, &in, "bad", activate_and_fail, NULL);
    register_test_plugin(&good);
    register_test_plugin(&bad);
    both[0] = &good;
    both[1] = &bad;
    CHECK(activate_in_order(&in, both, 2, WEIVEL_EDGES_CAPABILITIES));
    CHECK(outcomes_of(&in.told, good.handle, &kind) == 1 && kind == WEIVEL_C_OUTCOME_ACTIVE);
    CHECK(state_of(&in, &good) == WEIVEL_C_PLUGIN_ACTIVE);
    CHECK(outcomes_of(&in.told, bad.handle, &kind) == 1 && kind == WEIVEL_C_OUTCOME_QUARANTINED);
    CHECK(strstr(in.told.details[1], "the disk is full") != NULL);
    CHECK(state_of(&in, &bad) == WEIVEL_C_PLUGIN_QUARANTINED);
    CHECK(ledger_status_of(&in, &bad, &row) == WEIVEL_PLUGIN_STATUS_QUARANTINED);
    CHECK(row.refusal.detail != NULL && strstr(row.refusal.detail, "the disk is full") != NULL);
    CHECK(row.refusal.fix != NULL && row.refusal.fix[0] != '\0');
    CHECK(weivel_c_host_core(in.host, &core) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_ledger_row_release(core, &row) == WEIVEL_C_STATUS_OK);
    CHECK(sink_count(&in) == 0);
    end_host_world(&in);
}

static void a_plugin_the_core_refuses_to_open_is_answered_refused_with_its_refusal(void) {
    host_world in;
    test_plugin plugin;
    test_plugin *only[1];
    uint32_t kind = 0;
    begin_host_world(&in);
    name_acting_plugin(&plugin, &in, "agenda", activate_and_answer_done, NULL);
    register_test_plugin(&plugin);
    only[0] = &plugin;
    CHECK(activate_in_order(&in, only, 1, WEIVEL_EDGES_CAPABILITIES));
    CHECK(activate_in_order(&in, only, 1, WEIVEL_EDGES_CAPABILITIES));
    CHECK(outcomes_of(&in.told, plugin.handle, &kind) == 2 && kind == WEIVEL_C_OUTCOME_REFUSED);
    CHECK(in.told.codes[1] == WEIVEL_REFUSAL_PLUGIN_STATE);
    CHECK(place_of(&in.heard, "activate agenda") >= 0 && in.heard.count == 3);
    CHECK(state_of(&in, &plugin) == WEIVEL_C_PLUGIN_ACTIVE && sink_count(&in) == 0);
    end_host_world(&in);
}

static void a_plugin_released_while_its_activate_is_in_flight_is_answered_released_and_its_activate_settles_nothing(
    void) {
    host_world in;
    test_plugin plugin;
    test_plugin *only[1];
    uint32_t finished = 0;
    uint32_t kind = 0;
    begin_host_world(&in);
    name_acting_plugin(&plugin, &in, "agenda", activate_and_wait, NULL);
    register_test_plugin(&plugin);
    only[0] = &plugin;
    CHECK(activate_in_order(&in, only, 1, WEIVEL_EDGES_CAPABILITIES));
    CHECK(in.told.count == 0 && state_of(&in, &plugin) == WEIVEL_C_PLUGIN_ACTIVATING);
    CHECK(weivel_c_host_release(in.host, plugin.handle, &finished) == WEIVEL_C_STATUS_OK && finished == 1);
    CHECK(outcomes_of(&in.told, plugin.handle, &kind) == 1 && kind == WEIVEL_C_OUTCOME_RELEASED);
    CHECK(state_of(&in, &plugin) == 0 && ledger_status_of(&in, &plugin, NULL) == 0);
    forget_crossings();
    CHECK(weivel_c_completion_finish(plugin.activation, WEIVEL_C_ENTRY_DONE, NULL) == WEIVEL_C_STATUS_NONE);
    CHECK(weivel_c_completion_finish(plugin.activation, WEIVEL_C_ENTRY_DONE, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(crossed.next_event == 0 && in.told.count == 1 && sink_count(&in) == 0);
    CHECK(weivel_c_host_release(in.host, plugin.handle, &finished) == WEIVEL_C_STATUS_NONE && finished == 0);
    end_host_world(&in);
}

/** What the bent open answers in place of the core's, and whether it lets the core open the plugin first. */
static weivel_status open_answers;
static int open_reaches_the_core;

static weivel_status open_bent(weivel_core *core, weivel_handle plugin, const weivel_plugin_options *options,
                               uint32_t *pending) {
    weivel_status opened = WEIVEL_STATUS_OK;
    if (open_reaches_the_core) {
        opened = real_table.open_plugin(core, plugin, options, pending);
    }
    return opened == WEIVEL_STATUS_OK ? open_answers : opened;
}

static void fail_one_open(int reaches_the_core) {
    host_world in;
    test_plugin plugin;
    test_plugin *only[1];
    weivel_c_core_table bent = counting_table;
    uint32_t kind = 0;
    bent.open_plugin = open_bent;
    open_answers = WEIVEL_STATUS_OUT_OF_MEMORY;
    open_reaches_the_core = reaches_the_core;
    begin_host_world_on(&in, &bent);
    name_acting_plugin(&plugin, &in, "agenda", activate_and_answer_done, NULL);
    register_test_plugin(&plugin);
    only[0] = &plugin;
    CHECK(activate_in_order(&in, only, 1, WEIVEL_EDGES_CAPABILITIES));
    CHECK(outcomes_of(&in.told, plugin.handle, &kind) == 1 && kind == WEIVEL_C_OUTCOME_FAILED);
    CHECK(in.told.codes[0] == WEIVEL_REFUSAL_NONE && in.heard.count == 1);
    CHECK(sink_count(&in) == 1 && in.sink.items[0].failure.kind == WEIVEL_C_FAILURE_OPEN);
    CHECK(ledger_status_of(&in, &plugin, NULL) ==
          (reaches_the_core ? 0u : (uint32_t)WEIVEL_PLUGIN_STATUS_REGISTERED));
    end_host_world(&in);
}

static void an_open_that_fails_for_no_refusal_is_answered_failed_and_leaves_nothing_open_in_the_core(void) {
    fail_one_open(1);
    fail_one_open(0);
}

/** Mirrors the layer vector lifecycle/an-awaited-want-does-not-hold-back-a-later-plugin. */
static void an_awaited_want_does_not_hold_back_a_later_plugin(void) {
    host_world in;
    test_plugin agenda;
    test_plugin calendar;
    acting wanting = {"calendar:events", WEIVEL_C_STATUS_FAILED};
    acting registering = {"calendar:events", WEIVEL_C_STATUS_FAILED};
    test_plugin *both[2];
    uint32_t kind = 0;
    begin_host_world(&in);
    name_acting_plugin(&agenda, &in, "agenda", activate_and_await_a_want, &wanting);
    wants_service(&agenda, "calendar:events");
    name_acting_plugin(&calendar, &in, "calendar", activate_and_register, &registering);
    offers_service(&calendar, "calendar:events");
    register_test_plugin(&agenda);
    register_test_plugin(&calendar);
    both[0] = &agenda;
    both[1] = &calendar;
    CHECK(activate_in_order(&in, both, 2, WEIVEL_EDGES_CAPABILITIES));
    CHECK(is_before(&in.heard, "activate agenda", "activate calendar"));
    CHECK(is_before(&in.heard, "activate calendar", "registered calendar calendar:events"));
    CHECK(is_before(&in.heard, "registered calendar calendar:events", "settled agenda calendar:events"));
    CHECK(outcomes_of(&in.told, agenda.handle, &kind) == 1 && kind == WEIVEL_C_OUTCOME_ACTIVE);
    CHECK(outcomes_of(&in.told, calendar.handle, &kind) == 1 && kind == WEIVEL_C_OUTCOME_ACTIVE);
    CHECK(state_of(&in, &agenda) == WEIVEL_C_PLUGIN_ACTIVE && state_of(&in, &calendar) == WEIVEL_C_PLUGIN_ACTIVE);
    CHECK(agenda.activation == NULL && sink_count(&in) == 0);
    end_host_world(&in);
}

/** The steps of consume/a-refused-verify-quarantines-the-plugin, each a plugin's own or the host's. */
typedef struct screens {
    host_world in;
    test_plugin agenda;
    test_plugin calendar;
    test_plugin flaky;
    watched_heard agenda_heard;
    watched_heard flaky_heard;
} screens;

static int calendar_screen;
static int flaky_screen;

static void name_screens(screens *with) {
    name_acting_plugin(&with->agenda, &with->in, "agenda", activate_and_wait, NULL);
    with->agenda.capabilities_consumed[with->agenda.capabilities_consumed_count++] = "screens";
    name_acting_plugin(&with->calendar, &with->in, "calendar", activate_and_wait, NULL);
    with->calendar.capabilities_provided[with->calendar.capabilities_provided_count++] = "screens";
    name_acting_plugin(&with->flaky, &with->in, "flaky", activate_and_wait, NULL);
    with->flaky.capabilities_provided[with->flaky.capabilities_provided_count++] = "screens";
    with->flaky.capabilities_provided[with->flaky.capabilities_provided_count++] = "extras";
    with->flaky.capabilities_consumed[with->flaky.capabilities_consumed_count++] = "screens";
}

static void open_screens(screens *with) {
    test_plugin *all[3];
    name_screens(with);
    register_test_plugin(&with->agenda);
    register_test_plugin(&with->calendar);
    register_test_plugin(&with->flaky);
    all[0] = &with->agenda;
    all[1] = &with->calendar;
    all[2] = &with->flaky;
    activate_in_order(&with->in, all, 3, WEIVEL_EDGES_CAPABILITIES);
}

static void check_the_quarantine_reason(screens *with) {
    weivel_ledger_row row = WEIVEL_INIT(weivel_ledger_row);
    weivel_c_core *core = NULL;
    CHECK(ledger_status_of(&with->in, &with->flaky, &row) == WEIVEL_PLUGIN_STATUS_QUARANTINED);
    CHECK(row.refusal.code == WEIVEL_REFUSAL_ACTIVATION_MISMATCH);
    check_text(row.subject, "flaky", "the subject of a refused verify, as the ledger shows it");
    CHECK(row.has_fix == 1);
    CHECK(weivel_c_host_core(with->in.host, &core) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_ledger_row_release(core, &row) == WEIVEL_C_STATUS_OK);
    CHECK(ledger_status_of(&with->in, &with->agenda, NULL) == WEIVEL_PLUGIN_STATUS_ACTIVE);
}

/** Mirrors the layer vector consume/a-refused-verify-quarantines-the-plugin. */
static void a_refused_verify_quarantines_the_plugin_in_the_same_step(void) {
    screens with;
    weivel_c_capability_watcher agenda_watcher = {hear_a_capability, &with.agenda_heard};
    weivel_c_capability_watcher flaky_watcher = {hear_a_capability, &with.flaky_heard};
    weivel_c_refusal refusal = WEIVEL_INIT(weivel_c_refusal);
    uint32_t finished = 0;
    memset(&with, 0, sizeof with);
    begin_host_world(&with.in);
    open_screens(&with);
    CHECK(weivel_c_context_watch_capability(with.agenda.context, "screens", &agenda_watcher, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_watch_capability(with.flaky.context, "screens", &flaky_watcher, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_provide_capability(with.calendar.context, "screens", &calendar_screen, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_host_verify(with.in.host, with.calendar.handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_provide_capability(with.flaky.context, "screens", &flaky_screen, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_host_verify(with.in.host, with.flaky.handle, &refusal) == WEIVEL_C_STATUS_REFUSED);
    CHECK(refusal.code == WEIVEL_REFUSAL_ACTIVATION_MISMATCH && weivel_c_refusal_release(&refusal) == 0);
    CHECK(state_of(&with.in, &with.flaky) == WEIVEL_C_PLUGIN_QUARANTINED);
    CHECK(weivel_c_host_release(with.in.host, with.calendar.handle, &finished) == WEIVEL_C_STATUS_OK && finished);
    CHECK(with.agenda_heard.count == 2 && with.agenda_heard.changes[0] == WEIVEL_CAPABILITY_PROVIDED);
    CHECK(with.agenda_heard.changes[1] == WEIVEL_CAPABILITY_WITHDRAWN);
    CHECK(with.flaky_heard.count == 1 && with.flaky_heard.changes[0] == WEIVEL_CAPABILITY_PROVIDED);
    check_the_quarantine_reason(&with);
    CHECK(sink_count(&with.in) == 0);
    end_host_world(&with.in);
}

#if !defined(WEIVEL_C_USING_SHARED)
/** An allocator that refuses the allocation of the given number, counting from 1, and makes every other. */
typedef struct refusing_allocator {
    uint32_t made;
    uint32_t refused_at;
} refusing_allocator;

static void *allocate_unless_refused(void *data, size_t size) {
    refusing_allocator *self = data;
    return ++self->made == self->refused_at ? NULL : malloc(size);
}

static void release_plainly(void *data, void *memory) {
    (void)data;
    free(memory);
}

static void open_three_with_the_last_completion_refused(host_world *in, test_plugin *plugins,
                                                        refusing_allocator *refusing) {
    weivel_c_allocator allocator = {allocate_unless_refused, release_plainly, NULL};
    allocator.data = refusing;
    name_acting_plugin(&plugins[0], in, "first", activate_and_answer_done, NULL);
    name_acting_plugin(&plugins[1], in, "second", activate_and_answer_done, NULL);
    name_acting_plugin(&plugins[2], in, "third", activate_and_answer_done, NULL);
    register_test_plugin(&plugins[0]);
    register_test_plugin(&plugins[1]);
    register_test_plugin(&plugins[2]);
    weivel_c_host_replace_allocator(in->host, &allocator);
}

/** The host whose records a bent forget counts; set by the test that bends it. */
static weivel_c_host *counted_host;
static uint32_t records_at_the_forget;

static weivel_status forget_counting_the_records(weivel_core *core, weivel_handle plugin, uint32_t *pending) {
    CHECK(weivel_c_contexts_count(counted_host->contexts, NULL, &records_at_the_forget) == WEIVEL_C_STATUS_OK);
    return real_table.forget_plugin(core, plugin, pending);
}

/** Registers a service of its own and wants one nothing provides, so it holds two records. */
static uint32_t activate_holding_two_records(void *data, weivel_c_context *context, weivel_c_completion *completion,
                                             const char **reason) {
    test_plugin *self = data;
    weivel_c_want_callback callback = {trace_settlement, self};
    (void)completion;
    (void)reason;
    self->context = context;
    CHECK(weivel_c_context_register_service(context, "agenda:x", &clock_object, NULL, NULL, NULL) ==
          WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_want_service(context, "never:there", NULL, &callback, NULL, NULL) == WEIVEL_C_STATUS_OK);
    return WEIVEL_C_ENTRY_DONE;
}

static void counts_of(host_world *in, uint32_t *records, uint32_t *entries, uint32_t *holdings) {
    CHECK(weivel_c_contexts_count(in->host->contexts, NULL, records) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_handles_count(in->host->handles, entries, holdings) == WEIVEL_C_STATUS_OK);
}

static void a_release_ends_its_plugins_entries_before_it_forgets_it_and_leaves_none(void) {
    host_world in;
    test_plugin plugin;
    test_plugin *only[1];
    weivel_c_core_table bent = counting_table;
    uint32_t records = 9;
    uint32_t entries = 9;
    uint32_t holdings = 9;
    bent.forget_plugin = forget_counting_the_records;
    begin_host_world_on(&in, &bent);
    counted_host = in.host;
    records_at_the_forget = 9;
    name_acting_plugin(&plugin, &in, "agenda", activate_holding_two_records, NULL);
    offers_service(&plugin, "agenda:x");
    wants_service(&plugin, "never:there");
    register_test_plugin(&plugin);
    only[0] = &plugin;
    CHECK(activate_in_order(&in, only, 1, WEIVEL_EDGES_CAPABILITIES));
    counts_of(&in, &records, &entries, &holdings);
    CHECK(records == 2 && entries == 4 && holdings == 0);
    CHECK(weivel_c_host_release(in.host, plugin.handle, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(records_at_the_forget == 0);
    counts_of(&in, &records, &entries, &holdings);
    CHECK(records == 0 && entries == 1 && holdings == 0);
    end_host_world(&in);
}

static void an_activation_short_of_memory_for_its_outcomes_starts_no_plugin(void) {
    host_world in;
    test_plugin plugins[3];
    refusing_allocator refusing = {0, 3};
    weivel_handle handles[3];
    weivel_c_activation activation = WEIVEL_INIT(weivel_c_activation);
    weivel_activation_plan plan = WEIVEL_INIT(weivel_activation_plan);
    begin_host_world(&in);
    open_three_with_the_last_completion_refused(&in, plugins, &refusing);
    handles[0] = plugins[0].handle;
    handles[1] = plugins[1].handle;
    handles[2] = plugins[2].handle;
    activation.plugins = handles;
    activation.count = 3;
    activation.edge_source = WEIVEL_EDGES_CAPABILITIES;
    activation.listener.hear = hear_outcome;
    activation.listener.data = &in.told;
    CHECK(weivel_c_host_activate(in.host, &activation, &plan, NULL) == WEIVEL_C_STATUS_OUT_OF_MEMORY);
    CHECK(in.heard.count == 0 && in.told.count == 0 && sink_count(&in) == 0);
    CHECK(ledger_status_of(&in, &plugins[0], NULL) == WEIVEL_PLUGIN_STATUS_REGISTERED);
    end_host_world(&in);
}
#endif

static void verifying_a_plugin_never_opened_answers_the_cores_refusal_and_quarantines_nothing(void) {
    host_world in;
    test_plugin plugin;
    weivel_c_refusal refusal = WEIVEL_INIT(weivel_c_refusal);
    begin_host_world(&in);
    name_acting_plugin(&plugin, &in, "agenda", activate_and_answer_done, NULL);
    register_test_plugin(&plugin);
    CHECK(weivel_c_host_verify(in.host, plugin.handle, &refusal) == WEIVEL_C_STATUS_REFUSED);
    CHECK(refusal.code != WEIVEL_REFUSAL_NONE && weivel_c_refusal_release(&refusal) == WEIVEL_C_STATUS_OK);
    CHECK(state_of(&in, &plugin) == WEIVEL_C_PLUGIN_REGISTERED);
    CHECK(ledger_status_of(&in, &plugin, NULL) == WEIVEL_PLUGIN_STATUS_REGISTERED);
    CHECK(sink_count(&in) == 0);
    end_host_world(&in);
}

static char *text_on_the_heap(const char *text) {
    char *made = malloc(strlen(text) + 1);
    CHECK(made != NULL);
    memcpy(made, text, strlen(text) + 1);
    return made;
}

/** A manifest whose every part the caller allocated, to be scribbled over and freed once it is registered. */
typedef struct heap_manifest {
    weivel_manifest manifest;
    char *name;
    char *offered;
    char *wanted;
    char *constraint;
    weivel_service_offer *offers;
    weivel_service_want *wants;
    weivel_number *numbers;
} heap_manifest;

static void build_on_the_heap(heap_manifest *self) {
    self->manifest = (weivel_manifest)WEIVEL_INIT(weivel_manifest);
    self->name = text_on_the_heap("agenda");
    self->offered = text_on_the_heap("agenda:x");
    self->wanted = text_on_the_heap("mail:inbox");
    self->constraint = text_on_the_heap("speed");
    self->offers = calloc(1, sizeof *self->offers);
    self->wants = calloc(1, sizeof *self->wants);
    self->numbers = calloc(1, sizeof *self->numbers);
    self->offers->size = (uint32_t)sizeof *self->offers;
    self->offers->id = self->offered;
    *self->wants = want_of(self->wanted);
    self->numbers->size = (uint32_t)sizeof *self->numbers;
    self->numbers->name = self->constraint;
    self->numbers->value = 4.5;
    self->wants->constraints = (weivel_numbers){self->numbers, 1};
    self->manifest.import_name = self->name;
    self->manifest.api = 1;
    self->manifest.services.provides = (weivel_service_offers){self->offers, 1};
    self->manifest.services.consumes = (weivel_service_wants){self->wants, 1};
}

static void scribble_and_free(heap_manifest *self) {
    memset(self->name, 'x', strlen(self->name));
    memset(self->offered, 'x', strlen(self->offered));
    memset(self->wanted, 'x', strlen(self->wanted));
    memset(self->constraint, 'x', strlen(self->constraint));
    memset(self->numbers, 0, sizeof *self->numbers);
    free(self->name);
    free(self->offered);
    free(self->wanted);
    free(self->constraint);
    free(self->offers);
    free(self->wants);
    free(self->numbers);
    memset(&self->manifest, 0, sizeof self->manifest);
}

static void check_the_copy(const weivel_manifest *copy, const heap_manifest *given) {
    CHECK(copy != NULL && copy != &given->manifest && copy->size == sizeof *copy);
    check_text(copy->import_name, "agenda", "the import name the host copied");
    CHECK(copy->services.provides.count == 1 && copy->services.consumes.count == 1);
    check_text(copy->services.provides.items[0].id, "agenda:x", "the offer the host copied");
    check_text(copy->services.consumes.items[0].id, "mail:inbox", "the want the host copied");
    CHECK(copy->services.consumes.items[0].constraints.count == 1);
    check_text(copy->services.consumes.items[0].constraints.items[0].name, "speed", "the constraint copied");
    CHECK(copy->services.consumes.items[0].constraints.items[0].value == 4.5);
}

static void a_manifest_is_copied_so_its_caller_may_free_it_once_it_is_registered(void) {
    host_world in;
    test_plugin plugin;
    test_plugin *only[1];
    heap_manifest given;
    const weivel_manifest *copy = NULL;
    uint32_t kind = 0;
    begin_host_world(&in);
    name_acting_plugin(&plugin, &in, "agenda", activate_and_answer_done, NULL);
    build_on_the_heap(&given);
    CHECK(weivel_c_host_register(in.host, &given.manifest, &plugin.entry, &plugin.handle, NULL) ==
          WEIVEL_C_STATUS_OK);
    scribble_and_free(&given);
    only[0] = &plugin;
    CHECK(activate_in_order(&in, only, 1, WEIVEL_EDGES_CAPABILITIES));
    CHECK(outcomes_of(&in.told, plugin.handle, &kind) == 1 && kind == WEIVEL_C_OUTCOME_ACTIVE);
    CHECK(weivel_c_context_manifest(plugin.context, &copy) == WEIVEL_C_STATUS_OK);
    check_the_copy(copy, &given);
    CHECK(place_of(&in.heard, "outcome agenda active") >= 0 && sink_count(&in) == 0);
    end_host_world(&in);
}

static void every_call_answers_its_mistakes_as_values(void) {
    host_world in;
    weivel_c_host_options options = WEIVEL_INIT(weivel_c_host_options);
    weivel_c_activation activation = WEIVEL_INIT(weivel_c_activation);
    weivel_activation_plan plan = WEIVEL_INIT(weivel_activation_plan);
    weivel_c_host *host = (weivel_c_host *)&clock_object;
    uint32_t state = 0;
    begin_host_world(&in);
    CHECK(weivel_c_host_create(NULL, &host, NULL) == WEIVEL_C_STATUS_MISUSE && host == NULL);
    CHECK(weivel_c_host_create(&options, &host, NULL) == WEIVEL_C_STATUS_MISUSE && host == NULL);
    options.core_path = "";
    CHECK(weivel_c_host_create(&options, &host, NULL) == WEIVEL_C_STATUS_NOT_A_LIBRARY && host == NULL);
    CHECK(weivel_c_host_activate(in.host, &activation, &plan, NULL) == WEIVEL_C_STATUS_MISUSE);
    activation.listener.hear = hear_outcome;
    activation.listener.data = &in.told;
    CHECK(weivel_c_host_activate(in.host, &activation, &plan, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_host_plugin_state(in.host, 12345, &state) == WEIVEL_C_STATUS_NONE);
    CHECK(weivel_c_host_verify(in.host, 12345, NULL) == WEIVEL_C_STATUS_NONE);
    CHECK(weivel_c_host_release(in.host, 12345, NULL) == WEIVEL_C_STATUS_NONE);
    CHECK(weivel_c_completion_finish(NULL, WEIVEL_C_ENTRY_DONE, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_host_free(in.host) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_host_free(NULL) == WEIVEL_C_STATUS_OK);
    CHECK(sink_count(&in) == 0);
    end_host_world(&in);
}

int main(int argument_count, char **arguments) {
    if (argument_count != 2 || !load_tables(arguments[1])) {
        fprintf(stderr, "usage: host_test <core-library>\n");
        return 2;
    }
    a_plugin_is_registered_from_a_manifest_value_and_an_entry_alone();
    a_service_of_the_hosts_own_is_offered_to_a_plan_and_answered_as_the_hosts_pointer();
    an_activate_that_finishes_is_verified_and_one_that_fails_is_quarantined_with_a_reason();
    a_plugin_the_core_refuses_to_open_is_answered_refused_with_its_refusal();
    a_plugin_released_while_its_activate_is_in_flight_is_answered_released_and_its_activate_settles_nothing();
    an_open_that_fails_for_no_refusal_is_answered_failed_and_leaves_nothing_open_in_the_core();
    an_awaited_want_does_not_hold_back_a_later_plugin();
    a_refused_verify_quarantines_the_plugin_in_the_same_step();
    verifying_a_plugin_never_opened_answers_the_cores_refusal_and_quarantines_nothing();
    a_manifest_is_copied_so_its_caller_may_free_it_once_it_is_registered();
#if !defined(WEIVEL_C_USING_SHARED)
    an_activation_short_of_memory_for_its_outcomes_starts_no_plugin();
    a_release_ends_its_plugins_entries_before_it_forgets_it_and_leaves_none();
#endif


    every_call_answers_its_mistakes_as_values();

    CHECK(weivel_c_core_unload(&real_table) == WEIVEL_C_STATUS_OK);
    return check_summary("host");
}
