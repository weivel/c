#ifndef WEIVEL_C_CONTEXT_H
#define WEIVEL_C_CONTEXT_H

/**
 * @file
 * The `context` module of the weivel C language implementation: what a plugin sees of the core, and the only way it
 * reaches any of it. A weivel_c_context is one plugin's view; a weivel_c_contexts is what every context of one host
 * shares, and it is the host's to make.
 *
 * A context decides nothing. Reach, scope, settlement, revocation and order are the core's: every call here makes the
 * core's call of the same meaning through the wrapper and answers what the core answered, and nothing the core
 * answers is kept to answer a later call with.
 *
 * - A service is a `void *`. A get, a settled want and a watch hearing a registration each answer the very pointer the
 *   provider registered, looked up from the registration handle the core answered. The layer never reads, copies,
 *   wraps or frees it, so calling through it is a plain call in C that crosses into nothing of the layer's or the
 *   core's.
 * - What comes later comes through a callback the plugin gives: a want's settlement, a watch's changes, a capacity
 *   question, and what the core reports about the plugin. Every callback answers a weivel_c_status, and anything but
 *   WEIVEL_C_STATUS_OK is a failure of the plugin's code: it is reported to the core's error sink, every other
 *   callback still hears each change in order, and it is never a refusal and never told to the core.
 * - Everything a callback is handed, each text included, lives for the length of that call.
 * - Once the plugin is released or quarantined its callbacks still hear the events that call itself queued, its wants
 *   rejected WEIVEL_REFUSAL_STOPPED among them. Its entries end only when the host says so, with
 *   weivel_c_context_end_entries. A call the plugin still makes reaches the core as a late call and is answered as
 *   the core answers it.
 *
 * Threading, for every call here unless it says otherwise: any thread, a callback included. A call takes the layer
 * lock for the core's call and its drain, as every call through the wrapper does, then the lock of the contexts, then
 * the lock of the handles table, each only inside the layer's own code. No callback of the plugin's or of the host's
 * is ever called under any of the three: every one runs from the dispatch, outside them all, so each may call back
 * into its context. A call made while no dispatch runs dispatches its own events before it returns, so a want that
 * settles at once has called its callback before weivel_c_context_want_service returns.
 */

#include <stdint.h>

#include <weivel/c/core.h>
#include <weivel/c/handles.h>
#include <weivel/c/timer.h>

#ifdef __cplusplus
extern "C" {
#endif

/** What every context of one host shares. Opaque; made by weivel_c_contexts_create, freed by weivel_c_contexts_free. */
typedef struct weivel_c_contexts weivel_c_contexts;

/** One plugin's view of the core. Opaque; made by weivel_c_context_create and freed by weivel_c_context_free. */
typedef struct weivel_c_context weivel_c_context;

/** One line of log, as the host's log is handed it. */
typedef struct weivel_c_log_record {
    uint32_t size;
    /** A weivel_log_level. */
    uint32_t level;
    /** The plugin that logged, or 0 for a log of the core's own. */
    weivel_handle plugin;
    /** That plugin's import name, as its manifest gives it; NULL for the core's own log and for the host's context. */
    const char *plugin_name;
    /** The line, UTF-8. Never NULL. */
    const char *text;
} weivel_c_log_record;

/**
 * Where log goes: `write` and the `data` it is called with. It is the host's to direct, so a NULL `write` drops every
 * line and the layer writes none anywhere itself. It is called outside every lock of the layer's. A status other than
 * WEIVEL_C_STATUS_OK goes to the error sink.
 *
 * @implNote No `size`: it is embedded by value, so the struct embedding it is what grows.
 */
typedef struct weivel_c_log {
    weivel_c_status (*write)(void *data, const weivel_c_log_record *record);
    void *data;
} weivel_c_log;

/** What the contexts of a host are made from. Zero, or a field beyond `size`, means the default. */
typedef struct weivel_c_contexts_options {
    uint32_t size;
    /** The core every context calls. Required; it outlives the contexts. */
    weivel_c_core *core;
    /**
     * The table the contexts keep their objects in. Required; it outlives the contexts. Every entry in it that is
     * not a plugin's is the contexts' own, so nothing else adds a registration, an implementation, a watch or a want
     * to it.
     */
    weivel_c_handles *handles;
    /**
     * Where a want that needs the time takes it. NULL, like a timer made without a scheduler, leaves the host without
     * a clock, and such a want is then the caller's mistake.
     */
    weivel_c_timer *timer;
    /** The host's log. */
    weivel_c_log log;
} weivel_c_contexts_options;

/**
 * Makes what the contexts of one host share, and adds the one listener through which every context hears the core:
 * an event is routed to the plugin it names through the handles table, so a host with many plugins still has one
 * listener.
 *
 * @param contexts receives it, owned by the caller until weivel_c_contexts_free. Required; NULL on every answer but
 * OK.
 * @return WEIVEL_C_STATUS_OK; WEIVEL_C_STATUS_MISUSE (a NULL argument, options smaller than their first shape, no
 * `core` or no `handles`); WEIVEL_C_STATUS_OUT_OF_MEMORY; WEIVEL_C_STATUS_DESTROYED for a destroyed core.
 */
WEIVEL_C_API weivel_c_status weivel_c_contexts_create(const weivel_c_contexts_options *options,
                                                      weivel_c_contexts **contexts);

/**
 * Frees the contexts, every context still made from them, and every record any of them still keeps, ending each one's
 * entry and every holding of their plugins in the handles table, so the table is left with nothing of theirs. The core
 * must be destroyed first: only then is no event, no step and no callback of the contexts' left to run.
 *
 * @return WEIVEL_C_STATUS_OK, also for NULL; WEIVEL_C_STATUS_MISUSE, freeing nothing, while the core is not
 * destroyed.
 * Threading: no call on the contexts, on any context of theirs or on their core may be in flight or follow.
 */
WEIVEL_C_API weivel_c_status weivel_c_contexts_free(weivel_c_contexts *contexts);

/**
 * Answers how many contexts there are, and how many records they keep between them: one per registration,
 * implementation, watch and want that has not ended.
 *
 * @param contexts_made receives the contexts; may be NULL.
 * @param records receives the records; may be NULL.
 * @return WEIVEL_C_STATUS_OK or WEIVEL_C_STATUS_MISUSE.
 */
WEIVEL_C_API weivel_c_status weivel_c_contexts_count(weivel_c_contexts *contexts, uint32_t *contexts_made,
                                                     uint32_t *records);

/**
 * Builds the context of a plugin the host is about to open, and makes the plugin's entry in the handles table, whose
 * object is the context. The host opens the plugin with weivel_c_context_token as its payload.
 *
 * WEIVEL_HOST may be given as the plugin: that context is the host's own, through which it registers its services
 * and is answered and revoked objects as a plugin is.
 *
 * @param plugin the handle the core answered the plugin's registration, or WEIVEL_HOST.
 * @param manifest the plugin's manifest value as the host registered it; NULL when there is none to show. It is
 * borrowed, never copied: it stays valid and unchanged until the context is freed.
 * @param context receives the context, owned by the caller until weivel_c_context_free. Required; NULL on every
 * answer but OK.
 * @return WEIVEL_C_STATUS_OK; WEIVEL_C_STATUS_MISUSE (a NULL argument, a plugin of 0, or a plugin the handles table
 * already holds); WEIVEL_C_STATUS_OUT_OF_MEMORY.
 *
 * @implNote The manifest is borrowed because the host keeps the registered value anyway: a plugin is registered in
 * one call and opened in another, and the core's own copy cannot be read back. A second copy here would be the same
 * value twice, and only one of the two could be "the manifest as the host registered it".
 */
WEIVEL_C_API weivel_c_status weivel_c_context_create(weivel_c_contexts *contexts, weivel_handle plugin,
                                                     const weivel_manifest *manifest, weivel_c_context **context);

/**
 * Answers the token of the plugin's entry in the handles table: what the host opens the plugin with as its payload,
 * so the payload the core answers for the plugin leads to its context without a search.
 *
 * @return WEIVEL_C_STATUS_OK or WEIVEL_C_STATUS_MISUSE.
 */
WEIVEL_C_API weivel_c_status weivel_c_context_token(weivel_c_context *context, uint64_t *token);

/**
 * Ends the plugin's own entries: its registrations, implementations, watches, wants, answerers and listeners are
 * dropped from the layer's tables, and nothing is called in the core, which has ended them already. It is the step
 * the host takes once the events its release or quarantine call queued have been dispatched, and not before. What the
 * plugin holds of others is not an entry and goes with the revocations the core queues. A callback of the plugin's
 * may call it: what that callback was handed stays until it returns.
 *
 * The context stays usable: a call made through it afterwards is a late call the core answers, and a want made then
 * is still settled to its callback. A plugin opened again after a quarantine goes on with the same context.
 *
 * @return WEIVEL_C_STATUS_OK or WEIVEL_C_STATUS_MISUSE.
 * Threading: only from the dispatch, which is where a step given to weivel_c_core_defer runs, or once the core is
 * destroyed. No callback of the plugin's is then running on another thread, so what it was handed is not freed under
 * it.
 */
WEIVEL_C_API weivel_c_status weivel_c_context_end_entries(weivel_c_context *context);

/**
 * Detaches a context from its plugin once the core has forgotten the plugin: the plugin's entry and every holding of
 * its leave the handles table, so the core may issue its handle to a plugin registered later, and from then on every
 * call through the context that would reach the core answers WEIVEL_C_STATUS_DETACHED, with no refusal, and asks the
 * core nothing, because the handle it would carry may by then name another plugin. Whether a call is detached is
 * decided under the layer lock as the call is made, so a host that detaches the context in its forget's own hold of
 * that lock, as the layer's host does, leaves no call that passed the check to reach the core after the forget. An
 * event drained before the context was made never reaches its listeners, since it names whichever plugin held the
 * handle then. Its log, its manifest and its listeners stay as they were. The context's memory stays until it is
 * freed, so a plugin that calls it after its host forgot it reads nothing freed.
 *
 * @return WEIVEL_C_STATUS_OK, also for a context detached already; WEIVEL_C_STATUS_MISUSE.
 */
WEIVEL_C_API weivel_c_status weivel_c_context_detach(weivel_c_context *context);

/**
 * Frees a context, every record it still keeps, the plugin's entry in the handles table and every holding of the
 * plugin's. A record a callback is still being handed, or whose want is being settled, is freed by the dispatch once
 * that callback returns, so a callback may free its own context; a plugin listener of the context may not.
 *
 * @return WEIVEL_C_STATUS_OK, also for NULL.
 * Threading: as weivel_c_context_end_entries; and no call on the context may be in flight or follow.
 */
WEIVEL_C_API weivel_c_status weivel_c_context_free(weivel_c_context *context);

/**
 * Answers the plugin's manifest value, as the host registered it: the very value given to weivel_c_context_create.
 *
 * @return WEIVEL_C_STATUS_OK; WEIVEL_C_STATUS_NONE for a context made without one; WEIVEL_C_STATUS_MISUSE.
 */
WEIVEL_C_API weivel_c_status weivel_c_context_manifest(weivel_c_context *context, const weivel_manifest **manifest);

/**
 * Writes one line to the host's log, with the plugin named.
 *
 * @param level a weivel_log_level.
 * @param text the line. Required; read only during the call.
 * @return WEIVEL_C_STATUS_OK, whatever the host's log answers, since its failure goes to the error sink;
 * WEIVEL_C_STATUS_MISUSE.
 */
WEIVEL_C_API weivel_c_status weivel_c_context_log(weivel_c_context *context, uint32_t level, const char *text);

/**
 * Answers the service registered under `id` now: the provider's own pointer.
 *
 * @param service receives the pointer; NULL on every answer but OK. Required.
 * @param refusal as every call of the wrapper takes it; may be NULL.
 * @return WEIVEL_C_STATUS_OK; WEIVEL_C_STATUS_NONE when nothing is registered under `id`, which is absence and not a
 * refusal; WEIVEL_C_STATUS_REFUSED with the core's refusal, of reach among them; WEIVEL_C_STATUS_FAILED when the core
 * answered a registration the layer holds no object for, one made around the contexts; and what the wrapper answers
 * for any call.
 */
WEIVEL_C_API weivel_c_status weivel_c_context_get_service(weivel_c_context *context, const char *id, void **service,
                                                          weivel_c_refusal *refusal);

/** How a want ended, as its callback is handed it. */
typedef struct weivel_c_settlement {
    uint32_t size;
    /** The want's handle, as weivel_c_context_want_service answers it. */
    weivel_handle want;
    /** The service id asked for. */
    const char *id;
    /**
     * The provider's own pointer, or NULL for a rejected want. NULL with a `refusal` of code WEIVEL_REFUSAL_NONE is a
     * want the core settled with a registration the layer holds no object for, which the error sink was told.
     */
    void *service;
    /**
     * Why a rejected want was rejected: the core's code, detail, fix, subject and has_fix. Its texts are the event's
     * and it is never released. For a degraded want `subject` is the constraint it accepted unmet.
     */
    weivel_c_refusal refusal;
    /** 1 when a constrained want accepted a provider that never met its constraints, else 0. */
    uint32_t degraded;
} weivel_c_settlement;

/**
 * What a want settles to: `settled` and the `data` it is called with, exactly once for a want the core answered,
 * from the dispatch.
 *
 * @implNote No `size`, as weivel_c_log has none.
 */
typedef struct weivel_c_want_callback {
    weivel_c_status (*settled)(void *data, const weivel_c_settlement *settlement);
    void *data;
} weivel_c_want_callback;

/**
 * How long a want may wait, each field handed to the core unchanged under the same name (see weivel_want_options).
 * Zero, or a field beyond `size`, means the default: no deadline at all.
 */
typedef struct weivel_c_want_options {
    uint32_t size;
    /** Whether the want has a deadline, a weivel_choice. */
    uint32_t timed;
    int64_t timeout_millis;
    /** Whether a constrained want has a budget, a weivel_choice. */
    uint32_t budgeted;
    int64_t budget_millis;
    /** How often a constrained want polls; the core requires it of one. */
    int64_t poll_millis;
} weivel_c_want_options;

/**
 * Asks for the service under `id`, now or whenever it arrives. The want settles exactly once, when the core settles
 * it, through `callback`: with the provider's pointer or with the core's refusal. One that can be settled or must be
 * rejected at once settles through the callback too.
 *
 * Without options it waits for as long as it takes. Options that name a time (`timed` or `budgeted` set to
 * WEIVEL_CHOICE_YES, or a `poll_millis` above 0) need the host's clock for the core's `now`: on a host without one
 * the call answers WEIVEL_C_STATUS_MISUSE and nothing is asked of the core.
 *
 * @param options NULL for no deadline; read only during the call.
 * @param callback `settled` is required; copied.
 * @param want receives the want's handle, which weivel_c_context_withdraw_want takes; may be NULL.
 * @return WEIVEL_C_STATUS_OK, for a want rejected at once too; any other answer made no want, and the callback is
 * never called for it: WEIVEL_C_STATUS_REFUSED with the core's refusal, WEIVEL_C_STATUS_MISUSE (a NULL argument, an
 * empty id, options too small, no clock, or options the core calls malformed, then with its refusal),
 * WEIVEL_C_STATUS_OUT_OF_MEMORY, and what the wrapper answers for any call.
 */
WEIVEL_C_API weivel_c_status weivel_c_context_want_service(weivel_c_context *context, const char *id,
                                                           const weivel_c_want_options *options,
                                                           const weivel_c_want_callback *callback,
                                                           weivel_handle *want, weivel_c_refusal *refusal);

/**
 * Withdraws a want that has not settled; its callback is then never called. A want the core had already settled,
 * its settlement still waiting to be dispatched, is not withdrawn: the core refuses the withdrawal, which is answered
 * as the core answered it, and the settlement still reaches the callback.
 *
 * @return WEIVEL_C_STATUS_OK; WEIVEL_C_STATUS_NONE, with nothing asked of the core, for a handle that names no want
 * of this plugin's that still waits; and what the core answers the withdrawal.
 */
WEIVEL_C_API weivel_c_status weivel_c_context_withdraw_want(weivel_c_context *context, weivel_handle want,
                                                            weivel_c_refusal *refusal);

/** One change a watch of a service heard. */
typedef struct weivel_c_service_change {
    uint32_t size;
    /** The watch's handle. */
    weivel_handle watch;
    /** The service id watched. */
    const char *id;
    /** A weivel_change. */
    uint32_t change;
    /** The provider's own pointer for WEIVEL_CHANGE_REGISTERED, and NULL for WEIVEL_CHANGE_UNREGISTERED. */
    void *service;
} weivel_c_service_change;

/**
 * What hears a watched service: `hear` and the `data` it is called with, once per change, from the dispatch.
 *
 * @implNote No `size`, as weivel_c_log has none.
 */
typedef struct weivel_c_service_watcher {
    weivel_c_status (*hear)(void *data, const weivel_c_service_change *change);
    void *data;
} weivel_c_service_watcher;

/**
 * Watches `id`: every later registration and unregistration the core reports for it is heard, the provider's pointer
 * on the one and nothing on the other, until the plugin stops watching or stops.
 *
 * @param watcher `hear` is required; copied.
 * @param watch receives the watch's handle, which weivel_c_context_stop_watching takes; may be NULL.
 * @return WEIVEL_C_STATUS_OK; WEIVEL_C_STATUS_REFUSED as the core refuses it, with its refusal when it queued one;
 * WEIVEL_C_STATUS_MISUSE; WEIVEL_C_STATUS_OUT_OF_MEMORY; and what the wrapper answers for any call.
 */
WEIVEL_C_API weivel_c_status weivel_c_context_watch_service(weivel_c_context *context, const char *id,
                                                            const weivel_c_service_watcher *watcher,
                                                            weivel_handle *watch, weivel_c_refusal *refusal);

/**
 * Stops a watch of a service or of a capability. Its watcher is taken for no change once this returns; the one call
 * another thread's dispatch had already begun may still be running.
 *
 * @return WEIVEL_C_STATUS_OK; WEIVEL_C_STATUS_NONE, with nothing asked of the core, for a handle that names no watch
 * of this plugin's; and what the core answers.
 */
WEIVEL_C_API weivel_c_status weivel_c_context_stop_watching(weivel_c_context *context, weivel_handle watch,
                                                            weivel_c_refusal *refusal);

/**
 * Lists the ids the plugin may reach that are registered now, as the core answers them.
 *
 * @param list an empty list of the core's, as WEIVEL_INIT leaves it; on OK it is handed to
 * weivel_c_context_service_ids_release.
 * @return as the wrapper answers the core's call.
 */
WEIVEL_C_API weivel_c_status weivel_c_context_service_ids(weivel_c_context *context, weivel_service_list *list,
                                                          weivel_c_refusal *refusal);

/** Frees what weivel_c_context_service_ids filled. Answers WEIVEL_C_STATUS_OK or WEIVEL_C_STATUS_MISUSE. */
WEIVEL_C_API weivel_c_status weivel_c_context_service_ids_release(weivel_c_context *context,
                                                                  weivel_service_list *list);

/** One question a constrained want puts to a provider (see weivel_capacity_event). */
typedef struct weivel_c_capacity_question {
    uint32_t size;
    /** A weivel_capacity_question. */
    uint32_t question;
    /** The service id the provider registered. */
    const char *id;
    /** The consumer's constraints, empty for an unconstrained ask, each element at the layer's own stride. */
    weivel_numbers constraints;
} weivel_c_capacity_question;

/** A provider's answer to one question. As the layer hands it over it says nothing live and that it does not serve. */
typedef struct weivel_c_capacity_answer {
    uint32_t size;
    /**
     * For WEIVEL_CAPACITY_ATTRIBUTES: what the provider offers now. The layer reads them once `answer` has returned,
     * so they are memory of the provider's that outlives that return, never the callback's own stack.
     */
    weivel_numbers attributes;
    /** For WEIVEL_CAPACITY_SERVE: whether it serves, a weivel_choice; anything but WEIVEL_CHOICE_YES is a no. */
    uint32_t serves;
} weivel_c_capacity_answer;

/**
 * What answers a constrained want's questions about a registration: `answer` and the `data` it is called with, from
 * the dispatch. A status other than WEIVEL_C_STATUS_OK goes to the error sink and leaves the question unanswered,
 * which the core reads as not served for now.
 *
 * @implNote No `size`, as weivel_c_log has none.
 */
typedef struct weivel_c_capacity_answerer {
    weivel_c_status (*answer)(void *data, const weivel_c_capacity_question *question,
                              weivel_c_capacity_answer *answer);
    void *data;
} weivel_c_capacity_answerer;

/**
 * Registers `service` under `id`, until the plugin unregisters it or stops.
 *
 * Capacity answering is opt-in here. With an answerer, each question a constrained want asks about this registration
 * is put to it and its answer reported to the core. Without one every question is answered at once: nothing live, so
 * what the manifest advertises stands, and that it serves. A question whose event arrived `incomplete` is left
 * unanswered either way.
 *
 * @param service the provider's object, answered to consumers as this very pointer. Required.
 * @param answerer NULL for none; copied.
 * @param registration receives the registration's handle, which weivel_c_context_unregister_service takes; may be
 * NULL.
 * @return WEIVEL_C_STATUS_OK; WEIVEL_C_STATUS_REFUSED as the core refuses it, with its refusal when it queued one;
 * WEIVEL_C_STATUS_MISUSE; WEIVEL_C_STATUS_OUT_OF_MEMORY; and what the wrapper answers for any call.
 */
WEIVEL_C_API weivel_c_status weivel_c_context_register_service(weivel_c_context *context, const char *id,
                                                               void *service,
                                                               const weivel_c_capacity_answerer *answerer,
                                                               weivel_handle *registration,
                                                               weivel_c_refusal *refusal);

/**
 * Unregisters a service the plugin registered.
 *
 * @return WEIVEL_C_STATUS_OK; WEIVEL_C_STATUS_NONE, with nothing asked of the core, for a handle that names no
 * registration of this plugin's; and what the core answers.
 */
WEIVEL_C_API weivel_c_status weivel_c_context_unregister_service(weivel_c_context *context,
                                                                 weivel_handle registration,
                                                                 weivel_c_refusal *refusal);

/**
 * Provides `implementation` as the plugin's implementation of the capability `id`. It reaches consumers only once the
 * host has verified the plugin's activation, and lasts until the plugin stops.
 *
 * @param implementation the provider's object, answered to consumers as this very pointer. Required.
 * @param provided receives the implementation's handle; may be NULL.
 * @return as weivel_c_context_register_service answers.
 */
WEIVEL_C_API weivel_c_status weivel_c_context_provide_capability(weivel_c_context *context, const char *id,
                                                                 void *implementation, weivel_handle *provided,
                                                                 weivel_c_refusal *refusal);

/**
 * The verified implementations of a capability: `count` pointers at `items`, each the provider's own, in the order
 * the core answered them. They live in `storage`, private to the layer, until weivel_c_implementations_release.
 */
typedef struct weivel_c_implementations {
    uint32_t size;
    void *const *items;
    uint32_t count;
    void *storage;
} weivel_c_implementations;

/**
 * Lists the verified implementations of the capability `id`, in the core's order. An implementation the layer holds
 * no object for, one provided around the contexts, is left out and the error sink told.
 *
 * @param list an empty list, as WEIVEL_INIT or a release leaves it. Required.
 * @return WEIVEL_C_STATUS_OK, also with none; WEIVEL_C_STATUS_REFUSED with the core's refusal, of reach among them;
 * WEIVEL_C_STATUS_MISUSE; WEIVEL_C_STATUS_OUT_OF_MEMORY; and what the wrapper answers for any call.
 */
WEIVEL_C_API weivel_c_status weivel_c_context_all_capabilities(weivel_c_context *context, const char *id,
                                                               weivel_c_implementations *list,
                                                               weivel_c_refusal *refusal);

/**
 * Frees what a list carries and leaves it empty.
 *
 * @return WEIVEL_C_STATUS_OK, also for an empty list; WEIVEL_C_STATUS_MISUSE for NULL or one too small.
 * Threading: any thread; needs no context.
 */
WEIVEL_C_API weivel_c_status weivel_c_implementations_release(weivel_c_implementations *list);

/** One change a watch of a capability heard. */
typedef struct weivel_c_capability_change {
    uint32_t size;
    /** The watch's handle. */
    weivel_handle watch;
    /** The capability id watched. */
    const char *id;
    /** A weivel_capability_change. */
    uint32_t change;
    /** The provider's own pointer: the one that joined, or the very one that left. */
    void *implementation;
} weivel_c_capability_change;

/**
 * What hears a watched capability: `hear` and the `data` it is called with, once per change, from the dispatch.
 *
 * @implNote No `size`, as weivel_c_log has none.
 */
typedef struct weivel_c_capability_watcher {
    weivel_c_status (*hear)(void *data, const weivel_c_capability_change *change);
    void *data;
} weivel_c_capability_watcher;

/**
 * Watches the capability `id`: each verified implementation that joins or leaves later is heard, until the plugin
 * stops watching, with weivel_c_context_stop_watching, or stops.
 *
 * @return as weivel_c_context_watch_service answers.
 */
WEIVEL_C_API weivel_c_status weivel_c_context_watch_capability(weivel_c_context *context, const char *id,
                                                               const weivel_c_capability_watcher *watcher,
                                                               weivel_handle *watch, weivel_c_refusal *refusal);

/** What the core reports about a plugin. New kinds are only appended. */
typedef enum weivel_c_plugin_event_kind {
    /** A service or an implementation the plugin holds was withdrawn, so it must stop using `object`. */
    WEIVEL_C_PLUGIN_EVENT_REVOKED = 1,
    /** The plugin reached for what it may not; `refusal` says why. Reported once per reach kind and id. */
    WEIVEL_C_PLUGIN_EVENT_REACH_REFUSED = 2,
    /** A constrained want of the plugin's settled degraded; `refusal.subject` is the constraint accepted unmet. */
    WEIVEL_C_PLUGIN_EVENT_DEGRADED = 3
} weivel_c_plugin_event_kind;

/** One thing the core reported about a plugin. */
typedef struct weivel_c_plugin_event {
    uint32_t size;
    /** A weivel_c_plugin_event_kind. */
    uint32_t kind;
    /** A weivel_reach_kind: whether a service or a capability is concerned. */
    uint32_t reach;
    /** The service or capability id concerned. */
    const char *id;
    /** For a revocation, the provider's pointer the plugin was handed and must stop using; NULL otherwise. */
    void *object;
    /** The core's refusal, as weivel_c_settlement carries one; code WEIVEL_REFUSAL_NONE for a revocation. */
    weivel_c_refusal refusal;
} weivel_c_plugin_event;

/**
 * What hears what the core reports about a plugin: `hear` and the `data` it is called with, from the dispatch.
 *
 * @implNote No `size`, as weivel_c_log has none.
 */
typedef struct weivel_c_plugin_listener {
    weivel_c_status (*hear)(void *data, const weivel_c_plugin_event *event);
    void *data;
} weivel_c_plugin_listener;

/**
 * Adds a listener behind those the plugin already added; each event is heard by every one in that order. One added
 * while an event is being told to the plugin's listeners hears that event too.
 *
 * @param registration receives what weivel_c_context_remove_listener takes, never 0. Required.
 * @return WEIVEL_C_STATUS_OK, WEIVEL_C_STATUS_MISUSE or WEIVEL_C_STATUS_OUT_OF_MEMORY.
 */
WEIVEL_C_API weivel_c_status weivel_c_context_add_listener(weivel_c_context *context,
                                                           const weivel_c_plugin_listener *listener,
                                                           uint64_t *registration);

/**
 * Removes a listener; it is taken for no event once this returns. The one call another thread's dispatch had already
 * begun may still be running.
 *
 * @return WEIVEL_C_STATUS_OK; WEIVEL_C_STATUS_MISUSE for NULL or a registration that names no listener.
 */
WEIVEL_C_API weivel_c_status weivel_c_context_remove_listener(weivel_c_context *context, uint64_t registration);

#ifdef __cplusplus
}
#endif

#endif
