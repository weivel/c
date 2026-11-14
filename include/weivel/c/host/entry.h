#ifndef WEIVEL_C_HOST_ENTRY_H
#define WEIVEL_C_HOST_ENTRY_H

/**
 * @file
 * A plugin's entry in C: the one thing a plugin written in C hands its host. It offers an activate, which takes the
 * plugin's context and may finish later, and optionally a deactivate, which may finish later too.
 *
 * An entry function answers at once with one of three answers: done, failed with a reason, or pending. A pending one
 * finishes later by calling weivel_c_completion_finish exactly once with the completion it was handed, from any
 * thread, a callback of its context's included. That is C's form of "may finish later": the host never waits on it,
 * and goes on when it is finished.
 */

#include <stdint.h>

#include <weivel/c/context.h>

#ifdef __cplusplus
extern "C" {
#endif

/** What an entry function answers, and what weivel_c_completion_finish takes. */
typedef enum weivel_c_entry_answer {
    /** It did what it was for. */
    WEIVEL_C_ENTRY_DONE = 1,
    /** It failed; the reason it gives says why. */
    WEIVEL_C_ENTRY_FAILED = 2,
    /** It goes on, and finishes later through its completion. Only an entry function answers it. */
    WEIVEL_C_ENTRY_PENDING = 3
} weivel_c_entry_answer;

/**
 * What a pending entry function finishes with. The host's: it stays valid until weivel_c_host_free, so a finish that
 * comes late, or twice, is answered as a value and never touches freed memory.
 */
typedef struct weivel_c_completion weivel_c_completion;

/**
 * An activate or a deactivate.
 *
 * @param data the entry's `data`.
 * @param context the plugin's context; for a deactivate the plugin is still whole, so it can still use what it holds.
 * @param completion what a pending answer finishes with later.
 * @param reason where a failed answer may leave one English line saying why; NULL when it gives none. The host copies
 * it as the function returns, so a text of the function's own stack is safe.
 * @return a weivel_c_entry_answer. Any other value is taken as WEIVEL_C_ENTRY_FAILED.
 */
typedef uint32_t (*weivel_c_entry_function)(void *data, weivel_c_context *context, weivel_c_completion *completion,
                                            const char **reason);

/** A plugin's entry, as the host is handed it at registration and copies. */
typedef struct weivel_c_entry {
    uint32_t size;
    /** Required. Called once per activation, when the host starts the plugin. */
    weivel_c_entry_function activate;
    /** NULL for none. Called when the plugin is released, if its activate was called. */
    weivel_c_entry_function deactivate;
    /** The plugin's own; handed to both functions and never read by the layer. */
    void *data;
    /**
     * The library weivel_c_entry_load read the entry from, which the host the entry is registered with closes when it
     * is freed; NULL for an entry the caller built itself. Private to the layer.
     */
    void *library;
} weivel_c_entry;

/**
 * Finishes what an entry function answered pending.
 *
 * @param answer WEIVEL_C_ENTRY_DONE or WEIVEL_C_ENTRY_FAILED.
 * @param reason for WEIVEL_C_ENTRY_FAILED, one English line saying why, or NULL; read only during the call.
 * @return WEIVEL_C_STATUS_OK when the host takes the finish: an activate is verified or quarantined, a deactivate's
 * release goes on, each on the calling thread; WEIVEL_C_STATUS_NONE when nothing waits on it any more, because the
 * plugin was released while its activate was in flight, so that activate verifies, quarantines and reports nothing;
 * WEIVEL_C_STATUS_DESTROYED for an activate whose host has been destroyed, or is being destroyed, which is told to
 * nobody; WEIVEL_C_STATUS_MISUSE for NULL, an answer that is neither done nor failed, or a completion finished
 * already, each changing nothing. Never anything else. The status is never what the sink is told: a finish answered
 * anything but OK reports nothing, and one answered OK reports what the work it sets off reports, as the release and
 * the activation do wherever they run. A deactivate finished WEIVEL_C_ENTRY_FAILED tells the sink
 * WEIVEL_C_FAILURE_DEACTIVATE with its reason; a call the host then makes into the core that answers a failure other
 * than WEIVEL_C_STATUS_DESTROYED tells it WEIVEL_C_FAILURE_HOST_CALL; and when this thread becomes the dispatcher, an
 * outcome listener or a callback of a plugin's that answers a failure tells it WEIVEL_C_FAILURE_OUTCOME_LISTENER or
 * WEIVEL_C_FAILURE_LISTENER. Nothing reaches the log but what a dispatch on this thread hands it.
 * Threading: any thread. Defined until weivel_c_host_free.
 */
WEIVEL_C_API weivel_c_status weivel_c_completion_finish(weivel_c_completion *completion, uint32_t answer,
                                                        const char *reason);

/**
 * What a shared library exports for weivel_c_entry_load to read an entry through: it fills `entry`, whose `size` the
 * host set to what it knows, and answers WEIVEL_C_STATUS_OK, or any other status when it cannot.
 */
typedef weivel_c_status (*weivel_c_entry_provider)(weivel_c_entry *entry);

/** Why weivel_c_entry_load did not load, beyond its status. */
typedef struct weivel_c_entry_load_failure {
    uint32_t size;
    /** After WEIVEL_C_STATUS_MISSING_SYMBOL, the symbol the library lacks, the caller's own text; NULL otherwise. */
    const char *missing_symbol;
    /** After WEIVEL_C_STATUS_FAILED, what the library's provider answered; WEIVEL_C_STATUS_OK otherwise. */
    uint32_t provided;
} weivel_c_entry_load_failure;

/**
 * Reads a plugin's entry from a shared library: opens it at `path` with the platform's loader and calls the
 * weivel_c_entry_provider it exports as `symbol`. The layer chooses neither the path nor the symbol name. A path that
 * holds no directory separator is searched for by the platform's loader, as for weivel_c_core_load; give an absolute
 * path to load exactly one file.
 *
 * The library stays open, held by `entry->library`, until weivel_c_host_free frees the host the entry is registered
 * with, or until weivel_c_entry_unload for an entry never registered. Code of the plugin's may run until that free:
 * a finish made from inside the library on the plugin's own thread after its release or the destroy has finished, a
 * thread of its own, a service object another plugin still holds. The free comes only once every call into the
 * layer has returned, which is what makes closing the library then safe.
 *
 * @param entry an entry with `size` set and nothing else, as WEIVEL_INIT leaves it, filled only on OK. Required.
 * @param failure receives what a status alone cannot say; may be NULL. Cleared first when given and large enough.
 * @return WEIVEL_C_STATUS_OK; WEIVEL_C_STATUS_MISUSE (a NULL or empty path or symbol, no entry, an entry smaller than
 * its first shape or holding a library already); WEIVEL_C_STATUS_NOT_A_LIBRARY; WEIVEL_C_STATUS_MISSING_SYMBOL;
 * WEIVEL_C_STATUS_FAILED when the provider answered anything but OK or left no activate; WEIVEL_C_STATUS_OUT_OF_MEMORY.
 * Nothing stays loaded on any answer but OK.
 * Threading: any thread.
 */
WEIVEL_C_API weivel_c_status weivel_c_entry_load(const char *path, const char *symbol, weivel_c_entry *entry,
                                                 weivel_c_entry_load_failure *failure);

/**
 * Closes the library of an entry weivel_c_entry_load read and that no host was handed, and empties the entry.
 *
 * @return WEIVEL_C_STATUS_OK, also for an entry with no library; WEIVEL_C_STATUS_MISUSE; WEIVEL_C_STATUS_NOT_UNLOADED.
 */
WEIVEL_C_API weivel_c_status weivel_c_entry_unload(weivel_c_entry *entry);

#ifdef __cplusplus
}
#endif

#endif
