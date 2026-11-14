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

/**
 * Takes an open the destroy will wait for, unless the destroy has begun. Answers 0, taking nothing, once it has.
 *
 * @implNote The destroy's first step walks only the plugins already opened, so an open made while it walks would be
 * missed by it; counting the open first is what makes the destroy wait for it to be answered.
 */
static int take_the_open(weivel_c_host *host) {
    int running;
    weivel_c_mutex_lock(&host->lock);
    running = host->state == HOST_RUNNING;
    host->opens_in_flight += (uint32_t)running;
    weivel_c_mutex_unlock(&host->lock);
    return running;
}

/** Ends an open answered without its plugin being taken into the opened list, and lets a waiting destroy end. */
static void end_the_open(weivel_c_host *host) {
    int destroy_may_end;
    weivel_c_mutex_lock(&host->lock);
    host->opens_in_flight--;
    destroy_may_end = weivel_c_host_destroy_may_end(host);
    weivel_c_mutex_unlock(&host->lock);
    if (destroy_may_end) {
        weivel_c_host_end_the_destroy(host);
    }
}

/**
 * Takes an opened plugin into the opened list as activating. Answers 0 when the destroy began while it was being
 * opened: the plugin is then taken for a release owing no deactivate, since its activate is never called.
 */
static int mark_activating(host_plugin *plugin, weivel_c_completion *activation) {
    weivel_c_host *host = plugin->host;
    int running;
    weivel_c_mutex_lock(&host->lock);
    host->opens_in_flight--;
    running = host->state == HOST_RUNNING;
    if (running) {
        plugin->state = WEIVEL_C_PLUGIN_ACTIVATING;
        plugin->activating = activation;
        plugin->activate_called = 1;
        plugin->activations++;
        weivel_c_host_join_opened(plugin);
    } else {
        plugin->state = WEIVEL_C_PLUGIN_RELEASING;
        host->releases_in_flight++;
    }
    weivel_c_mutex_unlock(&host->lock);
    return running;
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

/** Answers an open the core did not take: refused, or failed for a reason of the layer's. */
static void answer_an_open_not_taken(host_plugin *plugin, weivel_c_completion *activation, weivel_c_status opened) {
    if (opened == WEIVEL_C_STATUS_REFUSED) {
        tell_at_once(activation, WEIVEL_C_OUTCOME_REFUSED);
    } else {
        fail_the_open(plugin, activation, opened);
    }
    end_the_open(activation->host);
}

/** Opens one plugin of the plan, builds nothing it could fail to build afterwards, and calls its activate. */
static void start_plugin(weivel_c_completion *activation) {
    weivel_c_host *host = activation->host;
    host_plugin *plugin = activation->plugin;
    weivel_c_status opened;
    if (!take_the_open(host)) {
        tell_at_once(activation, WEIVEL_C_OUTCOME_RELEASED);
        return;
    }
    opened = plugin == NULL ? WEIVEL_C_STATUS_FAILED : open_with_its_token(plugin, &activation->outcome.refusal);
    if (opened != WEIVEL_C_STATUS_OK) {
        answer_an_open_not_taken(plugin, activation, opened);
    } else if (mark_activating(plugin, activation)) {
        call_the_activate(plugin, activation);
    } else {
        tell_at_once(activation, WEIVEL_C_OUTCOME_RELEASED);
        weivel_c_host_go_on_releasing(plugin);
    }
}

/**
 * One activation's starting of its plan's plugins, run as a step so every activate is called inside a dispatch: what
 * an activate's own calls queue is then heard once it returns, as everywhere else in a dispatch. It lives on the
 * activating thread's stack, since the step runs before the activation returns or not at all.
 */
typedef struct host_start {
    weivel_c_step step;
    /** One per plugin of the plan's order, in that order, each made before the first open. */
    weivel_c_completion **activations;
    uint32_t count;
} host_start;

static weivel_c_status start_every_plugin(void *data, weivel_c_core *core) {
    host_start *start = data;
    uint32_t index;
    (void)core;
    for (index = 0; index < start->count; index++) {
        start_plugin(start->activations[index]);
    }
    return WEIVEL_C_STATUS_OK;
}

/**
 * Starts the plan's plugins inside a dispatch, and before this returns.
 *
 * @implNote Where no dispatch runs, this thread becomes the dispatcher and runs the start at once. Where one runs, here
 * or on another thread, nothing is queued and this thread starts the plugins itself, as a call made while that
 * dispatch runs: so the plugins are started exactly once, on this thread, before the activation returns, and never
 * by a step another dispatch could take first.
 */
static void start_in_a_dispatch(weivel_c_host *host, weivel_c_completion **activations, uint32_t count) {
    host_start start;
    uint32_t ran = 0;
    start.step = (weivel_c_step)WEIVEL_INIT(weivel_c_step);
    start.step.run = start_every_plugin;
    start.step.data = &start;
    start.activations = activations;
    start.count = count;
    if (weivel_c_core_run_as_the_dispatcher(host->core, &start.step, &ran) != WEIVEL_C_STATUS_OK || !ran) {
        start_every_plugin(&start, host->core);
    }
}

/** Starts the plugins of a plan the core answered, or answers OUT_OF_MEMORY, starting none and releasing the plan. */
static weivel_c_status start_the_plan(weivel_c_host *host, const weivel_c_outcome_listener *listener,
                                      weivel_activation_plan *plan) {
    weivel_c_completion **activations = weivel_c_host_make_completions(host, plan->order, plan->order_count, listener);
    if (activations == NULL) {
        weivel_c_core_activation_plan_release(host->core, plan);
        return WEIVEL_C_STATUS_OUT_OF_MEMORY;
    }
    start_in_a_dispatch(host, activations, plan->order_count);
    host->allocator.release(host->allocator.data, activations);
    return WEIVEL_C_STATUS_OK;
}

/**
 * Takes an activation the destroy will wait for, unless the destroy has begun. Answers 0, taking nothing, once it has.
 *
 * @implNote Counted from the running check until every started plugin's outcome is queued: a destroy that ended
 * between the plan and the start would destroy the core, and a plugin the start then keeps from its activate could no
 * longer be told `released`.
 */
static int take_the_activation(weivel_c_host *host) {
    int running;
    weivel_c_mutex_lock(&host->lock);
    running = host->state == HOST_RUNNING;
    host->activations_in_flight += (uint32_t)running;
    weivel_c_mutex_unlock(&host->lock);
    return running;
}

/** Ends an activation taken while the host ran, once its outcomes are queued, and lets a waiting destroy end. */
static void end_the_activation(weivel_c_host *host) {
    int destroy_may_end;
    weivel_c_mutex_lock(&host->lock);
    host->activations_in_flight--;
    destroy_may_end = weivel_c_host_destroy_may_end(host);
    weivel_c_mutex_unlock(&host->lock);
    if (destroy_may_end) {
        weivel_c_host_end_the_destroy(host);
    }
}

static weivel_c_status plan_and_start(weivel_c_host *host, const weivel_c_activation *activation,
                                      weivel_activation_plan *plan, weivel_c_refusal *refusal) {
    weivel_strings provided = {NULL, 0};
    const char **ids = list_the_hosts_services(host, &provided.count);
    weivel_c_status status;
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
    return start_the_plan(host, &activation->listener, plan);
}

weivel_c_status weivel_c_host_activate(weivel_c_host *host, const weivel_c_activation *activation,
                                       weivel_activation_plan *plan, weivel_c_refusal *refusal) {
    weivel_c_status status;
    if (host == NULL || plan == NULL || !activation_is_usable(activation)) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    if (!take_the_activation(host)) {
        return WEIVEL_C_STATUS_DESTROYED;
    }
    status = plan_and_start(host, activation, plan, refusal);
    end_the_activation(host);
    return status;
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
    host_ending *ending = data;
    weivel_c_host *host = ending->plugin->host;
    int current;
    (void)core;
    weivel_c_mutex_lock(&host->lock);
    current = ending->plugin->activations == ending->activation;
    weivel_c_mutex_unlock(&host->lock);
    return current ? weivel_c_context_end_entries(ending->plugin->context) : WEIVEL_C_STATUS_OK;
}

static void stamp_the_ending(host_ending *ending) {
    weivel_c_host *host = ending->plugin->host;
    weivel_c_mutex_lock(&host->lock);
    ending->activation = ending->plugin->activations;
    weivel_c_mutex_unlock(&host->lock);
}

weivel_c_status weivel_c_host_quarantine(host_ending *ending, const weivel_quarantine_reason *reason) {
    host_plugin *plugin = ending->plugin;
    weivel_c_host *host = plugin->host;
    weivel_c_status status;
    stamp_the_ending(ending);
    status = weivel_c_core_quarantine_plugin(host->core, plugin->handle, reason, NULL);
    weivel_c_host_report_call(host, status, "the core did not quarantine a plugin the host quarantined");
    if (status == WEIVEL_C_STATUS_OK) {
        weivel_c_core_defer(host->core, &ending->step, NULL);
    }
    return status;
}

weivel_c_status weivel_c_host_verify_plugin(host_ending *ending, weivel_c_refusal *refusal) {
    host_plugin *plugin = ending->plugin;
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
        weivel_c_host_quarantine(ending, NULL);
    }
    return verified;
}

/** 1 while the plugin is in the opened list, so a quarantine of it is the host's to make and a release ends it. */
static int is_opened(host_plugin *plugin) {
    int opened;
    weivel_c_mutex_lock(&plugin->host->lock);
    opened = plugin->opened != 0;
    weivel_c_mutex_unlock(&plugin->host->lock);
    return opened;
}

/**
 * @implNote A plugin the host never opened, or whose open the core refused, is answered as the core answers its
 * verification, and quarantined by nothing: the host holds nothing open of it that a quarantine would end, nor any
 * release that would end the quarantine.
 */
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
    if (!is_opened(found)) {
        return weivel_c_core_verify_activation(host->core, plugin, refusal);
    }
    return weivel_c_host_verify_plugin(&found->verify_ending, refusal);
}

/**
 * Under the host's lock: takes the settling of an activation. An activation whose plugin's release has begun, or
 * whose host is being destroyed, is left to that release, which answers it `released`.
 *
 * @implNote A settling taken is counted until its outcome is queued: the plugin's release no longer answers it, so a
 * destroy that ended meanwhile would destroy the core before that outcome could be queued, and it would be lost.
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
    host->settles_in_flight++;
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

/** Ends a settling taken while the host ran, once its outcome is queued, and lets a waiting destroy end. */
static void end_the_settling(weivel_c_host *host) {
    int destroy_may_end;
    weivel_c_mutex_lock(&host->lock);
    host->settles_in_flight--;
    destroy_may_end = weivel_c_host_destroy_may_end(host);
    weivel_c_mutex_unlock(&host->lock);
    if (destroy_may_end) {
        weivel_c_host_end_the_destroy(host);
    }
}

/** Verifies or quarantines the plugin of a settling taken, and queues its outcome. */
static void settle_what_was_taken(weivel_c_completion *activation, uint32_t answer, const char *reason) {
    host_plugin *plugin = activation->plugin;
    if (answer == WEIVEL_C_ENTRY_DONE) {
        weivel_c_status verified = weivel_c_host_verify_plugin(&activation->ending, &activation->outcome.refusal);
        tell_unless_told(activation, verified == WEIVEL_C_STATUS_OK ? WEIVEL_C_OUTCOME_ACTIVE
                                                                     : WEIVEL_C_OUTCOME_QUARANTINED);
        return;
    }
    if (set_state_unless_released(plugin, WEIVEL_C_PLUGIN_QUARANTINED)) {
        weivel_quarantine_reason quarantined = reason_of_a_failed_activate(&activation->outcome, reason);
        weivel_c_host_quarantine(&activation->ending, &quarantined);
    }
    tell_unless_told(activation, WEIVEL_C_OUTCOME_QUARANTINED);
}

weivel_c_status weivel_c_host_settle_activation(weivel_c_completion *activation, uint32_t answer, const char *reason) {
    weivel_c_host *host = activation->host;
    weivel_c_status taken;
    weivel_c_mutex_lock(&host->lock);
    taken = take_the_settling(activation);
    weivel_c_mutex_unlock(&host->lock);
    if (taken != WEIVEL_C_STATUS_OK) {
        return taken;
    }
    settle_what_was_taken(activation, answer, reason);
    end_the_settling(host);
    return WEIVEL_C_STATUS_OK;
}
