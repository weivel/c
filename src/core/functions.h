#ifndef WEIVEL_C_CORE_FUNCTIONS_H
#define WEIVEL_C_CORE_FUNCTIONS_H

/**
 * Every function the core exports, by its name without the `weivel_` prefix, in the order of the table's fields.
 * `test/core/table_covers_the_core.cmake` fails when the pinned core's header declares one this list lacks.
 */
#define WEIVEL_C_CORE_FUNCTIONS(FUNCTION) \
    FUNCTION(core_create)                 \
    FUNCTION(core_destroy)                \
    FUNCTION(next_event)                  \
    FUNCTION(event_release)               \
    FUNCTION(next_deadline)               \
    FUNCTION(advance)                     \
    FUNCTION(supports)                    \
    FUNCTION(support_release)             \
    FUNCTION(register_manifest)           \
    FUNCTION(plan_activation)             \
    FUNCTION(activation_plan_release)     \
    FUNCTION(open_plugin)                 \
    FUNCTION(plugin_payload)              \
    FUNCTION(release_plugin)              \
    FUNCTION(quarantine_plugin)           \
    FUNCTION(forget_plugin)               \
    FUNCTION(register_service)            \
    FUNCTION(unregister)                  \
    FUNCTION(handle_state)                \
    FUNCTION(get_service)                 \
    FUNCTION(want_service)                \
    FUNCTION(report_capacity)             \
    FUNCTION(watch_service)               \
    FUNCTION(service_ids)                 \
    FUNCTION(unbound_services)            \
    FUNCTION(service_list_release)        \
    FUNCTION(provide_capability)          \
    FUNCTION(verify_activation)           \
    FUNCTION(all_capabilities)            \
    FUNCTION(capability_list_release)     \
    FUNCTION(watch_capability)            \
    FUNCTION(ledger)                      \
    FUNCTION(ledger_snapshot_release)     \
    FUNCTION(ledger_entry)                \
    FUNCTION(ledger_row_release)

#endif
