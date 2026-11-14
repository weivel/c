#ifndef WEIVEL_C_HOST_H
#define WEIVEL_C_HOST_H

/**
 * @file
 * The `host` module of the weivel C language implementation: it registers plugins written in C, activates and
 * releases them, offers host services, loads C libraries as host services (see weivel/c/host/library.h), and is
 * destroyed and freed. Every decision is the core's; the host carries
 * out its answers in the order the core's contract gives.
 *
 * - Registration takes a manifest value and an entry (see weivel/c/host/entry.h) the caller hands it. It never needs
 *   the standard: reading a manifest file and discovering plugins on disk are the `standard` module's, which feeds
 *   this registration.
 * - Activation asks the core for the plan, then starts each plugin of the plan's order in that order (opens it with
 *   its context's token as payload, builds its context, calls its activate) without waiting for any activate, and
 *   answers the plan at once. Each started plugin's outcome arrives later, exactly once, through the listener the
 *   caller gives, from the dispatch.
 * - Release calls the deactivate, releases the plugin in the core, lets every event that release queued reach it,
 *   ends its entries, forgets it and drops it from the host's tables. The last three always run as one step deferred
 *   behind the release's events; a release made where no dispatch runs has run them before it returns.
 * - Destroy releases every plugin the host opened and has not released, the last opened first, lets every event and
 *   step those releases left run, unloads every C library it loaded, disarms the timer and destroys the core.
 *   Afterwards every call on the host answers WEIVEL_C_STATUS_DESTROYED, quietly, until weivel_c_host_free.
 *
 * Threading: any thread may call any function here, an entry function and every callback included. The host has one
 * lock of its own, held only inside its own code and never while it calls anything: the wrapper, the contexts, the
 * handles table, an entry, a listener, the log or the error sink each run with no lock of the host's held. Calls into
 * the core take the layer lock as every call through the wrapper does. A call made where no dispatch runs dispatches
 * what it queued before it returns; one made while a dispatch runs, on this thread or another, leaves it to that
 * dispatch.
 */

#include <stdint.h>

#include <weivel/c/context.h>
#include <weivel/c/core.h>
#include <weivel/c/host/entry.h>
#include <weivel/c/timer.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One host. Opaque; made by weivel_c_host_create, destroyed by weivel_c_host_destroy, freed by weivel_c_host_free. */
typedef struct weivel_c_host weivel_c_host;

/** What a host is made from. Zero, or a field beyond `size`, means the default. */
typedef struct weivel_c_host_options {
    uint32_t size;
    /**
     * A loaded table of the core's (see weivel_c_core_load), copied; its binary stays loaded until the host is freed.
     * NULL to have the host load `core_path` itself.
     */
    const weivel_c_core_table *table;
    /** The core's binary, loaded and unloaded by the host, when `table` is NULL; read only during the call. */
    const char *core_path;
    /** The core's own options, handed over unchanged; NULL for every default. Read only during the call. */
    const weivel_core_options *core_options;
    /** The caller's clock and scheduler, copied; NULL for a host that arms nothing (see weivel_c_scheduler). */
    const weivel_c_scheduler *scheduler;
    /** Where failures go; a NULL `report` writes one line to standard error per failure. */
    weivel_c_error_sink error_sink;
    /** Where log goes; a NULL `write` drops every line. */
    weivel_c_log log;
} weivel_c_host_options;

/**
 * Makes a host: its core behind the wrapper, the handles table, the timer on the caller's scheduler, the contexts,
 * and the host's own context, through which its services are registered.
 *
 * @param host receives the host, owned by the caller until weivel_c_host_free. Required; NULL on every answer but OK.
 * @param failure as weivel_c_core_load fills it when the host loads `core_path`; may be NULL.
 * @return WEIVEL_C_STATUS_OK; WEIVEL_C_STATUS_MISUSE (no options or host, options smaller than their first shape,
 * neither a table nor a path, a scheduler the timer refuses, or core options the core calls malformed);
 * WEIVEL_C_STATUS_NOT_A_LIBRARY or WEIVEL_C_STATUS_MISSING_SYMBOL for a path that does not load;
 * WEIVEL_C_STATUS_OUT_OF_MEMORY. Nothing is left made or loaded on any answer but OK.
 */
WEIVEL_C_API weivel_c_status weivel_c_host_create(const weivel_c_host_options *options, weivel_c_host **host,
                                                  weivel_c_core_load_failure *failure);

/**
 * Destroys the host, in the contract's four steps: releases every plugin it opened and has not released, one whose
 * activate is in flight and one quarantined included, the last opened first, each calling its deactivate when its
 * activate was called; lets every event those releases queued and every step they left run, each forget included;
 * unloads every C library it loaded (see weivel/c/host/library.h), telling the sink of one whose binary does not
 * close and unloading the rest; then disarms the timer and destroys the core, whatever the steps before reported. The
 * libraries plugins' entries were read from stay open until weivel_c_host_free.
 *
 * It is a step deferred behind everything already queued, and its last two steps a further step behind its own
 * releases' steps. Where no dispatch runs, it has finished when it returns, unless a deactivate it waits for is still
 * pending: the layer sets no timeout, and the destroy finishes on the thread that finishes the last such deactivate.
 * Inside a dispatch, or while another thread's dispatch runs, it finishes there.
 *
 * From the moment it is called, every other call on the host answers WEIVEL_C_STATUS_DESTROYED, writing nothing to
 * the log or the sink; nothing the destroy sets off reaches the caller but the outcome `released` of an activation in
 * flight, and what a deactivate's failure tells the sink.
 *
 * @param finished receives 1 once every step has run, so weivel_c_host_free may follow, else 0; may be NULL.
 * @return WEIVEL_C_STATUS_OK, for a second destroy too, which answers as the first did and `finished` as things
 * stand; WEIVEL_C_STATUS_MISUSE for NULL.
 */
WEIVEL_C_API weivel_c_status weivel_c_host_destroy(weivel_c_host *host, uint32_t *finished);

/**
 * Frees a destroyed host: its contexts, every plugin's record and completion, the library every plugin's entry was
 * read from (see weivel_c_entry_load), telling the sink of one that does not close, its table and the core's binary
 * when the host loaded it. A context or a completion a plugin still holds is gone afterwards, and so is its code.
 *
 * @return WEIVEL_C_STATUS_OK, also for NULL; WEIVEL_C_STATUS_MISUSE, freeing nothing, while the destroy has not
 * finished, or while bytes one of its C libraries answered are still held.
 * Threading: no call on the host, on any of its contexts or completions, may be in flight or follow.
 */
WEIVEL_C_API weivel_c_status weivel_c_host_free(weivel_c_host *host);

/**
 * Registers a plugin from its manifest value and its entry.
 *
 * @param manifest borrowed, never copied: it stays valid and unchanged until weivel_c_host_free, since the plugin's
 * context answers it as the host registered it. Required.
 * @param entry copied. Required, with an activate. An entry weivel_c_entry_load read hands the host its library.
 * @param plugin receives the plugin's handle. Required; 0 on every answer but OK.
 * @param refusal as every call of the wrapper takes it; may be NULL.
 * @return WEIVEL_C_STATUS_OK; WEIVEL_C_STATUS_REFUSED with the core's refusal; WEIVEL_C_STATUS_MISUSE (a NULL
 * argument, an entry too small or without an activate, or a manifest the core calls malformed, with its refusal);
 * WEIVEL_C_STATUS_OUT_OF_MEMORY; WEIVEL_C_STATUS_DESTROYED.
 */
WEIVEL_C_API weivel_c_status weivel_c_host_register(weivel_c_host *host, const weivel_manifest *manifest,
                                                    const weivel_c_entry *entry, weivel_handle *plugin,
                                                    weivel_c_refusal *refusal);

/**
 * Registers a service of the host's own under WEIVEL_HOST: offered to every activation plan as provided by the host,
 * and answered to a plugin as the very pointer given, as any provider's is.
 *
 * @return as weivel_c_context_register_service answers; WEIVEL_C_STATUS_DESTROYED.
 */
WEIVEL_C_API weivel_c_status weivel_c_host_register_service(weivel_c_host *host, const char *id, void *service,
                                                            const weivel_c_capacity_answerer *answerer,
                                                            weivel_handle *registration, weivel_c_refusal *refusal);

/**
 * Unregisters a service of the host's own; it is offered to no later plan.
 *
 * @return as weivel_c_context_unregister_service answers; WEIVEL_C_STATUS_DESTROYED.
 */
WEIVEL_C_API weivel_c_status weivel_c_host_unregister_service(weivel_c_host *host, weivel_handle registration,
                                                              weivel_c_refusal *refusal);

/**
 * Answers the host's own context, through which the host gets, wants and watches as WEIVEL_HOST. It is the host's,
 * valid until weivel_c_host_free.
 *
 * @return WEIVEL_C_STATUS_OK; WEIVEL_C_STATUS_MISUSE; WEIVEL_C_STATUS_DESTROYED.
 */
WEIVEL_C_API weivel_c_status weivel_c_host_context(weivel_c_host *host, weivel_c_context **context);

/**
 * Answers the host's core behind the wrapper, so its caller can read the ledger. The host's: the caller destroys and
 * frees nothing of it.
 *
 * @return WEIVEL_C_STATUS_OK; WEIVEL_C_STATUS_MISUSE; WEIVEL_C_STATUS_DESTROYED.
 */
WEIVEL_C_API weivel_c_status weivel_c_host_core(weivel_c_host *host, weivel_c_core **core);

/** How one started plugin's activation came out. */
typedef enum weivel_c_outcome_kind {
    /** Its activate finished and the core verified it. */
    WEIVEL_C_OUTCOME_ACTIVE = 1,
    /** Its activate failed, or the core refused to verify it; it is quarantined. */
    WEIVEL_C_OUTCOME_QUARANTINED = 2,
    /** The core refused to open it, so it was never opened. */
    WEIVEL_C_OUTCOME_REFUSED = 3,
    /** It was released while its activate was in flight; answered as that release begins. */
    WEIVEL_C_OUTCOME_RELEASED = 4,
    /** Its open failed for a reason other than the core's refusal, a defect of the layer the sink was told. */
    WEIVEL_C_OUTCOME_FAILED = 5
} weivel_c_outcome_kind;

/** One plugin's outcome, as the outcome listener is handed it. Everything in it lives for the length of that call. */
typedef struct weivel_c_outcome {
    uint32_t size;
    weivel_handle plugin;
    /** Its import name, as its manifest gives it. */
    const char *import_name;
    /** A weivel_c_outcome_kind. */
    uint32_t kind;
    /**
     * For REFUSED, the core's refusal of the open. For QUARANTINED, the core's refusal of the verification, or, when
     * the activate failed, the reason the host quarantined it with, code WEIVEL_REFUSAL_NONE, saying so and with a
     * fix. Otherwise empty. Never released by the listener.
     */
    weivel_c_refusal refusal;
} weivel_c_outcome;

/**
 * What hears each started plugin's outcome: `hear` and the `data` it is called with, once per plugin, from the
 * dispatch. A status other than WEIVEL_C_STATUS_OK goes to the error sink.
 *
 * @implNote No `size`: it is embedded by value, so the struct embedding it is what grows.
 */
typedef struct weivel_c_outcome_listener {
    weivel_c_status (*hear)(void *data, const weivel_c_outcome *outcome);
    void *data;
} weivel_c_outcome_listener;

/** What one activation is asked for. */
typedef struct weivel_c_activation {
    uint32_t size;
    /** The plugins, as weivel_c_host_register answered them; read only during the call. */
    const weivel_handle *plugins;
    uint32_t count;
    /** A weivel_edge_source: which declarations order the plan. The core sets no default, and neither does the host. */
    uint32_t edge_source;
    /** Required: `hear` must be given. */
    weivel_c_outcome_listener listener;
} weivel_c_activation;

/**
 * Plans the activation of plugins with the core and starts each plugin of the plan's order in that order, never
 * waiting for one activate before starting the next, as the host's description says. A plugin whose activate answers
 * at once is verified, or quarantined, before the next is started; its outcome, like every other, arrives through the
 * listener.
 *
 * @param plan the caller's struct with `size` set and nothing held, as WEIVEL_INIT leaves it; on OK it holds the
 * core's plan, its order the order the activates were started in, and is handed to weivel_c_host_plan_release.
 * @return WEIVEL_C_STATUS_OK once every activate of the plan has been started, whatever any of them answered;
 * WEIVEL_C_STATUS_MISUSE (a NULL argument, options too small, no listener, or a plan the core calls malformed, an
 * edge source that is none among them, with its refusal); WEIVEL_C_STATUS_OUT_OF_MEMORY with nothing started;
 * WEIVEL_C_STATUS_DESTROYED.
 */
WEIVEL_C_API weivel_c_status weivel_c_host_activate(weivel_c_host *host, const weivel_c_activation *activation,
                                                    weivel_activation_plan *plan, weivel_c_refusal *refusal);

/**
 * Frees a plan weivel_c_host_activate filled. It needs no core, so it may follow the host's destroy.
 *
 * @return WEIVEL_C_STATUS_OK or WEIVEL_C_STATUS_MISUSE.
 */
WEIVEL_C_API weivel_c_status weivel_c_host_plan_release(weivel_c_host *host, weivel_activation_plan *plan);

/**
 * Verifies a plugin's activation with the core, and quarantines it in the same step, with no reason of the host's,
 * when the core refuses.
 *
 * @return WEIVEL_C_STATUS_OK, verified; WEIVEL_C_STATUS_REFUSED with the core's refusal, the plugin quarantined;
 * WEIVEL_C_STATUS_NONE for a plugin the host holds no row for; WEIVEL_C_STATUS_DESTROYED, quietly.
 */
WEIVEL_C_API weivel_c_status weivel_c_host_verify(weivel_c_host *host, weivel_handle plugin,
                                                  weivel_c_refusal *refusal);

/**
 * Releases a plugin the host opened, in the contract's six steps. A release made while its activate is in flight
 * answers that activation `released` as it begins. A deactivate that answers pending holds the release, its core
 * release and every step after it waiting until it finishes, for as long as that takes.
 *
 * @param finished receives 1 when every step has run before this returned, so the plugin is forgotten, and 0 when
 * the last steps wait: on a pending deactivate, or behind the events of a dispatch running here or on another thread.
 * May be NULL.
 * @return WEIVEL_C_STATUS_OK once the release has begun; WEIVEL_C_STATUS_NONE for a plugin the host has not opened or
 * has released already; WEIVEL_C_STATUS_DESTROYED.
 */
WEIVEL_C_API weivel_c_status weivel_c_host_release(weivel_c_host *host, weivel_handle plugin, uint32_t *finished);

/** Where a plugin the host registered stands, as the host sees it. */
typedef enum weivel_c_plugin_state {
    /** Registered, and not opened, or the core refused to open it. */
    WEIVEL_C_PLUGIN_REGISTERED = 1,
    /** Opened, its activate in flight or not yet verified. */
    WEIVEL_C_PLUGIN_ACTIVATING = 2,
    /** Verified. */
    WEIVEL_C_PLUGIN_ACTIVE = 3,
    /** Quarantined, until it is activated again. */
    WEIVEL_C_PLUGIN_QUARANTINED = 4,
    /** Its release has begun and not finished. */
    WEIVEL_C_PLUGIN_RELEASING = 5
} weivel_c_plugin_state;

/**
 * Answers where a plugin stands.
 *
 * @param state receives a weivel_c_plugin_state. Required.
 * @return WEIVEL_C_STATUS_OK; WEIVEL_C_STATUS_NONE for a plugin forgotten, or never registered with this host;
 * WEIVEL_C_STATUS_MISUSE; WEIVEL_C_STATUS_DESTROYED.
 */
WEIVEL_C_API weivel_c_status weivel_c_host_plugin_state(weivel_c_host *host, weivel_handle plugin, uint32_t *state);

#ifdef __cplusplus
}
#endif

#endif
