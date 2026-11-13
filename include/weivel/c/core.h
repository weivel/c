#ifndef WEIVEL_C_CORE_H
#define WEIVEL_C_CORE_H

/**
 * @file
 * The `core` module of the weivel C language implementation: it loads the core's shared binary and holds every
 * function of it in one table. Nothing else in the layer names a core function, so one build of the layer runs
 * against whichever core its caller loads.
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
    WEIVEL_C_STATUS_NOT_UNLOADED = 5
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

#ifdef __cplusplus
}
#endif

#endif
