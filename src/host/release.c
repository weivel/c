#include "host.h"

#include <stdio.h>

#include "../core/calls.h"

/** What beginning a release found, under the host's lock. */
typedef struct beginning {
    /** The activation in flight, which this release answers `released`; NULL when there is none to answer. */
    weivel_c_completion *released_activation;
    /** 1 when the plugin's activate was called and it offers a deactivate. */
    int owes_deactivate;
} beginning;

/**
 * Under the host's lock: takes a plugin's release when the plugin is open and no release of it has begun. A release
 * takes the plugin out of the opened list at once, so a destroy releases each plugin once.
 */
static int take_the_release(host_plugin *plugin, beginning *began) {
    weivel_c_host *host = plugin->host;
    if (!plugin->opened || plugin->state == WEIVEL_C_PLUGIN_RELEASING || plugin->state == 0) {
        return 0;
    }
    plugin->state = WEIVEL_C_PLUGIN_RELEASING;
    weivel_c_host_leave_opened(plugin);
    host->releases_in_flight++;
    began->released_activation = NULL;
    if (plugin->activating != NULL &&
        weivel_c_host_choose_outcome(plugin->activating, WEIVEL_C_OUTCOME_RELEASED)) {
        began->released_activation = plugin->activating;
    }
    plugin->activating = NULL;
    began->owes_deactivate = plugin->activate_called && plugin->entry.deactivate != NULL;
    plugin->activate_called = 0;
    return 1;
}

static void report_a_failed_deactivate(weivel_c_host *host, uint32_t answer, const char *reason) {
    char text[WEIVEL_C_HOST_TEXT];
    if (answer == WEIVEL_C_ENTRY_FAILED) {
        snprintf(text, sizeof text, "a plugin's deactivate failed, and its release went on: %s",
                 reason == NULL || reason[0] == '\0' ? "it gave no reason" : reason);
    } else {
        snprintf(text, sizeof text, "a plugin's deactivate answered %u, which is no answer, and its release went on",
                 (unsigned)answer);
    }
    weivel_c_host_report(host, WEIVEL_C_FAILURE_DEACTIVATE, WEIVEL_C_STATUS_FAILED, text);
}

/** Answers -1 when the deactivation was finished already, else 1 when the release then finished, else 0. */
static int take_the_deactivation(weivel_c_completion *deactivation, uint32_t answer, const char *reason) {
    weivel_c_host *host = deactivation->host;
    int taken;
    weivel_c_mutex_lock(&host->lock);
    taken = !deactivation->finished;
    deactivation->finished = 1;
    weivel_c_mutex_unlock(&host->lock);
    if (!taken) {
        return -1;
    }
    if (answer != WEIVEL_C_ENTRY_DONE) {
        report_a_failed_deactivate(host, answer, reason);
    }
    return weivel_c_host_go_on_releasing(deactivation->plugin);
}

weivel_c_status weivel_c_host_settle_deactivation(weivel_c_completion *deactivation, uint32_t answer,
                                                  const char *reason) {
    return take_the_deactivation(deactivation, answer, reason) < 0 ? WEIVEL_C_STATUS_MISUSE : WEIVEL_C_STATUS_OK;
}

/**
 * Calls the deactivate while the plugin is still whole. Answers 1 when the release then finished.
 *
 * @implNote A pending deactivate moves the core's release and every step after it to the thread that finishes it, so
 * whether those steps are deferred is decided there; the layer sets no timeout of its own.
 */
static int call_the_deactivate(host_plugin *plugin) {
    const char *reason = NULL;
    uint32_t answer = plugin->entry.deactivate(plugin->entry.data, plugin->context, &plugin->deactivating, &reason);
    if (answer == WEIVEL_C_ENTRY_PENDING) {
        return 0;
    }
    return take_the_deactivation(&plugin->deactivating, answer, reason) > 0;
}

weivel_c_status weivel_c_host_begin_release(host_plugin *plugin, uint32_t *finished) {
    weivel_c_host *host = plugin->host;
    beginning began;
    int taken;
    int ended;
    weivel_c_mutex_lock(&host->lock);
    taken = take_the_release(plugin, &began);
    weivel_c_mutex_unlock(&host->lock);
    if (!taken) {
        return WEIVEL_C_STATUS_NONE;
    }
    if (began.released_activation != NULL) {
        weivel_c_host_tell_outcome(began.released_activation);
    }
    ended = began.owes_deactivate ? call_the_deactivate(plugin) : weivel_c_host_go_on_releasing(plugin);
    if (finished != NULL) {
        *finished = (uint32_t)ended;
    }
    return WEIVEL_C_STATUS_OK;
}

/**
 * @implNote Steps 4 to 6 are always deferred behind the events the core's release queued, wherever it was made, and
 * are queued in the release's own hold of the layer lock, so nothing another thread's call drains after it is heard
 * by the plugin ahead of them. With no dispatch running this thread becomes the dispatcher, so they have run, after
 * those events, before it returns, and inside a dispatch they wait their turn there. A release the core did not take
 * queued no events of its own, so its steps are then deferred, or run at once once the core is gone.
 */
int weivel_c_host_go_on_releasing(host_plugin *plugin) {
    weivel_c_host *host = plugin->host;
    uint32_t here = 0;
    weivel_c_answered_step then = {NULL, NULL, NULL, NULL, &plugin->finishing, &here};
    weivel_c_status released = weivel_c_core_release_plugin_then(host->core, plugin->handle, NULL, &then);
    weivel_c_host_report_call(host, released, "the core did not release a plugin the host released");
    if (released == WEIVEL_C_STATUS_OK) {
        return (int)here;
    }
    if (weivel_c_core_defer(host->core, &plugin->finishing, &here) != WEIVEL_C_STATUS_OK) {
        weivel_c_host_finish_release(plugin, host->core);
        return 1;
    }
    return (int)here;
}

/** The forget's answered step: detaches the context under the layer lock, before any later call can reach the core. */
static weivel_c_status detach_the_context(void *data) {
    return weivel_c_context_detach(data);
}

/**
 * Forgets the plugin in the core and detaches its context in the same hold of the layer lock, so a call through the
 * context either reached the core before the forget or finds it detached.
 */
static void forget_and_detach(host_plugin *plugin) {
    weivel_c_host *host = plugin->host;
    weivel_c_answered_step then = {detach_the_context, plugin->context, NULL, NULL, NULL, NULL};
    weivel_c_status forgotten = weivel_c_core_forget_plugin_then(host->core, plugin->handle, NULL, &then);
    weivel_c_host_report_call(host, forgotten, "the core did not forget a plugin the host released");
    if (forgotten != WEIVEL_C_STATUS_OK) {
        weivel_c_context_detach(plugin->context);
    }
}

weivel_c_status weivel_c_host_finish_release(void *data, weivel_c_core *core) {
    host_plugin *plugin = data;
    weivel_c_host *host = plugin->host;
    int destroy_may_end;
    (void)core;
    weivel_c_context_end_entries(plugin->context);
    forget_and_detach(plugin);
    weivel_c_mutex_lock(&host->lock);
    plugin->state = 0;
    host->releases_in_flight--;
    destroy_may_end = weivel_c_host_destroy_may_end(host);
    weivel_c_mutex_unlock(&host->lock);
    if (destroy_may_end) {
        weivel_c_host_end_the_destroy(host);
    }
    return WEIVEL_C_STATUS_OK;
}

weivel_c_status weivel_c_host_release(weivel_c_host *host, weivel_handle plugin, uint32_t *finished) {
    host_plugin *found;
    if (finished != NULL) {
        *finished = 0;
    }
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
    return weivel_c_host_begin_release(found, finished);
}
