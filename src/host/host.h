#ifndef WEIVEL_C_HOST_HOST_H
#define WEIVEL_C_HOST_HOST_H

/**
 * @file
 * What the sources of the `host` module share. None of it is exported from the shared library.
 *
 * The host's lock guards every field below that says so. It is a leaf: it is taken with no other lock of the layer's
 * held, and nothing is called while it is held, neither another part of the layer nor anything of a plugin's or of
 * the caller's.
 *
 * A plugin's record, and every completion, lives until weivel_c_host_free, so whatever a plugin still holds of the
 * host's (its context, a completion) stays memory it may read until then. A record whose plugin was forgotten is
 * kept with state 0 and is found by no handle.
 */

#include <weivel/c/host.h>
#include <weivel/c/host/library.h>

#include "../core/mutex.h"
#include "../core/wrapper.h"

#define WEIVEL_C_HOST_TEXT 512

/** Where a host stands. */
enum { HOST_RUNNING = 0, HOST_DESTROYING = 1, HOST_DESTROYED = 2 };

/** What a completion finishes. */
enum { COMPLETION_OF_AN_ACTIVATE = 1, COMPLETION_OF_A_DEACTIVATE = 2 };

typedef struct host_plugin host_plugin;

/**
 * The ending of a quarantined plugin's entries, deferred behind the events its quarantine queued, and tied to the
 * activation that quarantine ended: one that runs once a later activation has begun ends nothing of it.
 */
typedef struct host_ending {
    weivel_c_step step;
    host_plugin *plugin;
    /** The plugin's `activations` when the quarantine was made; guarded by the host's lock. */
    uint32_t activation;
} host_ending;

/**
 * One started plugin's outcome, told once from the dispatch as a step of its own. Made before the plugin is opened,
 * so telling it needs no memory.
 */
typedef struct host_outcome {
    weivel_c_step step;
    weivel_c_outcome_listener listener;
    weivel_handle plugin;
    /** A weivel_c_outcome_kind once one was chosen, under the host's lock; 0 before. */
    uint32_t kind;
    /** The refusal told with the outcome, the host's own copy, released once told. */
    weivel_c_refusal refusal;
    char detail[WEIVEL_C_HOST_TEXT];
    char fix[WEIVEL_C_HOST_TEXT];
} host_outcome;

struct weivel_c_completion {
    weivel_c_host *host;
    host_plugin *plugin;
    /** The host's every completion, freed by weivel_c_host_free. */
    weivel_c_completion *next;
    uint32_t purpose;
    /** 1 once finished; guarded by the host's lock. */
    uint32_t finished;
    /** For an activate: the outcome of the activation it finishes. */
    host_outcome outcome;
    /** For an activate: the ending of its plugin's entries should that activation be quarantined. */
    host_ending ending;
};

struct host_plugin {
    weivel_c_host *host;
    /** Every plugin the host registered, in the order it did. */
    host_plugin *next;
    /** The plugins the host opened and has not released, in the order it opened them; guarded by the host's lock. */
    host_plugin *opened_previous;
    host_plugin *opened_next;
    weivel_handle handle;
    /** The host's own copy of the manifest the plugin was registered with, freed with the record. */
    weivel_manifest *manifest;
    weivel_c_entry entry;
    /** Made when the plugin is first opened, detached once it is forgotten, freed with the host. */
    weivel_c_context *context;
    /** A weivel_c_plugin_state, or 0 once forgotten; guarded by the host's lock. */
    uint32_t state;
    /** 1 while the plugin is in the opened list; guarded by the host's lock. */
    uint32_t opened;
    /** 1 once its activate was called and its deactivate not; guarded by the host's lock. */
    uint32_t activate_called;
    /** How many activations have taken it into the opened list; guarded by the host's lock. */
    uint32_t activations;
    /** The completion of the activation in flight, or NULL; guarded by the host's lock. */
    weivel_c_completion *activating;
    weivel_c_completion deactivating;
    /** Steps 4 to 6 of its release. */
    weivel_c_step finishing;
    /** The ending of its entries once the quarantine a verify the host's caller asked for made has its events heard. */
    host_ending verify_ending;
};

/**
 * A service the host registered under WEIVEL_HOST, or is registering. Its id stays until the host is freed, since a
 * plan reads it.
 *
 * @implNote A registration claims its id on the list, `pending`, under the same hold that found no live library
 * declaring it and before the core is asked, so a library whose load is kept meanwhile finds the claim and is refused.
 */
typedef struct host_service {
    struct host_service *next;
    weivel_handle registration;
    /** 1 while the registration stands; guarded by the host's lock. */
    uint32_t live;
    /** 1 from the claim until the core has answered the registration; guarded by the host's lock. */
    uint32_t pending;
    char id[1];
} host_service;

struct weivel_c_host {
    weivel_c_mutex lock;
    /** Where the memory of the completions comes from. */
    weivel_c_allocator allocator;
    weivel_c_core_table table;
    uint32_t loaded_the_table;
    weivel_c_core *core;
    weivel_c_handles *handles;
    weivel_c_timer *timer;
    weivel_c_contexts *contexts;
    weivel_c_context *own_context;
    /** Guarded by the host's lock, as every field below. */
    host_plugin *first_plugin;
    host_plugin *last_plugin;
    host_plugin *first_opened;
    host_plugin *last_opened;
    weivel_c_completion *completions;
    host_service *services;
    /** The C libraries the host loaded, the last loaded first, unloaded ones kept until the host is freed. */
    weivel_c_library *libraries;
    uint32_t state;
    uint32_t releases_in_flight;
    /** The opens begun while the host was running and not yet taken into the opened list or answered. */
    uint32_t opens_in_flight;
    /** The settlings of activations taken while the host was running whose outcome is not yet queued. */
    uint32_t settles_in_flight;
    uint32_t destroy_waits;
    uint32_t destroy_finished;
    weivel_c_step destroy_beginning;
    weivel_c_step destroy_ending;
};

/**
 * Replaces the allocator the completions are made with: the seam a test fails an allocation through. No completion
 * may have been made yet.
 */
void weivel_c_host_replace_allocator(weivel_c_host *host, const weivel_c_allocator *allocator);

/**
 * Copies a manifest the caller registers into one block of the host's own, every text and array of it included, so
 * the caller may free or change its own once the registration returns. NULL when memory ran out. Freed with free().
 */
weivel_manifest *weivel_c_host_copy_manifest(const weivel_manifest *given);

/**
 * Registers a service under WEIVEL_HOST as weivel_c_host_register_service does, but without refusing an id a live
 * library declares: how a library the host loads registers its own services.
 */
weivel_c_status weivel_c_host_register_library_service(weivel_c_host *host, const char *id, void *service,
                                                       weivel_handle *registration, weivel_c_refusal *refusal);

/** Under the host's lock: 1 when a service the host registered or is registering holds `id`. */
int weivel_c_host_holds_service_id(const weivel_c_host *host, const char *id);

/** 1 while nothing has begun to destroy the host. */
int weivel_c_host_is_running(weivel_c_host *host);

/** The record of a plugin the host registered and has not forgotten, or NULL. */
host_plugin *weivel_c_host_plugin_of(weivel_c_host *host, weivel_handle plugin);

/** Tells the error sink of a failure of the host's: `kind` a weivel_c_failure_kind, `text` one English line. */
void weivel_c_host_report(weivel_c_host *host, uint32_t kind, weivel_c_status status, const char *text);

/** Reports a call the host made that answered anything but OK, unless the host is being destroyed past caring. */
void weivel_c_host_report_call(weivel_c_host *host, weivel_c_status status, const char *text);

/** Under the host's lock: adds a plugin to the end of the opened list. */
void weivel_c_host_join_opened(host_plugin *plugin);

/** Under the host's lock: takes a plugin out of the opened list. */
void weivel_c_host_leave_opened(host_plugin *plugin);

/**
 * Closes the library of a plugin's entry, if it has one, and tells the sink when that fails.
 *
 * @implNote Only weivel_c_host_free calls it: code of the plugin's may run until every call into the layer has
 * returned, a finish made from inside the library on the plugin's own thread, a background thread of its or a service
 * object another plugin holds among it, and none of them is known to the host to have ended any earlier.
 */
void weivel_c_host_close_library(host_plugin *plugin);

/**
 * Makes the completion of each plugin of a plan's order, with its outcome told to `listener`, and keeps them on the
 * host's list. Its `plugin` is NULL for a handle the host registered no plugin under. Answers the array, which its
 * caller hands back to the host's allocator; NULL, with nothing made, when memory ran out.
 */
weivel_c_completion **weivel_c_host_make_completions(weivel_c_host *host, const weivel_handle *order, uint32_t count,
                                                     const weivel_c_outcome_listener *listener);

/** Readies the ending of a plugin's entries after a quarantine. */
void weivel_c_host_ready_ending(host_ending *ending, host_plugin *plugin);

/** Readies a completion embedded in a record, keeping it off the host's list. */
void weivel_c_host_ready_completion(weivel_c_completion *completion, host_plugin *plugin, uint32_t purpose);

/**
 * Under the host's lock: chooses an activation's outcome when none was chosen. Answers 1 when this call chose it, so
 * its caller tells it.
 */
int weivel_c_host_choose_outcome(weivel_c_completion *activation, uint32_t kind);

/** Tells a chosen outcome to its listener from the dispatch. */
void weivel_c_host_tell_outcome(weivel_c_completion *activation);

/** Takes the answer an activate gave at once or finished with, and verifies or quarantines its plugin. */
weivel_c_status weivel_c_host_settle_activation(weivel_c_completion *activation, uint32_t answer, const char *reason);

/** Takes the answer a deactivate gave at once or finished with, and goes on with its plugin's release. */
weivel_c_status weivel_c_host_settle_deactivation(weivel_c_completion *deactivation, uint32_t answer,
                                                  const char *reason);

/**
 * Quarantines the plugin of `ending` in the core, its entries ending by `ending` once the events of that call have
 * been dispatched, unless a later activation of it has begun by then. With a NULL reason the ledger row shows the
 * core's own refusal of the plugin's verification.
 */
weivel_c_status weivel_c_host_quarantine(host_ending *ending, const weivel_quarantine_reason *reason);

/**
 * Verifies the plugin of `ending`, and quarantines it in the same step when the core refuses. Answers what the verify
 * answered.
 */
weivel_c_status weivel_c_host_verify_plugin(host_ending *ending, weivel_c_refusal *refusal);

/**
 * Begins a plugin's release. Answers WEIVEL_C_STATUS_OK, `*finished` 1 when every step has run before this returned;
 * or WEIVEL_C_STATUS_NONE for a plugin not opened or whose release has begun already.
 */
weivel_c_status weivel_c_host_begin_release(host_plugin *plugin, uint32_t *finished);

/** Steps 2 and on of a release, once its deactivate has finished. Answers 1 when they have all run. */
int weivel_c_host_go_on_releasing(host_plugin *plugin);

/** Steps 4 to 6 of a release, as the step a plugin's record carries: `data` is the record. */
weivel_c_status weivel_c_host_finish_release(void *data, weivel_c_core *core);

/** Ends a quarantined plugin's entries, as the step a host_ending carries: `data` is that ending. */
weivel_c_status weivel_c_host_end_quarantined_entries(void *data, weivel_c_core *core);

/**
 * Under the host's lock: whether the last release, open and settling a destroy waits on have finished, so its last
 * steps may run.
 */
int weivel_c_host_destroy_may_end(weivel_c_host *host);

/** Defers the destroy's last steps behind everything queued. */
void weivel_c_host_end_the_destroy(weivel_c_host *host);

/**
 * The destroy's unloading of every C library the host loaded and has not unloaded, the last loaded first. Each is
 * closed to calls and its binary closed once nothing holds it; a close the platform refuses is told to the sink and
 * the rest still unload. No service is unregistered, since the core is destroyed next.
 */
void weivel_c_host_unload_every_library(weivel_c_host *host);

/** Under the host's lock: 1 while a call runs in one of the host's libraries or bytes one answered are held. */
int weivel_c_host_libraries_are_held(weivel_c_host *host);

/** Frees every library record of a host whose libraries nothing holds. */
void weivel_c_host_free_libraries(weivel_c_host *host);

#endif
