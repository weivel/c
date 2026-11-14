#include <weivel/c/core.h>

#include "calls.h"

/**
 * @file
 * The core's calls, each as one function that names one field of the table and is handed to the wrapper, which is
 * the only thing that calls it. `test/core/only_the_wrapper_calls.cmake` fails when any other source of the layer
 * calls through the table.
 */

static int is_an_id(const char *id) {
    return id != NULL && id[0] != '\0';
}

typedef struct next_deadline_arguments {
    int64_t *deadline;
} next_deadline_arguments;

static weivel_status call_next_deadline(const weivel_c_core_table *table, weivel_core *core, void *arguments,
                                        uint32_t *pending_events) {
    next_deadline_arguments *given = arguments;
    return table->next_deadline(core, given->deadline, pending_events);
}

weivel_c_status weivel_c_core_next_deadline(weivel_c_core *core, int64_t *deadline, weivel_c_refusal *refusal) {
    next_deadline_arguments arguments = {deadline};
    if (deadline == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    return weivel_c_core_invoke(core, call_next_deadline, &arguments, WEIVEL_C_CALL_BUILT_BY_THE_LAYER, refusal);
}

typedef struct advance_arguments {
    int64_t now;
} advance_arguments;

static weivel_status call_advance(const weivel_c_core_table *table, weivel_core *core, void *arguments,
                                  uint32_t *pending_events) {
    advance_arguments *given = arguments;
    return table->advance(core, given->now, pending_events);
}

weivel_c_status weivel_c_core_advance(weivel_c_core *core, int64_t now, weivel_c_refusal *refusal) {
    advance_arguments arguments = {now};
    return weivel_c_core_invoke_handing_time(core, call_advance, &arguments, refusal);
}

typedef struct supports_arguments {
    const weivel_manifest *manifest;
    weivel_support *answer;
} supports_arguments;

static weivel_status call_supports(const weivel_c_core_table *table, weivel_core *core, void *arguments,
                                   uint32_t *pending_events) {
    supports_arguments *given = arguments;
    return table->supports(core, given->manifest, given->answer, pending_events);
}

weivel_c_status weivel_c_core_supports(weivel_c_core *core, const weivel_manifest *manifest, weivel_support *answer,
                                       weivel_c_refusal *refusal) {
    supports_arguments arguments = {manifest, answer};
    if (manifest == NULL || answer == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    return weivel_c_core_invoke(core, call_supports, &arguments, WEIVEL_C_CALL_CARRIES_A_CALLERS_STRUCT, refusal);
}

typedef struct register_manifest_arguments {
    const weivel_manifest *manifest;
    weivel_handle *plugin;
} register_manifest_arguments;

static weivel_status call_register_manifest(const weivel_c_core_table *table, weivel_core *core, void *arguments,
                                            uint32_t *pending_events) {
    register_manifest_arguments *given = arguments;
    return table->register_manifest(core, given->manifest, given->plugin, pending_events);
}

weivel_c_status weivel_c_core_register_manifest_then(weivel_c_core *core, const weivel_manifest *manifest,
                                                     weivel_handle *plugin, weivel_c_refusal *refusal,
                                                     const weivel_c_answered_step *then) {
    register_manifest_arguments arguments = {manifest, plugin};
    if (manifest == NULL || plugin == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    return weivel_c_core_invoke_then(core, call_register_manifest, &arguments, WEIVEL_C_CALL_CARRIES_A_CALLERS_STRUCT,
                                     refusal, then);
}

weivel_c_status weivel_c_core_register_manifest(weivel_c_core *core, const weivel_manifest *manifest,
                                                     weivel_handle *plugin, weivel_c_refusal *refusal) {
    return weivel_c_core_register_manifest_then(core, manifest, plugin, refusal, NULL);
}

typedef struct plan_activation_arguments {
    const weivel_handle *plugins;
    uint32_t count;
    uint32_t edge_source;
    const weivel_strings *host_provided;
    weivel_activation_plan *plan;
} plan_activation_arguments;

static weivel_status call_plan_activation(const weivel_c_core_table *table, weivel_core *core, void *arguments,
                                          uint32_t *pending_events) {
    plan_activation_arguments *given = arguments;
    return table->plan_activation(core, given->plugins, given->count, given->edge_source, given->host_provided,
                                  given->plan, pending_events);
}

weivel_c_status weivel_c_core_plan_activation(weivel_c_core *core, const weivel_handle *plugins, uint32_t count,
                                              uint32_t edge_source, const weivel_strings *host_provided,
                                              weivel_activation_plan *plan, weivel_c_refusal *refusal) {
    plan_activation_arguments arguments = {plugins, count, edge_source, host_provided, plan};
    if ((plugins == NULL && count != 0) || plan == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    return weivel_c_core_invoke(core, call_plan_activation, &arguments, WEIVEL_C_CALL_CARRIES_A_CALLERS_STRUCT,
                                refusal);
}

typedef struct open_plugin_arguments {
    weivel_handle plugin;
    const weivel_plugin_options *options;
} open_plugin_arguments;

static weivel_status call_open_plugin(const weivel_c_core_table *table, weivel_core *core, void *arguments,
                                      uint32_t *pending_events) {
    open_plugin_arguments *given = arguments;
    return table->open_plugin(core, given->plugin, given->options, pending_events);
}

weivel_c_status weivel_c_core_open_plugin(weivel_c_core *core, weivel_handle plugin,
                                          const weivel_plugin_options *options, weivel_c_refusal *refusal) {
    open_plugin_arguments arguments = {plugin, options};
    return weivel_c_core_invoke(core, call_open_plugin, &arguments, WEIVEL_C_CALL_CARRIES_A_CALLERS_STRUCT, refusal);
}

typedef struct plugin_payload_arguments {
    weivel_handle plugin;
    uint64_t *payload;
} plugin_payload_arguments;

static weivel_status call_plugin_payload(const weivel_c_core_table *table, weivel_core *core, void *arguments,
                                         uint32_t *pending_events) {
    plugin_payload_arguments *given = arguments;
    return table->plugin_payload(core, given->plugin, given->payload, pending_events);
}

weivel_c_status weivel_c_core_plugin_payload(weivel_c_core *core, weivel_handle plugin, uint64_t *payload,
                                             weivel_c_refusal *refusal) {
    plugin_payload_arguments arguments = {plugin, payload};
    if (payload == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    return weivel_c_core_invoke(core, call_plugin_payload, &arguments, WEIVEL_C_CALL_BUILT_BY_THE_LAYER, refusal);
}

typedef struct release_plugin_arguments {
    weivel_handle plugin;
} release_plugin_arguments;

static weivel_status call_release_plugin(const weivel_c_core_table *table, weivel_core *core, void *arguments,
                                         uint32_t *pending_events) {
    release_plugin_arguments *given = arguments;
    return table->release_plugin(core, given->plugin, pending_events);
}

weivel_c_status weivel_c_core_release_plugin(weivel_c_core *core, weivel_handle plugin, weivel_c_refusal *refusal) {
    release_plugin_arguments arguments = {plugin};
    return weivel_c_core_invoke(core, call_release_plugin, &arguments, WEIVEL_C_CALL_BUILT_BY_THE_LAYER, refusal);
}

typedef struct quarantine_plugin_arguments {
    weivel_handle plugin;
    const weivel_quarantine_reason *reason;
} quarantine_plugin_arguments;

static weivel_status call_quarantine_plugin(const weivel_c_core_table *table, weivel_core *core, void *arguments,
                                            uint32_t *pending_events) {
    quarantine_plugin_arguments *given = arguments;
    return table->quarantine_plugin(core, given->plugin, given->reason, pending_events);
}

weivel_c_status weivel_c_core_quarantine_plugin(weivel_c_core *core, weivel_handle plugin,
                                                const weivel_quarantine_reason *reason, weivel_c_refusal *refusal) {
    quarantine_plugin_arguments arguments = {plugin, reason};
    return weivel_c_core_invoke(core, call_quarantine_plugin, &arguments, WEIVEL_C_CALL_CARRIES_A_CALLERS_STRUCT,
                                refusal);
}

typedef struct forget_plugin_arguments {
    weivel_handle plugin;
} forget_plugin_arguments;

static weivel_status call_forget_plugin(const weivel_c_core_table *table, weivel_core *core, void *arguments,
                                        uint32_t *pending_events) {
    forget_plugin_arguments *given = arguments;
    return table->forget_plugin(core, given->plugin, pending_events);
}

weivel_c_status weivel_c_core_forget_plugin(weivel_c_core *core, weivel_handle plugin, weivel_c_refusal *refusal) {
    forget_plugin_arguments arguments = {plugin};
    return weivel_c_core_invoke(core, call_forget_plugin, &arguments, WEIVEL_C_CALL_BUILT_BY_THE_LAYER, refusal);
}

typedef struct register_service_arguments {
    weivel_handle owner;
    const char *id;
    weivel_handle *registration;
} register_service_arguments;

static weivel_status call_register_service(const weivel_c_core_table *table, weivel_core *core, void *arguments,
                                           uint32_t *pending_events) {
    register_service_arguments *given = arguments;
    return table->register_service(core, given->owner, given->id, given->registration, pending_events);
}

weivel_c_status weivel_c_core_register_service_then(weivel_c_core *core, weivel_handle owner, const char *id,
                                                    weivel_handle *registration, weivel_c_refusal *refusal,
                                                    const weivel_c_answered_step *then) {
    register_service_arguments arguments = {owner, id, registration};
    if (!is_an_id(id) || registration == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    return weivel_c_core_invoke_then(core, call_register_service, &arguments, WEIVEL_C_CALL_BUILT_BY_THE_LAYER,
                                     refusal, then);
}

weivel_c_status weivel_c_core_register_service(weivel_c_core *core, weivel_handle owner, const char *id,
                                                    weivel_handle *registration, weivel_c_refusal *refusal) {
    return weivel_c_core_register_service_then(core, owner, id, registration, refusal, NULL);
}

typedef struct unregister_arguments {
    weivel_handle handle;
} unregister_arguments;

static weivel_status call_unregister(const weivel_c_core_table *table, weivel_core *core, void *arguments,
                                     uint32_t *pending_events) {
    unregister_arguments *given = arguments;
    return table->unregister(core, given->handle, pending_events);
}

weivel_c_status weivel_c_core_unregister(weivel_c_core *core, weivel_handle handle, weivel_c_refusal *refusal) {
    unregister_arguments arguments = {handle};
    return weivel_c_core_invoke(core, call_unregister, &arguments, WEIVEL_C_CALL_BUILT_BY_THE_LAYER, refusal);
}

typedef struct handle_state_arguments {
    weivel_handle handle;
    uint32_t *condition;
} handle_state_arguments;

static weivel_status call_handle_state(const weivel_c_core_table *table, weivel_core *core, void *arguments,
                                       uint32_t *pending_events) {
    handle_state_arguments *given = arguments;
    return table->handle_state(core, given->handle, given->condition, pending_events);
}

weivel_c_status weivel_c_core_handle_state(weivel_c_core *core, weivel_handle handle, uint32_t *condition,
                                           weivel_c_refusal *refusal) {
    handle_state_arguments arguments = {handle, condition};
    if (condition == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    return weivel_c_core_invoke(core, call_handle_state, &arguments, WEIVEL_C_CALL_BUILT_BY_THE_LAYER, refusal);
}

typedef struct get_service_arguments {
    weivel_handle plugin;
    const char *id;
    weivel_handle *implementation;
} get_service_arguments;

static weivel_status call_get_service(const weivel_c_core_table *table, weivel_core *core, void *arguments,
                                      uint32_t *pending_events) {
    get_service_arguments *given = arguments;
    return table->get_service(core, given->plugin, given->id, given->implementation, pending_events);
}

weivel_c_status weivel_c_core_get_service_then(weivel_c_core *core, weivel_handle plugin, const char *id,
                                               weivel_handle *implementation, weivel_c_refusal *refusal,
                                               const weivel_c_answered_step *then) {
    get_service_arguments arguments = {plugin, id, implementation};
    if (!is_an_id(id) || implementation == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    return weivel_c_core_invoke_then(core, call_get_service, &arguments, WEIVEL_C_CALL_BUILT_BY_THE_LAYER, refusal,
                                     then);
}

weivel_c_status weivel_c_core_get_service(weivel_c_core *core, weivel_handle plugin, const char *id,
                                          weivel_handle *implementation, weivel_c_refusal *refusal) {
    return weivel_c_core_get_service_then(core, plugin, id, implementation, refusal, NULL);
}

typedef struct want_service_arguments {
    weivel_handle plugin;
    const char *id;
    const weivel_want_options *options;
    weivel_handle *want;
} want_service_arguments;

static weivel_status call_want_service(const weivel_c_core_table *table, weivel_core *core, void *arguments,
                                       uint32_t *pending_events) {
    want_service_arguments *given = arguments;
    return table->want_service(core, given->plugin, given->id, given->options, given->want, pending_events);
}

weivel_c_status weivel_c_core_want_service_then(weivel_c_core *core, weivel_handle plugin, const char *id,
                                                const weivel_want_options *options, weivel_handle *want,
                                                weivel_c_refusal *refusal,
                                                const weivel_c_answered_step *then) {
    want_service_arguments arguments = {plugin, id, options, want};
    if (!is_an_id(id) || want == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    return weivel_c_core_invoke_then(core, call_want_service, &arguments, WEIVEL_C_CALL_CARRIES_A_CALLERS_STRUCT,
                                     refusal, then);
}

weivel_c_status weivel_c_core_want_service(weivel_c_core *core, weivel_handle plugin, const char *id,
                                                const weivel_want_options *options, weivel_handle *want,
                                           weivel_c_refusal *refusal) {
    return weivel_c_core_want_service_then(core, plugin, id, options, want, refusal, NULL);
}

typedef struct report_capacity_arguments {
    weivel_handle want;
    weivel_handle registration;
    const weivel_capacity *capacity;
    int64_t now;
} report_capacity_arguments;

static weivel_status call_report_capacity(const weivel_c_core_table *table, weivel_core *core, void *arguments,
                                          uint32_t *pending_events) {
    report_capacity_arguments *given = arguments;
    return table->report_capacity(core, given->want, given->registration, given->capacity, given->now,
                                  pending_events);
}

weivel_c_status weivel_c_core_report_capacity(weivel_c_core *core, weivel_handle want, weivel_handle registration,
                                              const weivel_capacity *capacity, int64_t now,
                                              weivel_c_refusal *refusal) {
    report_capacity_arguments arguments = {want, registration, capacity, now};
    if (capacity == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    return weivel_c_core_invoke(core, call_report_capacity, &arguments, WEIVEL_C_CALL_CARRIES_A_CALLERS_STRUCT,
                                refusal);
}

typedef struct watch_service_arguments {
    weivel_handle plugin;
    const char *id;
    weivel_handle *watch;
} watch_service_arguments;

static weivel_status call_watch_service(const weivel_c_core_table *table, weivel_core *core, void *arguments,
                                        uint32_t *pending_events) {
    watch_service_arguments *given = arguments;
    return table->watch_service(core, given->plugin, given->id, given->watch, pending_events);
}

weivel_c_status weivel_c_core_watch_service_then(weivel_c_core *core, weivel_handle plugin, const char *id,
                                                 weivel_handle *watch, weivel_c_refusal *refusal,
                                                 const weivel_c_answered_step *then) {
    watch_service_arguments arguments = {plugin, id, watch};
    if (!is_an_id(id) || watch == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    return weivel_c_core_invoke_then(core, call_watch_service, &arguments, WEIVEL_C_CALL_BUILT_BY_THE_LAYER,
                                     refusal, then);
}

weivel_c_status weivel_c_core_watch_service(weivel_c_core *core, weivel_handle plugin, const char *id,
                                                 weivel_handle *watch, weivel_c_refusal *refusal) {
    return weivel_c_core_watch_service_then(core, plugin, id, watch, refusal, NULL);
}

typedef struct service_ids_arguments {
    weivel_handle plugin;
    weivel_service_list *list;
} service_ids_arguments;

static weivel_status call_service_ids(const weivel_c_core_table *table, weivel_core *core, void *arguments,
                                      uint32_t *pending_events) {
    service_ids_arguments *given = arguments;
    return table->service_ids(core, given->plugin, given->list, pending_events);
}

weivel_c_status weivel_c_core_service_ids(weivel_c_core *core, weivel_handle plugin, weivel_service_list *list,
                                          weivel_c_refusal *refusal) {
    service_ids_arguments arguments = {plugin, list};
    if (list == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    return weivel_c_core_invoke(core, call_service_ids, &arguments, WEIVEL_C_CALL_CARRIES_A_CALLERS_STRUCT, refusal);
}

typedef struct unbound_services_arguments {
    weivel_handle plugin;
    weivel_service_list *list;
} unbound_services_arguments;

static weivel_status call_unbound_services(const weivel_c_core_table *table, weivel_core *core, void *arguments,
                                           uint32_t *pending_events) {
    unbound_services_arguments *given = arguments;
    return table->unbound_services(core, given->plugin, given->list, pending_events);
}

weivel_c_status weivel_c_core_unbound_services(weivel_c_core *core, weivel_handle plugin, weivel_service_list *list,
                                               weivel_c_refusal *refusal) {
    unbound_services_arguments arguments = {plugin, list};
    if (list == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    return weivel_c_core_invoke(core, call_unbound_services, &arguments, WEIVEL_C_CALL_CARRIES_A_CALLERS_STRUCT,
                                refusal);
}

typedef struct provide_capability_arguments {
    weivel_handle plugin;
    const char *id;
    weivel_handle *implementation;
} provide_capability_arguments;

static weivel_status call_provide_capability(const weivel_c_core_table *table, weivel_core *core, void *arguments,
                                             uint32_t *pending_events) {
    provide_capability_arguments *given = arguments;
    return table->provide_capability(core, given->plugin, given->id, given->implementation, pending_events);
}

weivel_c_status weivel_c_core_provide_capability_then(weivel_c_core *core, weivel_handle plugin, const char *id,
                                                      weivel_handle *implementation, weivel_c_refusal *refusal,
                                                      const weivel_c_answered_step *then) {
    provide_capability_arguments arguments = {plugin, id, implementation};
    if (!is_an_id(id) || implementation == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    return weivel_c_core_invoke_then(core, call_provide_capability, &arguments, WEIVEL_C_CALL_BUILT_BY_THE_LAYER,
                                     refusal, then);
}

weivel_c_status weivel_c_core_provide_capability(weivel_c_core *core, weivel_handle plugin, const char *id,
                                                      weivel_handle *implementation, weivel_c_refusal *refusal) {
    return weivel_c_core_provide_capability_then(core, plugin, id, implementation, refusal, NULL);
}

typedef struct verify_activation_arguments {
    weivel_handle plugin;
} verify_activation_arguments;

static weivel_status call_verify_activation(const weivel_c_core_table *table, weivel_core *core, void *arguments,
                                            uint32_t *pending_events) {
    verify_activation_arguments *given = arguments;
    return table->verify_activation(core, given->plugin, pending_events);
}

weivel_c_status weivel_c_core_verify_activation(weivel_c_core *core, weivel_handle plugin,
                                                weivel_c_refusal *refusal) {
    verify_activation_arguments arguments = {plugin};
    return weivel_c_core_invoke(core, call_verify_activation, &arguments, WEIVEL_C_CALL_BUILT_BY_THE_LAYER, refusal);
}

typedef struct all_capabilities_arguments {
    weivel_handle plugin;
    const char *id;
    weivel_capability_list *list;
} all_capabilities_arguments;

static weivel_status call_all_capabilities(const weivel_c_core_table *table, weivel_core *core, void *arguments,
                                           uint32_t *pending_events) {
    all_capabilities_arguments *given = arguments;
    return table->all_capabilities(core, given->plugin, given->id, given->list, pending_events);
}

weivel_c_status weivel_c_core_all_capabilities_then(weivel_c_core *core, weivel_handle plugin, const char *id,
                                                    weivel_capability_list *list, weivel_c_refusal *refusal,
                                                    const weivel_c_answered_step *then) {
    all_capabilities_arguments arguments = {plugin, id, list};
    if (!is_an_id(id) || list == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    return weivel_c_core_invoke_then(core, call_all_capabilities, &arguments, WEIVEL_C_CALL_CARRIES_A_CALLERS_STRUCT,
                                     refusal, then);
}

weivel_c_status weivel_c_core_all_capabilities(weivel_c_core *core, weivel_handle plugin, const char *id,
                                               weivel_capability_list *list, weivel_c_refusal *refusal) {
    return weivel_c_core_all_capabilities_then(core, plugin, id, list, refusal, NULL);
}

typedef struct watch_capability_arguments {
    weivel_handle plugin;
    const char *id;
    weivel_handle *watch;
} watch_capability_arguments;

static weivel_status call_watch_capability(const weivel_c_core_table *table, weivel_core *core, void *arguments,
                                           uint32_t *pending_events) {
    watch_capability_arguments *given = arguments;
    return table->watch_capability(core, given->plugin, given->id, given->watch, pending_events);
}

weivel_c_status weivel_c_core_watch_capability_then(weivel_c_core *core, weivel_handle plugin, const char *id,
                                                    weivel_handle *watch, weivel_c_refusal *refusal,
                                                    const weivel_c_answered_step *then) {
    watch_capability_arguments arguments = {plugin, id, watch};
    if (!is_an_id(id) || watch == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    return weivel_c_core_invoke_then(core, call_watch_capability, &arguments, WEIVEL_C_CALL_BUILT_BY_THE_LAYER,
                                     refusal, then);
}

weivel_c_status weivel_c_core_watch_capability(weivel_c_core *core, weivel_handle plugin, const char *id,
                                                    weivel_handle *watch, weivel_c_refusal *refusal) {
    return weivel_c_core_watch_capability_then(core, plugin, id, watch, refusal, NULL);
}

typedef struct ledger_arguments {
    weivel_ledger_snapshot *snapshot;
} ledger_arguments;

static weivel_status call_ledger(const weivel_c_core_table *table, weivel_core *core, void *arguments,
                                 uint32_t *pending_events) {
    ledger_arguments *given = arguments;
    return table->ledger(core, given->snapshot, pending_events);
}

weivel_c_status weivel_c_core_ledger(weivel_c_core *core, weivel_ledger_snapshot *snapshot,
                                     weivel_c_refusal *refusal) {
    ledger_arguments arguments = {snapshot};
    if (snapshot == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    return weivel_c_core_invoke(core, call_ledger, &arguments, WEIVEL_C_CALL_CARRIES_A_CALLERS_STRUCT, refusal);
}

typedef struct ledger_entry_arguments {
    weivel_handle plugin;
    weivel_ledger_row *row;
} ledger_entry_arguments;

static weivel_status call_ledger_entry(const weivel_c_core_table *table, weivel_core *core, void *arguments,
                                       uint32_t *pending_events) {
    ledger_entry_arguments *given = arguments;
    return table->ledger_entry(core, given->plugin, given->row, pending_events);
}

weivel_c_status weivel_c_core_ledger_entry(weivel_c_core *core, weivel_handle plugin, weivel_ledger_row *row,
                                           weivel_c_refusal *refusal) {
    ledger_entry_arguments arguments = {plugin, row};
    if (row == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    return weivel_c_core_invoke(core, call_ledger_entry, &arguments, WEIVEL_C_CALL_CARRIES_A_CALLERS_STRUCT, refusal);
}

static weivel_status call_support_release(const weivel_c_core_table *table, void *answer) {
    return table->support_release(answer);
}

weivel_c_status weivel_c_core_support_release(weivel_c_core *core, weivel_support *answer) {
    return weivel_c_core_invoke_release(core, call_support_release, answer);
}

static weivel_status call_activation_plan_release(const weivel_c_core_table *table, void *answer) {
    return table->activation_plan_release(answer);
}

weivel_c_status weivel_c_core_activation_plan_release(weivel_c_core *core, weivel_activation_plan *plan) {
    return weivel_c_core_invoke_release(core, call_activation_plan_release, plan);
}

static weivel_status call_service_list_release(const weivel_c_core_table *table, void *answer) {
    return table->service_list_release(answer);
}

weivel_c_status weivel_c_core_service_list_release(weivel_c_core *core, weivel_service_list *list) {
    return weivel_c_core_invoke_release(core, call_service_list_release, list);
}

static weivel_status call_capability_list_release(const weivel_c_core_table *table, void *answer) {
    return table->capability_list_release(answer);
}

weivel_c_status weivel_c_core_capability_list_release(weivel_c_core *core, weivel_capability_list *list) {
    return weivel_c_core_invoke_release(core, call_capability_list_release, list);
}

static weivel_status call_ledger_snapshot_release(const weivel_c_core_table *table, void *answer) {
    return table->ledger_snapshot_release(answer);
}

weivel_c_status weivel_c_core_ledger_snapshot_release(weivel_c_core *core, weivel_ledger_snapshot *snapshot) {
    return weivel_c_core_invoke_release(core, call_ledger_snapshot_release, snapshot);
}

static weivel_status call_ledger_row_release(const weivel_c_core_table *table, void *answer) {
    return table->ledger_row_release(answer);
}

weivel_c_status weivel_c_core_ledger_row_release(weivel_c_core *core, weivel_ledger_row *row) {
    return weivel_c_core_invoke_release(core, call_ledger_row_release, row);
}
