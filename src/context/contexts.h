#ifndef WEIVEL_C_CONTEXT_CONTEXTS_H
#define WEIVEL_C_CONTEXT_CONTEXTS_H

/**
 * @file
 * What the sources of the `context` module share. None of it is exported from the shared library.
 *
 * The locks, in the one order they are ever taken: the layer lock, which only the wrapper takes and under which a
 * call's answered step runs; then the lock of the contexts; then the lock of the handles table. Nothing here calls
 * into the core, a callback, the log or the error sink while it holds the lock of the contexts.
 *
 * Every entry of the handles table that is not a plugin's is a weivel_c_record of these contexts, so the table a
 * host gives its contexts is used for nothing else of those kinds.
 */

#include <weivel/c/context.h>

#include "../core/mutex.h"
#include "../core/wrapper.h"

/** What a record stands for. */
typedef enum weivel_c_record_kind {
    WEIVEL_C_RECORD_SERVICE = 1,
    WEIVEL_C_RECORD_IMPLEMENTATION = 2,
    WEIVEL_C_RECORD_SERVICE_WATCH = 3,
    WEIVEL_C_RECORD_CAPABILITY_WATCH = 4,
    WEIVEL_C_RECORD_WANT = 5
} weivel_c_record_kind;

typedef struct weivel_c_record weivel_c_record;

/**
 * The layer's object for one handle the core answered a plugin: a registration, an implementation, a watch or a want,
 * with the id it was asked under and what the plugin gave with it.
 *
 * @implNote A record is freed only by the thread dispatching, or once the core is destroyed: by the step it carries,
 * by the listener that settled its want or called back with it, or by its context ending its entries. A callback
 * therefore never has a record, or the id text it was handed from one, freed under it by another thread.
 *
 * - `ended` is 1 once what the record stands for ended, so no callback is begun for it afterwards. A plugin's own
 *   ending sets it only once the core has answered that ending OK, under the layer lock, so a settlement the core had
 *   already queued still finds the record and settles it.
 * - `retiring` is 1 while the freeing belongs to one path that has not run yet: the step an ending queued, or the
 *   dispatch that claimed a want to settle it.
 * - `calling` is 1 while the dispatch is inside a callback that was handed the record's id; whoever ends the record
 *   meanwhile leaves the freeing to the dispatch, which frees it once the callback returns.
 *
 * Whoever would free a record that is retiring or calling instead leaves it, moving it to the orphans when its
 * context is freed.
 */
struct weivel_c_record {
    weivel_c_step step;
    weivel_c_contexts *contexts;
    /** NULL once the context was freed while the record's step still waited. */
    weivel_c_context *context;
    weivel_c_record *previous;
    weivel_c_record *next;
    weivel_handle handle;
    weivel_handle plugin;
    uint32_t kind;
    uint32_t ended;
    uint32_t retiring;
    uint32_t calling;
    /** 1 while a want holds the reservation of the one holding its settlement may hand over. */
    uint32_t holding_reserved;
    /** The provider's object, for a registration and an implementation. */
    void *object;
    weivel_c_capacity_answerer answerer;
    weivel_c_want_callback want_callback;
    weivel_c_service_watcher service_watcher;
    weivel_c_capability_watcher capability_watcher;
    /** The service or capability id, in the record's own block. */
    const char *id;
};

typedef struct weivel_c_listener_slot {
    uint64_t registration;
    weivel_c_plugin_listener listener;
} weivel_c_listener_slot;

struct weivel_c_context {
    weivel_c_contexts *contexts;
    weivel_c_context *previous;
    weivel_c_context *next;
    weivel_handle plugin;
    const weivel_manifest *manifest;
    uint64_t token;
    weivel_c_record *records;
    weivel_c_listener_slot *slots;
    uint32_t slot_count;
    uint32_t slot_capacity;
    uint64_t last_registration;
    /** 1 once weivel_c_context_detach was called; guarded by the lock of the contexts. */
    uint32_t detached;
};

/** @implNote `lock` guards every list and count here, each context's lists, and each record's flags. */
struct weivel_c_contexts {
    weivel_c_mutex lock;
    weivel_c_core *core;
    weivel_c_handles *handles;
    weivel_c_timer *timer;
    weivel_c_log log;
    /** Where the memory of the records comes from. */
    weivel_c_allocator allocator;
    weivel_c_context *first;
    /** Records whose context was freed while their step waited. */
    weivel_c_record *orphans;
    uint32_t context_count;
    uint32_t record_count;
};

/** A call's answered step and what it works on: the record to keep under the handle the core just answered. */
typedef struct weivel_c_keeping {
    weivel_c_record *record;
    const weivel_handle *answered;
    /** 1 once the step used the record's reservation, whatever the addition into it answered. */
    int reservation_used;
} weivel_c_keeping;

/**
 * Replaces the allocator the records are made with: the seam a test fails an allocation through. No record may have
 * been made yet.
 */
void weivel_c_contexts_replace_allocator(weivel_c_contexts *contexts, const weivel_c_allocator *allocator);

/**
 * Makes a record of `kind` for `context` with its own copy of `id`, and reserves its entry in the handles table, and
 * for a want the one holding its settlement may hand over: everything a call that answers a handle of the plugin's
 * own needs, taken before the core is asked, so nothing can fail once the core has answered. NULL when memory ran
 * out, with nothing made and nothing reserved.
 */
weivel_c_record *weivel_c_record_prepare(weivel_c_context *context, uint32_t kind, const char *id);

/**
 * Frees a prepared record the core answered no handle for, or one its answered step could not keep, and gives back
 * each reservation it took that was not used.
 */
void weivel_c_record_abandon(const weivel_c_keeping *keeping);

/**
 * The answered step of a call that answers a handle of the plugin's own: `data` is a weivel_c_keeping. It fills the
 * entry its record reserved and needs no memory, so no event names a handle the table lacks for want of it. It fails
 * only for a handle the table already holds, and says in the keeping whether it used the reservation.
 */
weivel_c_status weivel_c_record_keep_answered(void *data);

/** Under the lock of the contexts: the record kept under `handle` when it is of `kind`, ended or not; else NULL. */
weivel_c_record *weivel_c_record_find(weivel_c_contexts *contexts, weivel_handle handle, uint32_t kind);

/** Copies the record kept under `handle` when it is of `kind` and not ended. Answers 0 when there is none. */
int weivel_c_record_read(weivel_c_contexts *contexts, weivel_handle handle, uint32_t kind, weivel_c_record *copy);

/**
 * The record kept under `handle` when it is of `kind` and not ended, marked as called back with until
 * weivel_c_record_put_back; else NULL. Only the dispatching thread calls it, around one callback.
 */
weivel_c_record *weivel_c_record_borrow(weivel_c_contexts *contexts, weivel_handle handle, uint32_t kind);

/** Ends a borrowing, and frees the record when it was ended meanwhile and no step is left to free it. */
void weivel_c_record_put_back(weivel_c_record *record);

/** As weivel_c_record_borrow, and ends the record for every later reader: what settles a want exactly once. */
weivel_c_record *weivel_c_record_claim(weivel_c_contexts *contexts, weivel_handle handle, uint32_t kind);

/**
 * Hands a claimed want's settlement over: records that its plugin holds the registration `implementation`, into the
 * room the want reserved for it, and answers the provider's object, as weivel_c_contexts_hold answers it.
 */
weivel_c_status weivel_c_record_hold_settled(weivel_c_record *want, weivel_handle plugin,
                                             weivel_handle implementation, void **object);

/** Ends and frees a claimed record at once. Only the dispatching thread calls it. */
void weivel_c_record_end_in_dispatch(weivel_c_record *record);

/** Ends every record of a context. With `freeing`, a record whose step still waits is moved to the orphans. */
void weivel_c_records_end(weivel_c_context *context, int freeing);

/** Frees the records of a list whose entries have ended and whose steps can no longer run. */
void weivel_c_records_free(weivel_c_record *first);

/**
 * Ends the entries of the records of a list, each only while the table still keeps that very record, and frees them:
 * what freeing the contexts does once the core is destroyed.
 */
void weivel_c_records_discard(weivel_c_record *first);

/**
 * Records that `plugin` holds the registration or implementation `handle`, and answers the provider's object.
 * Answers WEIVEL_C_STATUS_FAILED, `*object` NULL and no reservation used, when the layer holds no record of `kind`
 * for the handle; any other failure is the table's, with `*object` still answered. With `reserved` the holding goes
 * into room its caller reserved in the handles table, and then the table cannot fail.
 */
weivel_c_status weivel_c_contexts_hold(weivel_c_contexts *contexts, weivel_handle plugin, weivel_handle handle,
                                       uint32_t kind, int reserved, void **object);

/** 1 once the context was detached from its plugin, so no call through it may reach the core. */
int weivel_c_context_is_detached(weivel_c_context *context);

/** The context of `plugin`, or NULL. Only the dispatching thread may use what it answers. */
weivel_c_context *weivel_c_contexts_context_of(weivel_c_contexts *contexts, weivel_handle plugin);

/** Tells the error sink of a failure met for `event`, which may be NULL. */
void weivel_c_contexts_report(weivel_c_contexts *contexts, uint32_t kind, weivel_c_status status,
                              const weivel_c_event *event, const char *text);

/** Reports a callback's answer to the error sink when it is a failure. */
void weivel_c_contexts_called_back(weivel_c_contexts *contexts, weivel_c_status answered,
                                   const weivel_c_event *event);

/** Hands the host's log one line. */
void weivel_c_contexts_write_log(weivel_c_contexts *contexts, const weivel_c_log_record *record,
                                 const weivel_c_event *event);

/** Has every listener of `context` hear `told`, in the order they were added; a NULL context hears nothing. */
void weivel_c_context_emit(weivel_c_context *context, const weivel_c_plugin_event *told, const weivel_c_event *event);

/** The one listener the contexts add to their core. */
weivel_c_status weivel_c_contexts_hear(void *data, weivel_c_core *core, const weivel_c_event *event);

/** Puts one capacity question to the registration it names and reports the answer. */
void weivel_c_contexts_answer_capacity(weivel_c_contexts *contexts, const weivel_c_event *event);

/** A refusal as a callback is handed it: the event's own texts, with nothing to release. */
weivel_c_refusal weivel_c_contexts_refusal_of(const weivel_refusal *refusal, const char *subject, uint32_t has_fix);

/**
 * Ends in the core what a handle of the context's own plugin stands for, of `kind` or `other_kind`, and retires its
 * record once the core has answered OK. Answers WEIVEL_C_STATUS_NONE, asking the core nothing, for a handle that names
 * no such record of the plugin's that has not ended.
 */
weivel_c_status weivel_c_context_end_handle(weivel_c_context *context, weivel_handle handle, uint32_t kind,
                                            uint32_t other_kind, weivel_c_refusal *refusal);

#endif
