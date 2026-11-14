#include "host.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define END_OF(type, field) (offsetof(type, field) + sizeof(((type *)0)->field))
#define FIRST_ACTIVATION_SIZE END_OF(weivel_c_activation, listener)

static int activation_is_usable(const weivel_c_activation *activation) {
    return activation != NULL && activation->size >= FIRST_ACTIVATION_SIZE && activation->listener.hear != NULL &&
           (activation->plugins != NULL || activation->count == 0);
}

/** Under the host's lock: the ids of the services the host registered that still stand. */
static uint32_t count_the_hosts_services(const weivel_c_host *host) {
    const host_service *kept;
    uint32_t count = 0;
    for (kept = host->services; kept != NULL; kept = kept->next) {
        count += kept->live;
    }
    return count;
}

/**
 * Lists the ids the host provides, for a plan. The ids are the host's until it is freed, so the list stays readable
 * after the lock is let go however the services change meanwhile.
 */
static const char **list_the_hosts_services(weivel_c_host *host, uint32_t *count) {
    const host_service *kept;
    const char **ids;
    uint32_t live;
    *count = 0;
    weivel_c_mutex_lock(&host->lock);
    live = count_the_hosts_services(host);
    ids = malloc((live == 0 ? 1 : live) * sizeof *ids);
    for (kept = host->services; ids != NULL && kept != NULL; kept = kept->next) {
        if (kept->live) {
            ids[(*count)++] = kept->id;
        }
    }
    weivel_c_mutex_unlock(&host->lock);
    return ids;
}

static void call_the_activate(host_plugin *plugin, weivel_c_completion *activation) {
    const char *reason = NULL;
    uint32_t answer = plugin->entry.activate(plugin->entry.data, plugin->context, activation, &reason);
    if (answer == WEIVEL_C_ENTRY_PENDING) {
        return;
    }
    if (answer != WEIVEL_C_ENTRY_DONE && answer != WEIVEL_C_ENTRY_FAILED) {
        answer = WEIVEL_C_ENTRY_FAILED;
        reason = "the activate answered a value that is no answer";
    }
    weivel_c_host_settle_activation(activation, answer, reason);
}

static weivel_c_status open_with_its_token(host_plugin *plugin, weivel_c_refusal *refusal) {
    weivel_plugin_options options = WEIVEL_INIT(weivel_plugin_options);
    weivel_c_context_token(plugin->context, &options.payload);
    return weivel_c_core_open_plugin(plugin->host->core, plugin->handle, &options, refusal);
}

/** 1 when the core holds the plugin open, as its ledger row says. */
static int is_held_open(host_plugin *plugin) {
    weivel_ledger_row row = WEIVEL_INIT(weivel_ledger_row);
    weivel_c_core *core = plugin->host->core;
    int held_open = 0;
    if (weivel_c_core_ledger_entry(core, plugin->handle, &row, NULL) == WEIVEL_C_STATUS_OK) {
        held_open = row.status == WEIVEL_PLUGIN_STATUS_ACTIVE;
        weivel_c_core_ledger_row_release(core, &row);
    }
    return held_open;
}

/** Releases and forgets a plugin whose open failed though the core holds it open; no deactivate is owed. */
static void release_after_a_failed_open(host_plugin *plugin) {
    weivel_c_host *host = plugin->host;
    weivel_c_mutex_lock(&host->lock);
    plugin->state = WEIVEL_C_PLUGIN_RELEASING;
    host->releases_in_flight++;
    weivel_c_mutex_unlock(&host->lock);
    weivel_c_host_go_on_releasing(plugin);
}

/**
 * @implNote An open that fails for a reason other than the core's refusal is a defect of the layer: it is told to the
 * sink and answered `failed`, rather than to the caller, so the plan and every other plugin's outcome still reach it.
 */
static void fail_the_open(host_plugin *plugin, weivel_c_completion *activation, weivel_c_status opened) {
    weivel_c_host *host = activation->host;
    int chosen;
    weivel_c_host_report(host, WEIVEL_C_FAILURE_OPEN, opened,
                         "a plugin's open failed for a reason other than the core's refusal");
    weivel_c_refusal_release(&activation->outcome.refusal);
    if (plugin != NULL && is_held_open(plugin)) {
        release_after_a_failed_open(plugin);
    }
    weivel_c_mutex_lock(&host->lock);
    chosen = weivel_c_host_choose_outcome(activation, WEIVEL_C_OUTCOME_FAILED);
    weivel_c_mutex_unlock(&host->lock);
    if (chosen) {
        weivel_c_host_tell_outcome(activation);
    }
}

static void mark_activating(host_plugin *plugin, weivel_c_completion *activation) {
    weivel_c_host *host = plugin->host;
    weivel_c_mutex_lock(&host->lock);
    plugin->state = WEIVEL_C_PLUGIN_ACTIVATING;
    plugin->activating = activation;
    plugin->activate_called = 1;
    weivel_c_host_join_opened(plugin);
    weivel_c_mutex_unlock(&host->lock);
}

static void tell_at_once(weivel_c_completion *activation, uint32_t kind) {
    weivel_c_host *host = activation->host;
    int chosen;
    weivel_c_mutex_lock(&host->lock);
    chosen = weivel_c_host_choose_outcome(activation, kind);
    weivel_c_mutex_unlock(&host->lock);
    if (chosen) {
        weivel_c_host_tell_outcome(activation);
    }
}

/** Opens one plugin of the plan, builds nothing it could fail to build afterwards, and calls its activate. */
static void start_plugin(weivel_c_host *host, const weivel_c_outcome_listener *listener, weivel_handle handle) {
    host_plugin *plugin = weivel_c_host_plugin_of(host, handle);
    weivel_c_completion *activation = weivel_c_host_make_completion(host, plugin, handle);
    weivel_c_status opened;
    if (activation == NULL) {
        weivel_c_host_report(host, WEIVEL_C_FAILURE_OPEN, WEIVEL_C_STATUS_OUT_OF_MEMORY,
                             "there was no memory to start a plugin of a plan, nor to tell its outcome");
        return;
    }
    activation->outcome.listener = *listener;
    opened = plugin == NULL ? WEIVEL_C_STATUS_FAILED : open_with_its_token(plugin, &activation->outcome.refusal);
    if (opened == WEIVEL_C_STATUS_REFUSED) {
        tell_at_once(activation, WEIVEL_C_OUTCOME_REFUSED);
    } else if (opened != WEIVEL_C_STATUS_OK) {
        fail_the_open(plugin, activation, opened);
    } else {
        mark_activating(plugin, activation);
        call_the_activate(plugin, activation);
    }
}

static int claim_the_start(host_start *start) {
    weivel_c_host *host = start->host;
    int claimed;
    weivel_c_mutex_lock(&host->lock);
    claimed = !start->claimed;
    start->claimed = 1;
    weivel_c_mutex_unlock(&host->lock);
    return claimed;
}

static void start_every_plugin(host_start *start) {
    uint32_t index;
    for (index = 0; index < start->count; index++) {
        start_plugin(start->host, &start->listener, start->order[index]);
    }
}

static weivel_c_status run_the_start(void *data, weivel_c_core *core) {
    host_start *start = data;
    (void)core;
    if (claim_the_start(start)) {
        start_every_plugin(start);
    }
    return WEIVEL_C_STATUS_OK;
}

static host_start *make_the_start(weivel_c_host *host, const weivel_c_outcome_listener *listener,
                                  const weivel_activation_plan *plan) {
    host_start *start = calloc(1, sizeof *start + plan->order_count * sizeof start->order[0]);
    uint32_t index;
    if (start == NULL) {
        return NULL;
    }
    start->step = (weivel_c_step)WEIVEL_INIT(weivel_c_step);
    start->step.run = run_the_start;
    start->step.data = start;
    start->host = host;
    start->listener = *listener;
    start->count = plan->order_count;
    for (index = 0; index < plan->order_count; index++) {
        start->order[index] = plan->order[index];
    }
    weivel_c_mutex_lock(&host->lock);
    start->next = host->starts;
    host->starts = start;
    weivel_c_mutex_unlock(&host->lock);
    return start;
}

/**
 * Starts the plan's plugins inside a dispatch, and before this returns.
 *
 * @implNote Where no dispatch runs, the defer makes this thread the dispatcher and the start has run when it returns.
 * Where one runs, here or on another thread, the start would wait in its queue; this thread then takes it from the
 * step and starts the plugins itself, as a call made while that dispatch runs, so the activation still returns only
 * once every activate has been started. The step finds the start taken and does nothing.
 */
static void start_in_a_dispatch(host_start *start) {
    uint32_t here = 0;
    if (weivel_c_core_defer(start->host->core, &start->step, &here) == WEIVEL_C_STATUS_OK && here) {
        return;
    }
    if (claim_the_start(start)) {
        start_every_plugin(start);
    }
}

weivel_c_status weivel_c_host_activate(weivel_c_host *host, const weivel_c_activation *activation,
                                       weivel_activation_plan *plan, weivel_c_refusal *refusal) {
    weivel_strings provided = {NULL, 0};
    const char **ids;
    host_start *start;
    weivel_c_status status;
    if (host == NULL || plan == NULL || !activation_is_usable(activation)) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    if (!weivel_c_host_is_running(host)) {
        return WEIVEL_C_STATUS_DESTROYED;
    }
    ids = list_the_hosts_services(host, &provided.count);
    if (ids == NULL) {
        return WEIVEL_C_STATUS_OUT_OF_MEMORY;
    }
    provided.items = ids;
    status = weivel_c_core_plan_activation(host->core, activation->plugins, activation->count,
                                           activation->edge_source, &provided, plan, refusal);
    free(ids);
    if (status != WEIVEL_C_STATUS_OK) {
        return status;
    }
    start = make_the_start(host, &activation->listener, plan);
    if (start == NULL) {
        weivel_c_core_activation_plan_release(host->core, plan);
        return WEIVEL_C_STATUS_OUT_OF_MEMORY;
    }
    start_in_a_dispatch(start);
    return WEIVEL_C_STATUS_OK;
}

weivel_c_status weivel_c_host_plan_release(weivel_c_host *host, weivel_activation_plan *plan) {
    if (host == NULL || plan == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    return weivel_c_core_activation_plan_release(host->core, plan);
}

/** Under the host's lock: 1 when the plugin's release has begun or it is forgotten. */
static int is_released(const host_plugin *plugin) {
    return plugin->state == WEIVEL_C_PLUGIN_RELEASING || plugin->state == 0;
}

static int set_state_unless_released(host_plugin *plugin, uint32_t state) {
    weivel_c_host *host = plugin->host;
    int set;
    weivel_c_mutex_lock(&host->lock);
    set = !is_released(plugin);
    if (set) {
        plugin->state = state;
    }
    weivel_c_mutex_unlock(&host->lock);
    return set;
}

weivel_c_status weivel_c_host_end_quarantined_entries(void *data, weivel_c_core *core) {
    host_plugin *plugin = data;
    (void)core;
    return weivel_c_context_end_entries(plugin->context);
}

weivel_c_status weivel_c_host_quarantine(host_plugin *plugin, const weivel_quarantine_reason *reason) {
    weivel_c_host *host = plugin->host;
    weivel_c_status status = weivel_c_core_quarantine_plugin(host->core, plugin->handle, reason, NULL);
    weivel_c_host_report_call(host, status, "the core did not quarantine a plugin the host quarantined");
    if (status == WEIVEL_C_STATUS_OK) {
        weivel_c_core_defer(host->core, &plugin->quarantine_ending, NULL);
    }
    return status;
}

weivel_c_status weivel_c_host_verify_plugin(host_plugin *plugin, weivel_c_refusal *refusal) {
    weivel_c_host *host = plugin->host;
    weivel_c_status verified = weivel_c_core_verify_activation(host->core, plugin->handle, refusal);
    if (verified == WEIVEL_C_STATUS_OK) {
        set_state_unless_released(plugin, WEIVEL_C_PLUGIN_ACTIVE);
        return verified;
    }
    if (verified == WEIVEL_C_STATUS_DESTROYED) {
        return verified;
    }
    if (verified != WEIVEL_C_STATUS_REFUSED) {
        weivel_c_host_report_call(host, verified, "the core could not verify a plugin, which the host quarantined");
    }
    if (set_state_unless_released(plugin, WEIVEL_C_PLUGIN_QUARANTINED)) {
        weivel_c_host_quarantine(plugin, NULL);
    }
    return verified;
}

weivel_c_status weivel_c_host_verify(weivel_c_host *host, weivel_handle plugin, weivel_c_refusal *refusal) {
    host_plugin *found;
    if (host == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    if (!weivel_c_host_is_running(host)) {
        return WEIVEL_C_STATUS_DESTROYED;
    }
    found = weivel_c_host_plugin_of(host, plugin);
    if (found == NULL) {
        return WEIVEL_C_STATUS_NONE;
    }
    return weivel_c_host_verify_plugin(found, refusal);
}

/**
 * Under the host's lock: takes the settling of an activation. An activation whose plugin's release has begun, or
 * whose host is being destroyed, is left to that release, which answers it `released`.
 */
static weivel_c_status take_the_settling(weivel_c_completion *activation) {
    weivel_c_host *host = activation->host;
    host_plugin *plugin = activation->plugin;
    if (activation->finished) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    activation->finished = 1;
    if (host->state != HOST_RUNNING) {
        return WEIVEL_C_STATUS_DESTROYED;
    }
    if (plugin == NULL || plugin->activating != activation) {
        return WEIVEL_C_STATUS_NONE;
    }
    plugin->activating = NULL;
    return WEIVEL_C_STATUS_OK;
}

/** Tells the activation's outcome, `released` in place of `kind` when the plugin's release began meanwhile. */
static void tell_unless_told(weivel_c_completion *activation, uint32_t kind) {
    weivel_c_host *host = activation->host;
    int chosen;
    weivel_c_mutex_lock(&host->lock);
    chosen = weivel_c_host_choose_outcome(activation,
                                          is_released(activation->plugin) ? WEIVEL_C_OUTCOME_RELEASED : kind);
    weivel_c_mutex_unlock(&host->lock);
    if (chosen) {
        weivel_c_host_tell_outcome(activation);
    }
}

/** The reason a plugin whose activate failed is quarantined with: what happened, the plugin's own words, and a fix. */
static weivel_quarantine_reason reason_of_a_failed_activate(host_outcome *outcome, const char *said) {
    weivel_quarantine_reason reason = WEIVEL_INIT(weivel_quarantine_reason);
    snprintf(outcome->detail, sizeof outcome->detail, "the plugin's activate failed: %s",
             said == NULL || said[0] == '\0' ? "it gave no reason" : said);
    snprintf(outcome->fix, sizeof outcome->fix, "%s",
             "remove what makes the plugin's activate fail, then activate the plugin again");
    outcome->refusal.code = WEIVEL_REFUSAL_NONE;
    outcome->refusal.detail = outcome->detail;
    outcome->refusal.fix = outcome->fix;
    outcome->refusal.has_fix = 1;
    reason.detail = outcome->detail;
    reason.fix = outcome->fix;
    return reason;
}

weivel_c_status weivel_c_host_settle_activation(weivel_c_completion *activation, uint32_t answer, const char *reason) {
    weivel_c_host *host = activation->host;
    host_plugin *plugin = activation->plugin;
    weivel_c_status taken;
    weivel_c_mutex_lock(&host->lock);
    taken = take_the_settling(activation);
    weivel_c_mutex_unlock(&host->lock);
    if (taken != WEIVEL_C_STATUS_OK) {
        return taken;
    }
    if (answer == WEIVEL_C_ENTRY_DONE) {
        weivel_c_status verified = weivel_c_host_verify_plugin(plugin, &activation->outcome.refusal);
        tell_unless_told(activation, verified == WEIVEL_C_STATUS_OK ? WEIVEL_C_OUTCOME_ACTIVE
                                                                     : WEIVEL_C_OUTCOME_QUARANTINED);
        return WEIVEL_C_STATUS_OK;
    }
    if (set_state_unless_released(plugin, WEIVEL_C_PLUGIN_QUARANTINED)) {
        weivel_quarantine_reason quarantined = reason_of_a_failed_activate(&activation->outcome, reason);
        weivel_c_host_quarantine(plugin, &quarantined);
    }
    tell_unless_told(activation, WEIVEL_C_OUTCOME_QUARANTINED);
    return WEIVEL_C_STATUS_OK;
}
