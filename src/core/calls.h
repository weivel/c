#ifndef WEIVEL_C_CORE_CALLS_H
#define WEIVEL_C_CORE_CALLS_H

/**
 * @file
 * The calls that answer a handle, each with the step its caller runs before any event naming that handle is heard
 * (see weivel_c_answered_step). The layer's other modules make their entry in the `handles` table through it. Each
 * is the public call of the same name without `_then`, which passes no step. None is exported from the shared
 * library.
 */

#include "wrapper.h"

weivel_c_status weivel_c_core_register_manifest_then(weivel_c_core *core, const weivel_manifest *manifest,
                                                     weivel_handle *plugin, weivel_c_refusal *refusal,
                                                     const weivel_c_answered_step *then);

weivel_c_status weivel_c_core_register_service_then(weivel_c_core *core, weivel_handle owner, const char *id,
                                               weivel_handle *registration, weivel_c_refusal *refusal,
                                                    const weivel_c_answered_step *then);

weivel_c_status weivel_c_core_want_service_then(weivel_c_core *core, weivel_handle plugin, const char *id,
                                           const weivel_want_options *options, weivel_handle *want,
                                                weivel_c_refusal *refusal,
                                                const weivel_c_answered_step *then);

weivel_c_status weivel_c_core_watch_service_then(weivel_c_core *core, weivel_handle plugin, const char *id,
                                            weivel_handle *watch, weivel_c_refusal *refusal,
                                                 const weivel_c_answered_step *then);

weivel_c_status weivel_c_core_provide_capability_then(weivel_c_core *core, weivel_handle plugin, const char *id,
                                                 weivel_handle *implementation, weivel_c_refusal *refusal,
                                                      const weivel_c_answered_step *then);

weivel_c_status weivel_c_core_watch_capability_then(weivel_c_core *core, weivel_handle plugin, const char *id,
                                               weivel_handle *watch, weivel_c_refusal *refusal,
                                                    const weivel_c_answered_step *then);

#endif
