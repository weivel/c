#include "host.h"
#include "libraries.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "../context/contexts.h"
#include "../core/calls.h"

#define END_OF(type, field) (offsetof(type, field) + sizeof(((type *)0)->field))
#define FIRST_ENTRY_SIZE END_OF(weivel_c_entry, library)

int weivel_c_host_is_running(weivel_c_host *host) {
    int running;
    weivel_c_mutex_lock(&host->lock);
    running = host->state == HOST_RUNNING;
    weivel_c_mutex_unlock(&host->lock);
    return running;
}

host_plugin *weivel_c_host_plugin_of(weivel_c_host *host, weivel_handle plugin) {
    host_plugin *found;
    weivel_c_mutex_lock(&host->lock);
    found = host->first_plugin;
    while (found != NULL && (found->handle != plugin || found->state == 0)) {
        found = found->next;
    }
    weivel_c_mutex_unlock(&host->lock);
    return found;
}

void weivel_c_host_report(weivel_c_host *host, uint32_t kind, weivel_c_status status, const char *text) {
    weivel_c_failure failure = WEIVEL_INIT(weivel_c_failure);
    failure.kind = kind;
    failure.status = (uint32_t)status;
    failure.event_kind = WEIVEL_EVENT_NONE;
    failure.text = text;
    weivel_c_core_report(host->core, &failure);
}

void weivel_c_host_report_call(weivel_c_host *host, weivel_c_status status, const char *text) {
    if (status != WEIVEL_C_STATUS_OK && status != WEIVEL_C_STATUS_DESTROYED) {
        weivel_c_host_report(host, WEIVEL_C_FAILURE_HOST_CALL, status, text);
    }
}

void weivel_c_host_join_opened(host_plugin *plugin) {
    weivel_c_host *host = plugin->host;
    if (plugin->opened) {
        weivel_c_host_leave_opened(plugin);
    }
    plugin->opened_previous = host->last_opened;
    plugin->opened_next = NULL;
    if (host->last_opened != NULL) {
        host->last_opened->opened_next = plugin;
    } else {
        host->first_opened = plugin;
    }
    host->last_opened = plugin;
    plugin->opened = 1;
}

void weivel_c_host_leave_opened(host_plugin *plugin) {
    weivel_c_host *host = plugin->host;
    if (plugin->opened_previous != NULL) {
        plugin->opened_previous->opened_next = plugin->opened_next;
    } else {
        host->first_opened = plugin->opened_next;
    }
    if (plugin->opened_next != NULL) {
        plugin->opened_next->opened_previous = plugin->opened_previous;
    } else {
        host->last_opened = plugin->opened_previous;
    }
    plugin->opened_previous = NULL;
    plugin->opened_next = NULL;
    plugin->opened = 0;
}

static int entry_is_usable(const weivel_c_entry *entry) {
    return entry != NULL && entry->size >= FIRST_ENTRY_SIZE && entry->activate != NULL;
}

static void fill_plugin(host_plugin *plugin, weivel_c_host *host, weivel_manifest *manifest,
                        const weivel_c_entry *entry) {
    plugin->host = host;
    plugin->manifest = manifest;
    memcpy(&plugin->entry, entry, entry->size < sizeof plugin->entry ? entry->size : sizeof plugin->entry);
    plugin->entry.size = (uint32_t)sizeof plugin->entry;
    plugin->state = WEIVEL_C_PLUGIN_REGISTERED;
    weivel_c_host_ready_completion(&plugin->deactivating, plugin, COMPLETION_OF_A_DEACTIVATE);
    plugin->finishing = (weivel_c_step)WEIVEL_INIT(weivel_c_step);
    plugin->finishing.run = weivel_c_host_finish_release;
    plugin->finishing.data = plugin;
    weivel_c_host_ready_ending(&plugin->verify_ending, plugin);
}

/** Keeps a registered plugin in the host's tables, unless the destroy has begun. Answers 0, keeping nothing, then. */
static int keep_plugin(host_plugin *plugin) {
    weivel_c_host *host = plugin->host;
    int running;
    weivel_c_mutex_lock(&host->lock);
    running = host->state == HOST_RUNNING;
    if (running && host->last_plugin != NULL) {
        host->last_plugin->next = plugin;
    } else if (running) {
        host->first_plugin = plugin;
    }
    if (running) {
        host->last_plugin = plugin;
    }
    weivel_c_mutex_unlock(&host->lock);
    return running;
}

/**
 * Releases and forgets in the core a plugin the host registered and cannot keep, so no plugin of the core's is left
 * that the host holds no row for.
 */
static void undo_the_registration(weivel_c_host *host, weivel_handle plugin) {
    weivel_c_host_report_call(host, weivel_c_core_release_plugin(host->core, plugin, NULL),
                              "the core did not release a plugin whose registration the host undid");
    weivel_c_host_report_call(host, weivel_c_core_forget_plugin(host->core, plugin, NULL),
                              "the core did not forget a plugin whose registration the host undid");
}

/**
 * Forgets in the core, quietly, a plugin registered while the destroy began, and detaches its context, which the
 * contexts free with the host. The entry's library stays its caller's.
 */
static void forget_unkept(weivel_c_host *host, host_plugin *made) {
    weivel_c_core_forget_plugin(host->core, made->handle, NULL);
    weivel_c_context_detach(made->context);
    free(made->manifest);
    free(made);
}

/**
 * @implNote The plugin's context is made and its entry reserved before the core is asked, and kept by the
 * registration's answered step, though the plugin is handed it only by its activate: so the core never holds a plugin
 * the table lacks an entry for, and a registration short of memory reaches nothing of the core.
 */
static weivel_c_status register_into(weivel_c_host *host, host_plugin *made, const weivel_manifest *given,
                                     const weivel_manifest *copy, weivel_c_refusal *refusal) {
    const weivel_manifest *registered = weivel_c_host_manifest_is_whole(given) ? copy : given;
    weivel_c_context_keeping keeping = {NULL, &made->handle, 0};
    weivel_c_answered_step then = {weivel_c_context_keep_answered, &keeping, NULL, NULL, NULL, NULL};
    weivel_c_status status = weivel_c_context_prepare(host->contexts, copy, &keeping.context);
    if (status != WEIVEL_C_STATUS_OK) {
        return status;
    }
    status = weivel_c_core_register_manifest_then(host->core, registered, &made->handle, refusal, &then);
    if (!keeping.kept) {
        weivel_c_context_discard_prepared(keeping.context);
    }
    if (status != WEIVEL_C_STATUS_OK && made->handle != 0) {
        undo_the_registration(host, made->handle);
    }
    made->context = keeping.kept ? keeping.context : NULL;
    return status;
}

weivel_c_status weivel_c_host_register(weivel_c_host *host, const weivel_manifest *manifest,
                                       const weivel_c_entry *entry, weivel_handle *plugin, weivel_c_refusal *refusal) {
    host_plugin *made;
    weivel_manifest *copy;
    weivel_c_status status;
    if (plugin == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    *plugin = 0;
    if (host == NULL || manifest == NULL || !entry_is_usable(entry)) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    if (!weivel_c_host_is_running(host)) {
        return WEIVEL_C_STATUS_DESTROYED;
    }
    made = calloc(1, sizeof *made);
    copy = made == NULL ? NULL : weivel_c_host_copy_manifest(manifest);
    if (copy == NULL) {
        free(made);
        return WEIVEL_C_STATUS_OUT_OF_MEMORY;
    }
    status = register_into(host, made, manifest, copy, refusal);
    if (status != WEIVEL_C_STATUS_OK) {
        free(copy);
        free(made);
        return status;
    }
    fill_plugin(made, host, copy, entry);
    if (!keep_plugin(made)) {
        forget_unkept(host, made);
        return WEIVEL_C_STATUS_DESTROYED;
    }
    *plugin = made->handle;
    return WEIVEL_C_STATUS_OK;
}

weivel_c_status weivel_c_host_plugin_state(weivel_c_host *host, weivel_handle plugin, uint32_t *state) {
    host_plugin *found;
    if (host == NULL || state == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    if (!weivel_c_host_is_running(host)) {
        return WEIVEL_C_STATUS_DESTROYED;
    }
    found = weivel_c_host_plugin_of(host, plugin);
    if (found == NULL) {
        return WEIVEL_C_STATUS_NONE;
    }
    weivel_c_mutex_lock(&host->lock);
    *state = found->state;
    weivel_c_mutex_unlock(&host->lock);
    return *state == 0 ? WEIVEL_C_STATUS_NONE : WEIVEL_C_STATUS_OK;
}

weivel_c_status weivel_c_host_context(weivel_c_host *host, weivel_c_context **context) {
    if (host == NULL || context == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    if (!weivel_c_host_is_running(host)) {
        return WEIVEL_C_STATUS_DESTROYED;
    }
    *context = host->own_context;
    return WEIVEL_C_STATUS_OK;
}

weivel_c_status weivel_c_host_core(weivel_c_host *host, weivel_c_core **core) {
    if (host == NULL || core == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    if (!weivel_c_host_is_running(host)) {
        return WEIVEL_C_STATUS_DESTROYED;
    }
    *core = host->core;
    return WEIVEL_C_STATUS_OK;
}

weivel_c_status weivel_c_host_timer(weivel_c_host *host, weivel_c_timer **timer) {
    if (host == NULL || timer == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    if (!weivel_c_host_is_running(host)) {
        return WEIVEL_C_STATUS_DESTROYED;
    }
    *timer = host->timer;
    return WEIVEL_C_STATUS_OK;
}

int weivel_c_host_holds_service_id(const weivel_c_host *host, const char *id) {
    const host_service *kept;
    for (kept = host->services; kept != NULL; kept = kept->next) {
        if ((kept->live || kept->pending) && strcmp(kept->id, id) == 0) {
            return 1;
        }
    }
    return 0;
}

/**
 * Claims `kept`'s id on the host's list, unless the destroy has begun or, when `library_ids_refused`, a live library
 * declares it. Answers what the registration answers when the claim is refused.
 */
static weivel_c_status claim_the_id(weivel_c_host *host, host_service *kept, int library_ids_refused) {
    weivel_c_status status = WEIVEL_C_STATUS_OK;
    weivel_c_mutex_lock(&host->lock);
    if (host->state != HOST_RUNNING) {
        status = WEIVEL_C_STATUS_DESTROYED;
    } else if (library_ids_refused && weivel_c_host_a_live_library_declares_held(host, kept->id)) {
        status = WEIVEL_C_STATUS_MISUSE;
    } else {
        kept->pending = 1;
        kept->next = host->services;
        host->services = kept;
    }
    weivel_c_mutex_unlock(&host->lock);
    return status;
}

/** Under the host's lock: takes a claim the core did not answer off the host's list. */
static void unlink_the_claim(weivel_c_host *host, host_service *kept) {
    host_service **link = &host->services;
    while (*link != NULL && *link != kept) {
        link = &(*link)->next;
    }
    if (*link == kept) {
        *link = kept->next;
    }
}

/** Ends a claim once the core answered: the registration stands, or the claim is dropped and freed. */
static void settle_the_claim(weivel_c_host *host, host_service *kept, weivel_c_status registered) {
    int stands = registered == WEIVEL_C_STATUS_OK;
    weivel_c_mutex_lock(&host->lock);
    kept->pending = 0;
    kept->live = (uint32_t)stands;
    if (!stands) {
        unlink_the_claim(host, kept);
    }
    weivel_c_mutex_unlock(&host->lock);
    if (!stands) {
        free(kept);
    }
}

static weivel_c_status register_claimed(weivel_c_host *host, const char *id, void *service,
                                        const weivel_c_capacity_answerer *answerer, int library_ids_refused,
                                        weivel_handle *registration, weivel_c_refusal *refusal) {
    host_service *kept;
    weivel_c_status status;
    if (host == NULL || id == NULL || id[0] == '\0' || service == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    kept = calloc(1, sizeof *kept + strlen(id));
    if (kept == NULL) {
        return WEIVEL_C_STATUS_OUT_OF_MEMORY;
    }
    memcpy(kept->id, id, strlen(id) + 1);
    status = claim_the_id(host, kept, library_ids_refused);
    if (status != WEIVEL_C_STATUS_OK) {
        free(kept);
        return status;
    }
    status = weivel_c_context_register_service(host->own_context, id, service, answerer, &kept->registration,
                                               refusal);
    if (status == WEIVEL_C_STATUS_OK && registration != NULL) {
        *registration = kept->registration;
    }
    settle_the_claim(host, kept, status);
    return status;
}

weivel_c_status weivel_c_host_register_service(weivel_c_host *host, const char *id, void *service,
                                               const weivel_c_capacity_answerer *answerer,
                                               weivel_handle *registration, weivel_c_refusal *refusal) {
    if (registration != NULL) {
        *registration = 0;
    }
    return register_claimed(host, id, service, answerer, 1, registration, refusal);
}

weivel_c_status weivel_c_host_register_library_service(weivel_c_host *host, const char *id, void *service,
                                                       weivel_handle *registration, weivel_c_refusal *refusal) {
    if (registration != NULL) {
        *registration = 0;
    }
    return register_claimed(host, id, service, NULL, 0, registration, refusal);
}

static void forget_service(weivel_c_host *host, weivel_handle registration) {
    host_service *kept;
    weivel_c_mutex_lock(&host->lock);
    for (kept = host->services; kept != NULL; kept = kept->next) {
        if (kept->live && kept->registration == registration) {
            kept->live = 0;
        }
    }
    weivel_c_mutex_unlock(&host->lock);
}

weivel_c_status weivel_c_host_unregister_service(weivel_c_host *host, weivel_handle registration,
                                                 weivel_c_refusal *refusal) {
    weivel_c_status status;
    if (host == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    if (!weivel_c_host_is_running(host)) {
        return WEIVEL_C_STATUS_DESTROYED;
    }
    status = weivel_c_context_unregister_service(host->own_context, registration, refusal);
    if (status == WEIVEL_C_STATUS_OK) {
        forget_service(host, registration);
    }
    return status;
}
