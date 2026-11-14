#include <weivel/c/standard.h>

#include <stddef.h>

#define END_OF(type, field) (offsetof(type, field) + sizeof(((type *)0)->field))
#define FIRST_CONFIG_SIZE END_OF(weivel_standard_config, release_refusal)
#define FIRST_SECRETS_SIZE END_OF(weivel_standard_secrets, release_refusal)
#define FIRST_PATHS_SIZE END_OF(weivel_standard_paths, plugin)

weivel_c_status weivel_c_standard_offer_config(weivel_c_host *host, weivel_standard_config *config,
                                               weivel_handle *registration, weivel_c_refusal *refusal) {
    if (config == NULL || config->size < FIRST_CONFIG_SIZE) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    return weivel_c_host_register_service(host, weivel_standard_service_config(), config, NULL, registration,
                                          refusal);
}

weivel_c_status weivel_c_standard_offer_secrets(weivel_c_host *host, weivel_standard_secrets *secrets,
                                                weivel_handle *registration, weivel_c_refusal *refusal) {
    if (secrets == NULL || secrets->size < FIRST_SECRETS_SIZE) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    return weivel_c_host_register_service(host, weivel_standard_service_secrets(), secrets, NULL, registration,
                                          refusal);
}

weivel_c_status weivel_c_standard_offer_paths(weivel_c_host *host, weivel_standard_paths *paths,
                                              weivel_handle *registration, weivel_c_refusal *refusal) {
    if (paths == NULL || paths->size < FIRST_PATHS_SIZE) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    return weivel_c_host_register_service(host, weivel_standard_service_paths(), paths, NULL, registration, refusal);
}
