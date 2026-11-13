#ifndef WEIVEL_C_CORE_H
#define WEIVEL_C_CORE_H

/**
 * @file
 * The `core` module of the weivel C language implementation: it loads the core's shared binary into one table, and
 * owns the one wrapper every call into that binary goes through. Nothing else in the layer names a core function, so
 * one build of the layer runs against whichever core its caller loads.
 *
 * The wrapper, in the order it works:
 *
 * - A call is made under the layer lock and its events are drained under the same hold, so the events drained after
 *   a call are that call's. Each event is copied into a weivel_c_event the layer owns and released in the core before
 *   the next is drained.
 * - Events are dispatched to the listeners first in, first out, outside the lock, by one thread at a time. A call
 *   made while a dispatch runs, from a listener or from another thread, drains its own events onto the end of the
 *   queue and returns; the running dispatch delivers them after everything drained before them. The dispatcher is
 *   never re-entered. A deferred step is an entry of the same queue, so listeners and steps run one at a time.
 * - What a listener, a step or a hook answers as a failure goes to the error sink, never to a caller.
 *
 * Conventions every declaration of the layer follows:
 *
 * - Size. Every public struct begins with `uint32_t size`, set by the caller to `sizeof` the struct as the caller was
 *   compiled (the core's WEIVEL_INIT does it, and zeroes the rest). Fields are only ever appended. The layer reads and
 *   writes only the bytes the caller's `size` covers, and a size smaller than the struct's first published shape is
 *   the caller's mistake.
 * - Status. Every call answers a weivel_c_status and never crashes, aborts or prints on a mistake.
 * - Text. A path is UTF-8 on every platform.
 */

#include <stdint.h>

#include <weivel/core.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#if defined(WEIVEL_C_BUILDING_SHARED)
#define WEIVEL_C_API __declspec(dllexport)
#elif defined(WEIVEL_C_USING_SHARED)
#define WEIVEL_C_API __declspec(dllimport)
#else
#define WEIVEL_C_API
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define WEIVEL_C_API __attribute__((visibility("default")))
#else
#define WEIVEL_C_API
#endif

/** What every call of the layer answers. */
typedef enum weivel_c_status {
    /** The call did what it was asked. */
    WEIVEL_C_STATUS_OK = 0,
    /** The caller's mistake: a required pointer was NULL, a size was too small, or a struct was not as the call needs
     * it. Nothing changed. */
    WEIVEL_C_STATUS_MISUSE = 1,
    /** Memory ran out; nothing changed. */
    WEIVEL_C_STATUS_OUT_OF_MEMORY = 2,
    /** The path names nothing the platform's loader opens as a shared library; nothing changed. */
    WEIVEL_C_STATUS_NOT_A_LIBRARY = 3,
    /** The library lacks a function the caller's table has room for; nothing changed and nothing stays loaded. */
    WEIVEL_C_STATUS_MISSING_SYMBOL = 4,
    /** The platform's loader refused to close a library. The table is emptied all the same. */
    WEIVEL_C_STATUS_NOT_UNLOADED = 5,
    /** The core had nothing to answer (its WEIVEL_STATUS_NONE): nothing is registered, or no deadline is set. Not a
     * failure. */
    WEIVEL_C_STATUS_NONE = 6,
    /** The core declined the call. The caller's weivel_c_refusal says why when the core queued a refusal for it, and
     * is left empty when the core queued none. */
    WEIVEL_C_STATUS_REFUSED = 7,
    /** The core called a call the layer built malformed: a defect of the layer, reported to the error sink, and
     * never the caller's mistake. */
    WEIVEL_C_STATUS_LAYER_DEFECT = 8,
    /** The layer's core object was destroyed; no call was made. */
    WEIVEL_C_STATUS_DESTROYED = 9,
    /** A failure with no status of its own: what a listener, a step or a hook answers when it failed, and what a
     * call answers when the core answered a status this layer does not know. */
    WEIVEL_C_STATUS_FAILED = 10
} weivel_c_status;

/**
 * One loaded core: the binary and every function it exports, each under the name the core's header gives it without
 * the `weivel_` prefix, with the signature and the documentation it has there.
 *
 * A table is empty when `library` is NULL, which is how WEIVEL_INIT leaves it, and then none of its functions may be
 * called.
 */
typedef struct weivel_c_core_table {
    uint32_t size;
    /** The platform loader's handle of the binary; NULL while nothing is loaded. Owned by the table. */
    void *library;
    weivel_status (*core_create)(const weivel_core_options *options, weivel_core **core, uint32_t *pending_events);
    weivel_status (*core_destroy)(weivel_core *core);
    weivel_status (*next_event)(weivel_core *core, weivel_event *event, uint32_t *pending_events);
    weivel_status (*event_release)(weivel_event *event);
    weivel_status (*next_deadline)(weivel_core *core, int64_t *deadline, uint32_t *pending_events);
    weivel_status (*advance)(weivel_core *core, int64_t now, uint32_t *pending_events);
    weivel_status (*supports)(weivel_core *core, const weivel_manifest *manifest, weivel_support *answer,
                              uint32_t *pending_events);
    weivel_status (*support_release)(weivel_support *answer);
    weivel_status (*register_manifest)(weivel_core *core, const weivel_manifest *manifest, weivel_handle *plugin,
                                       uint32_t *pending_events);
    weivel_status (*plan_activation)(weivel_core *core, const weivel_handle *plugins, uint32_t count,
                                     uint32_t edge_source, const weivel_strings *host_provided,
                                     weivel_activation_plan *plan, uint32_t *pending_events);
    weivel_status (*activation_plan_release)(weivel_activation_plan *plan);
    weivel_status (*open_plugin)(weivel_core *core, weivel_handle plugin, const weivel_plugin_options *options,
                                 uint32_t *pending_events);
    weivel_status (*plugin_payload)(weivel_core *core, weivel_handle plugin, uint64_t *payload,
                                    uint32_t *pending_events);
    weivel_status (*release_plugin)(weivel_core *core, weivel_handle plugin, uint32_t *pending_events);
    weivel_status (*quarantine_plugin)(weivel_core *core, weivel_handle plugin, const weivel_quarantine_reason *reason,
                                       uint32_t *pending_events);
    weivel_status (*forget_plugin)(weivel_core *core, weivel_handle plugin, uint32_t *pending_events);
    weivel_status (*register_service)(weivel_core *core, weivel_handle owner, const char *id,
                                      weivel_handle *registration, uint32_t *pending_events);
    weivel_status (*unregister)(weivel_core *core, weivel_handle handle, uint32_t *pending_events);
    weivel_status (*handle_state)(weivel_core *core, weivel_handle handle, uint32_t *condition,
                                  uint32_t *pending_events);
    weivel_status (*get_service)(weivel_core *core, weivel_handle plugin, const char *id,
                                 weivel_handle *implementation, uint32_t *pending_events);
    weivel_status (*want_service)(weivel_core *core, weivel_handle plugin, const char *id,
                                  const weivel_want_options *options, weivel_handle *want, uint32_t *pending_events);
    weivel_status (*report_capacity)(weivel_core *core, weivel_handle want, weivel_handle registration,
                                     const weivel_capacity *capacity, int64_t now, uint32_t *pending_events);
    weivel_status (*watch_service)(weivel_core *core, weivel_handle plugin, const char *id, weivel_handle *watch,
                                   uint32_t *pending_events);
    weivel_status (*service_ids)(weivel_core *core, weivel_handle plugin, weivel_service_list *list,
                                 uint32_t *pending_events);
    weivel_status (*unbound_services)(weivel_core *core, weivel_handle plugin, weivel_service_list *list,
                                      uint32_t *pending_events);
    weivel_status (*service_list_release)(weivel_service_list *list);
    weivel_status (*provide_capability)(weivel_core *core, weivel_handle plugin, const char *id,
                                        weivel_handle *implementation, uint32_t *pending_events);
    weivel_status (*verify_activation)(weivel_core *core, weivel_handle plugin, uint32_t *pending_events);
    weivel_status (*all_capabilities)(weivel_core *core, weivel_handle plugin, const char *id,
                                      weivel_capability_list *list, uint32_t *pending_events);
    weivel_status (*capability_list_release)(weivel_capability_list *list);
    weivel_status (*watch_capability)(weivel_core *core, weivel_handle plugin, const char *id, weivel_handle *watch,
                                      uint32_t *pending_events);
    weivel_status (*ledger)(weivel_core *core, weivel_ledger_snapshot *snapshot, uint32_t *pending_events);
    weivel_status (*ledger_snapshot_release)(weivel_ledger_snapshot *snapshot);
    weivel_status (*ledger_entry)(weivel_core *core, weivel_handle plugin, weivel_ledger_row *row,
                                  uint32_t *pending_events);
    weivel_status (*ledger_row_release)(weivel_ledger_row *row);
} weivel_c_core_table;

/** Why weivel_c_core_load did not load, beyond its status. */
typedef struct weivel_c_core_load_failure {
    uint32_t size;
    /**
     * After WEIVEL_C_STATUS_MISSING_SYMBOL, the name of the first function the library lacks, in the table's order;
     * NULL after every other status. The text is the layer's own and is never freed.
     */
    const char *missing_symbol;
} weivel_c_core_load_failure;

/**
 * Loads the core's shared binary from `path` and fills `table` with every function of the core the table has room
 * for. The layer chooses no path and no file name: both are the caller's.
 *
 * @param path the binary, as the platform's loader takes it. Required; read only during the call.
 * @param table an empty table (see weivel_c_core_table), filled only when the call answers OK. Required. The caller
 * owns it and hands it to weivel_c_core_unload once no core created through it is left.
 * @param failure receives what a status alone cannot say; may be NULL.
 * @return WEIVEL_C_STATUS_OK, or WEIVEL_C_STATUS_MISUSE (no `path`, no `table`, a `table` or `failure` smaller than
 * its first shape, or a `table` that already holds a library), WEIVEL_C_STATUS_NOT_A_LIBRARY,
 * WEIVEL_C_STATUS_MISSING_SYMBOL or WEIVEL_C_STATUS_OUT_OF_MEMORY.
 * Threading: any thread; one table is loaded by one thread at a time. A loaded table is only read, so any number of
 * threads may call through it, as the core allows for each function.
 */
WEIVEL_C_API weivel_c_status weivel_c_core_load(const char *path, weivel_c_core_table *table,
                                                weivel_c_core_load_failure *failure);

/**
 * Closes the binary a table holds and empties the table, so a later call through it is a call through NULL and never
 * into code that is gone. Every core created through the table must be destroyed first.
 *
 * @return WEIVEL_C_STATUS_OK, also for an empty table, which is a no-op; WEIVEL_C_STATUS_MISUSE (no `table`, or one
 * smaller than its first shape); or WEIVEL_C_STATUS_NOT_UNLOADED.
 * Threading: no call through this table may be in flight or follow.
 */
WEIVEL_C_API weivel_c_status weivel_c_core_unload(weivel_c_core_table *table);

/**
 * One core behind the wrapper. Opaque; made by weivel_c_core_create, destroyed by weivel_c_core_destroy and freed by
 * weivel_c_core_free.
 */
typedef struct weivel_c_core weivel_c_core;

/**
 * The refusal the core queued for a refused call, as a value the caller owns. A call fills it only when it answers
 * WEIVEL_C_STATUS_REFUSED, or WEIVEL_C_STATUS_MISUSE for a struct of the caller's the core called malformed, and only
 * when the core queued a refusal for that call; otherwise it stays empty, `code` WEIVEL_REFUSAL_NONE and every text
 * NULL, because the layer never invents a refusal the core did not give.
 *
 * The texts live in `storage`, private to the layer, until weivel_c_refusal_release. A call takes an empty refusal,
 * as WEIVEL_INIT or a release leaves it, or NULL when its caller wants the status alone.
 */
typedef struct weivel_c_refusal {
    uint32_t size;
    /** A weivel_refusal_code. */
    uint32_t code;
    /** What happened, English UTF-8. */
    const char *detail;
    /** What would make it succeed, English UTF-8. */
    const char *fix;
    /** What the refusal is about, as its code says; NULL when it names nothing. */
    const char *subject;
    /** 1 when `fix` is non-empty, else 0. */
    uint32_t has_fix;
    void *storage;
} weivel_c_refusal;

/**
 * Frees what a refusal carries and leaves it empty.
 *
 * @return WEIVEL_C_STATUS_OK, also as a no-op for an empty refusal; WEIVEL_C_STATUS_MISUSE for NULL or one smaller
 * than its first shape.
 * Threading: any thread; needs no core, and may run after the core that filled it is freed.
 */
WEIVEL_C_API weivel_c_status weivel_c_refusal_release(weivel_c_refusal *refusal);

/**
 * One event of the core, copied into the layer's own memory as it was drained. `kind` is a weivel_event_kind and
 * `data` holds the payload the core's header documents for that kind, read through the member of the same name
 * (`refused` for a WEIVEL_EVENT_REFUSAL). Every text and every array a payload carries is the layer's copy: a
 * capacity event's constraints are laid out at the layer's own stride, each element's `size` the layer's
 * `sizeof(weivel_number)`.
 *
 * An event lives for the length of the listener call that hears it. A listener that keeps anything copies it.
 */
typedef struct weivel_c_event {
    uint32_t size;
    uint32_t kind;
    /** The event's place in the order this core's events were drained, counted from 1. */
    uint64_t sequence;
    union {
        weivel_log_event log;
        weivel_refusal_event refused;
        weivel_want_event want;
        weivel_watch_event watch;
        weivel_revoked_event revoked;
        weivel_reach_refusal reach;
        weivel_capacity_event capacity;
        weivel_capability_event capability;
        weivel_deadline_event deadline;
        uint64_t reserved[WEIVEL_EVENT_PAYLOAD_WORDS];
    } data;
} weivel_c_event;

/** What went wrong in code the layer calls, or in the layer itself. New kinds are only appended. */
typedef enum weivel_c_failure_kind {
    /** A listener answered a status other than WEIVEL_C_STATUS_OK for an event. */
    WEIVEL_C_FAILURE_LISTENER = 1,
    /** A deferred step answered a status other than WEIVEL_C_STATUS_OK. */
    WEIVEL_C_FAILURE_STEP = 2,
    /** An event could not be copied, for want of memory, and was dropped; the events after it were still drained. */
    WEIVEL_C_FAILURE_EVENT_NOT_COPIED = 3,
    /** The core called a call the layer built malformed. */
    WEIVEL_C_FAILURE_LAYER_DEFECT = 4,
    /** A deadline hook answered a status other than WEIVEL_C_STATUS_OK. */
    WEIVEL_C_FAILURE_DEADLINE_HOOK = 5,
    /** A refused call's refusal could not be copied for its caller, who was answered the status alone. */
    WEIVEL_C_FAILURE_REFUSAL_NOT_COPIED = 6,
    /** One call met more failures than the layer keeps for it; `sequence` holds how many were not told. */
    WEIVEL_C_FAILURE_NOT_TOLD = 7
} weivel_c_failure_kind;

/** One failure, as the error sink is told it. Everything in it lives for the length of the sink's call. */
typedef struct weivel_c_failure {
    uint32_t size;
    /** A weivel_c_failure_kind. */
    uint32_t kind;
    /** The weivel_c_status the failing code answered, or the layer's own for a failure of its own. */
    uint32_t status;
    /** The weivel_event_kind of the event concerned, or WEIVEL_EVENT_NONE when no event is. */
    uint32_t event_kind;
    /** That event's `sequence`, or 0. */
    uint64_t sequence;
    /** One English line saying what failed. Never NULL. */
    const char *text;
} weivel_c_failure;

/**
 * Where failures go: `report` and the `data` it is called with. It is called outside the layer lock, on the thread
 * that met the failure, so it may call into the core. A sink that answers a status other than WEIVEL_C_STATUS_OK has
 * itself failed, and that is written to standard error. A NULL `report` is the default sink, which writes one line
 * per failure to standard error.
 *
 * @implNote No `size`: it is embedded by value, so the struct embedding it is what grows.
 */
typedef struct weivel_c_error_sink {
    weivel_c_status (*report)(void *data, const weivel_c_failure *failure);
    void *data;
} weivel_c_error_sink;

/**
 * Hears every event of a core: `hear` and the `data` it is called with. It is called outside the layer lock, by the
 * one thread dispatching, and may call into the core; the events such a call drains are heard after `hear` returns,
 * behind every event drained before them. A status other than WEIVEL_C_STATUS_OK goes to the error sink and the
 * other listeners still hear the event.
 *
 * @implNote No `size`, as weivel_c_error_sink has none.
 */
typedef struct weivel_c_listener {
    weivel_c_status (*hear)(void *data, weivel_c_core *core, const weivel_c_event *event);
    void *data;
} weivel_c_listener;

/**
 * The layer's place for a step in its queue. Private to the layer, and all zero while the step does not wait.
 */
typedef struct weivel_c_queue_place {
    struct weivel_c_queue_place *next;
    uint32_t waiting;
} weivel_c_queue_place;

/**
 * A step to run once every event drained so far has been dispatched: `run` and the `data` it is called with. The
 * caller owns the struct and keeps it where it is until the step has run or the core is destroyed, so queueing a
 * step needs no memory and cannot fail for want of it. `run` is called outside the layer lock and may call into the
 * core and queue this very step again. A status other than WEIVEL_C_STATUS_OK goes to the error sink.
 */
typedef struct weivel_c_step {
    uint32_t size;
    weivel_c_status (*run)(void *data, weivel_c_core *core);
    void *data;
    weivel_c_queue_place place;
} weivel_c_step;

/**
 * What the `timer` module installs to follow the core's deadline. Both hooks run under the layer lock, inside the
 * call that sets them off, so they do no more than read a clock and arm a timer: a call into this core from either
 * waits on itself for ever. A hook that answers a status other than WEIVEL_C_STATUS_OK is reported to the error
 * sink.
 */
typedef struct weivel_c_deadline_hooks {
    uint32_t size;
    /**
     * Told of each WEIVEL_EVENT_DEADLINE as it is drained, before any event is dispatched: `set` 1 with the deadline
     * `at`, or 0 when none is left. May be NULL.
     *
     * @implNote At drain time and not at dispatch, because a dispatch can run after a newer announcement was drained.
     */
    weivel_c_status (*announced)(void *data, int64_t at, uint32_t set);
    /**
     * Asked before each call whether a deadline has fallen due: it answers `*due` 1 and the time `*now` when its
     * clock has reached the deadline it armed, and the wrapper then hands the core that time with weivel_advance,
     * drains and dispatches it as any call, and only then makes the call it was asked for. The advance the wrapper
     * makes is not preceded by this question, so the check never recurses. May be NULL, and then no call is checked.
     */
    weivel_c_status (*is_due)(void *data, int64_t *now, uint32_t *due);
    void *data;
} weivel_c_deadline_hooks;

/** What a core is created from. Zero, or a field beyond `size`, means the default. */
typedef struct weivel_c_core_options {
    uint32_t size;
    /** A loaded table. Required. Copied, so the caller's may go, but its binary stays loaded until the core is
     * freed. */
    const weivel_c_core_table *table;
    /** The core's own options, handed over unchanged; NULL for every default. Read only during the call. */
    const weivel_core_options *core_options;
    /** Where failures go for the life of the core. */
    weivel_c_error_sink error_sink;
    /** A first listener, registered before the core is created so it hears what creation queues; `hear` NULL for
     * none. */
    weivel_c_listener listener;
} weivel_c_core_options;

/**
 * Creates a core through `options->table` and the object that wraps it.
 *
 * @param core receives the new object, owned by the caller until weivel_c_core_free. Required; NULL on every answer
 * but OK.
 * @return WEIVEL_C_STATUS_OK; WEIVEL_C_STATUS_MISUSE (no `options`, no `core`, options smaller than their first
 * shape, a table that is missing, too small or empty, or core options the core calls malformed);
 * WEIVEL_C_STATUS_OUT_OF_MEMORY.
 * Threading: any thread.
 */
WEIVEL_C_API weivel_c_status weivel_c_core_create(const weivel_c_core_options *options, weivel_c_core **core);

/**
 * Destroys the core behind the object. Events drained and not yet dispatched are dropped, and steps still waiting
 * are dropped without being run, their structs the caller's again; from the moment this returns no listener call
 * and no step begins. An owner that wants them heard makes its last call, lets it return, and destroys then.
 *
 * The object itself stays until weivel_c_core_free, and every later call on it answers WEIVEL_C_STATUS_DESTROYED.
 * It may be called inside a listener or a step: the dispatch it runs in then ends with that listener or step.
 *
 * @return WEIVEL_C_STATUS_OK, also for a core destroyed already; WEIVEL_C_STATUS_MISUSE for NULL.
 * Threading: any thread; a call in flight on another thread finishes first. The one event or step another thread's
 * dispatch had already taken from the queue may still be running when this returns.
 */
WEIVEL_C_API weivel_c_status weivel_c_core_destroy(weivel_c_core *core);

/**
 * Frees a destroyed core's object.
 *
 * @return WEIVEL_C_STATUS_OK, also for NULL; WEIVEL_C_STATUS_MISUSE, freeing nothing, for a core not destroyed or
 * one whose dispatch is still running, which is where a listener or a step that destroyed it still is.
 * Threading: no call on this object may be in flight or follow.
 */
WEIVEL_C_API weivel_c_status weivel_c_core_free(weivel_c_core *core);

/**
 * Adds a listener behind those already added. Each event is heard by every listener in the order they were added;
 * one added while an event is dispatched hears that event too.
 *
 * @param listener `hear` is required; read only during the call.
 * @param registration receives what weivel_c_core_remove_listener takes, never 0. Required.
 * @return WEIVEL_C_STATUS_OK, or WEIVEL_C_STATUS_MISUSE, WEIVEL_C_STATUS_OUT_OF_MEMORY or
 * WEIVEL_C_STATUS_DESTROYED.
 * Threading: any thread, a listener or a step included.
 */
WEIVEL_C_API weivel_c_status weivel_c_core_add_listener(weivel_c_core *core, const weivel_c_listener *listener,
                                                        uint64_t *registration);

/**
 * Removes a listener. No call to it begins once this returns; one already begun on another thread may still be
 * running, so a listener whose `data` is about to be freed is removed from inside a dispatch, by a listener or a
 * step.
 *
 * @return WEIVEL_C_STATUS_OK; WEIVEL_C_STATUS_MISUSE for NULL or a registration that names no listener;
 * WEIVEL_C_STATUS_DESTROYED.
 * Threading: any thread, a listener or a step included.
 */
WEIVEL_C_API weivel_c_status weivel_c_core_remove_listener(weivel_c_core *core, uint64_t registration);

/**
 * Sets the deadline hooks, replacing any set before.
 *
 * @param hooks NULL removes them; read only during the call.
 * @return WEIVEL_C_STATUS_OK, WEIVEL_C_STATUS_MISUSE or WEIVEL_C_STATUS_DESTROYED.
 * Threading: any thread but from inside a hook.
 */
WEIVEL_C_API weivel_c_status weivel_c_core_set_deadline_hooks(weivel_c_core *core,
                                                              const weivel_c_deadline_hooks *hooks);

/**
 * Runs a step once every event drained so far has been dispatched. A step is a queue entry like an event: it joins
 * the end of the queue and is run by the one thread dispatching, in queue order, so no step runs while a listener or
 * another step does. While a dispatch runs, on this thread or on another, that dispatch runs it, possibly after this
 * returns. While none runs, every event drained so far has been dispatched already, so the calling thread becomes
 * the dispatcher and the step has run when this returns; a call it makes is then a call made inside a dispatch, its
 * events heard after the step returns and before this does.
 *
 * @param step an idle step, as WEIVEL_INIT or its last run leaves it. Required.
 * @return WEIVEL_C_STATUS_OK, whatever the step answers; WEIVEL_C_STATUS_MISUSE (NULL, a step smaller than its first
 * shape, without `run`, or already waiting); WEIVEL_C_STATUS_DESTROYED, the step never run.
 * Threading: any thread.
 */
WEIVEL_C_API weivel_c_status weivel_c_core_defer(weivel_c_core *core, weivel_c_step *step);

/**
 * @name The core's calls, through the wrapper.
 *
 * Each makes the call of the same name in the core's header, which documents it, and answers its status as a
 * weivel_c_status:
 *
 * - WEIVEL_STATUS_OK, NONE and OUT_OF_MEMORY as the status of the same name;
 * - WEIVEL_STATUS_REFUSED as WEIVEL_C_STATUS_REFUSED, with the refusal the core queued for the call copied into
 *   `refusal`: the first WEIVEL_EVENT_REFUSAL or WEIVEL_EVENT_REACH_REFUSED the call's drain holds, which is the
 *   call's own, since a refused call changes nothing and so queues nothing about anyone else. That event is still
 *   dispatched to the listeners as every event is. Where the core queues none (a refusal of reach it reported
 *   before, a late call that queues only a log, weivel_handle_state on a handle that names nothing) `refusal` stays
 *   empty;
 * - WEIVEL_STATUS_MALFORMED as WEIVEL_C_STATUS_LAYER_DEFECT, reported to the error sink, when the layer built every
 *   part of the call; and as WEIVEL_C_STATUS_MISUSE, with the core's refusal in `refusal`, when the call carried a
 *   struct its caller built, which is then what was malformed.
 *
 * Each answers WEIVEL_C_STATUS_MISUSE without making the call for a NULL `core`, a NULL required pointer, an id that
 * is NULL or empty, or a `refusal` that is too small or still holds one, and WEIVEL_C_STATUS_DESTROYED on a
 * destroyed core. `refusal` may always be NULL. An answer the core fills is released by the call named after its
 * release.
 *
 * Threading: any thread. A call made while a dispatch runs returns once its events are drained, before they are
 * dispatched.
 */
/**@{*/
WEIVEL_C_API weivel_c_status weivel_c_core_next_deadline(weivel_c_core *core, int64_t *deadline,
                                                         weivel_c_refusal *refusal);
WEIVEL_C_API weivel_c_status weivel_c_core_advance(weivel_c_core *core, int64_t now, weivel_c_refusal *refusal);
WEIVEL_C_API weivel_c_status weivel_c_core_supports(weivel_c_core *core, const weivel_manifest *manifest,
                                                    weivel_support *answer, weivel_c_refusal *refusal);
WEIVEL_C_API weivel_c_status weivel_c_core_support_release(weivel_c_core *core, weivel_support *answer);
WEIVEL_C_API weivel_c_status weivel_c_core_register_manifest(weivel_c_core *core, const weivel_manifest *manifest,
                                                             weivel_handle *plugin, weivel_c_refusal *refusal);
WEIVEL_C_API weivel_c_status weivel_c_core_plan_activation(weivel_c_core *core, const weivel_handle *plugins,
                                                           uint32_t count, uint32_t edge_source,
                                                           const weivel_strings *host_provided,
                                                           weivel_activation_plan *plan, weivel_c_refusal *refusal);
WEIVEL_C_API weivel_c_status weivel_c_core_activation_plan_release(weivel_c_core *core,
                                                                   weivel_activation_plan *plan);
WEIVEL_C_API weivel_c_status weivel_c_core_open_plugin(weivel_c_core *core, weivel_handle plugin,
                                                       const weivel_plugin_options *options,
                                                       weivel_c_refusal *refusal);
WEIVEL_C_API weivel_c_status weivel_c_core_plugin_payload(weivel_c_core *core, weivel_handle plugin,
                                                          uint64_t *payload, weivel_c_refusal *refusal);
WEIVEL_C_API weivel_c_status weivel_c_core_release_plugin(weivel_c_core *core, weivel_handle plugin,
                                                          weivel_c_refusal *refusal);
WEIVEL_C_API weivel_c_status weivel_c_core_quarantine_plugin(weivel_c_core *core, weivel_handle plugin,
                                                             const weivel_quarantine_reason *reason,
                                                             weivel_c_refusal *refusal);
WEIVEL_C_API weivel_c_status weivel_c_core_forget_plugin(weivel_c_core *core, weivel_handle plugin,
                                                         weivel_c_refusal *refusal);
WEIVEL_C_API weivel_c_status weivel_c_core_register_service(weivel_c_core *core, weivel_handle owner, const char *id,
                                                            weivel_handle *registration, weivel_c_refusal *refusal);
WEIVEL_C_API weivel_c_status weivel_c_core_unregister(weivel_c_core *core, weivel_handle handle,
                                                      weivel_c_refusal *refusal);
WEIVEL_C_API weivel_c_status weivel_c_core_handle_state(weivel_c_core *core, weivel_handle handle,
                                                        uint32_t *condition, weivel_c_refusal *refusal);
WEIVEL_C_API weivel_c_status weivel_c_core_get_service(weivel_c_core *core, weivel_handle plugin, const char *id,
                                                       weivel_handle *implementation, weivel_c_refusal *refusal);
WEIVEL_C_API weivel_c_status weivel_c_core_want_service(weivel_c_core *core, weivel_handle plugin, const char *id,
                                                        const weivel_want_options *options, weivel_handle *want,
                                                        weivel_c_refusal *refusal);
WEIVEL_C_API weivel_c_status weivel_c_core_report_capacity(weivel_c_core *core, weivel_handle want,
                                                           weivel_handle registration,
                                                           const weivel_capacity *capacity, int64_t now,
                                                           weivel_c_refusal *refusal);
WEIVEL_C_API weivel_c_status weivel_c_core_watch_service(weivel_c_core *core, weivel_handle plugin, const char *id,
                                                         weivel_handle *watch, weivel_c_refusal *refusal);
WEIVEL_C_API weivel_c_status weivel_c_core_service_ids(weivel_c_core *core, weivel_handle plugin,
                                                       weivel_service_list *list, weivel_c_refusal *refusal);
WEIVEL_C_API weivel_c_status weivel_c_core_unbound_services(weivel_c_core *core, weivel_handle plugin,
                                                            weivel_service_list *list, weivel_c_refusal *refusal);
WEIVEL_C_API weivel_c_status weivel_c_core_service_list_release(weivel_c_core *core, weivel_service_list *list);
WEIVEL_C_API weivel_c_status weivel_c_core_provide_capability(weivel_c_core *core, weivel_handle plugin,
                                                              const char *id, weivel_handle *implementation,
                                                              weivel_c_refusal *refusal);
WEIVEL_C_API weivel_c_status weivel_c_core_verify_activation(weivel_c_core *core, weivel_handle plugin,
                                                             weivel_c_refusal *refusal);
WEIVEL_C_API weivel_c_status weivel_c_core_all_capabilities(weivel_c_core *core, weivel_handle plugin, const char *id,
                                                            weivel_capability_list *list, weivel_c_refusal *refusal);
WEIVEL_C_API weivel_c_status weivel_c_core_capability_list_release(weivel_c_core *core,
                                                                   weivel_capability_list *list);
WEIVEL_C_API weivel_c_status weivel_c_core_watch_capability(weivel_c_core *core, weivel_handle plugin, const char *id,
                                                            weivel_handle *watch, weivel_c_refusal *refusal);
WEIVEL_C_API weivel_c_status weivel_c_core_ledger(weivel_c_core *core, weivel_ledger_snapshot *snapshot,
                                                  weivel_c_refusal *refusal);
WEIVEL_C_API weivel_c_status weivel_c_core_ledger_snapshot_release(weivel_c_core *core,
                                                                   weivel_ledger_snapshot *snapshot);
WEIVEL_C_API weivel_c_status weivel_c_core_ledger_entry(weivel_c_core *core, weivel_handle plugin,
                                                        weivel_ledger_row *row, weivel_c_refusal *refusal);
WEIVEL_C_API weivel_c_status weivel_c_core_ledger_row_release(weivel_c_core *core, weivel_ledger_row *row);
/**@}*/

#ifdef __cplusplus
}
#endif

#endif
