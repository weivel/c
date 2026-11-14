#include "libraries.h"

#include <stddef.h>
#include <string.h>

static int is_listed_before(const weivel_standard_library *declaration, size_t count, const char *id) {
    size_t at;
    for (at = 0; at < count; at++) {
        if (strcmp(declaration->services[at].id, id) == 0) {
            return 1;
        }
    }
    return 0;
}

const char *weivel_c_host_repeated_id(const weivel_standard_library *declaration) {
    size_t at;
    for (at = 1; at < declaration->service_count; at++) {
        if (is_listed_before(declaration, at, declaration->services[at].id)) {
            return declaration->services[at].id;
        }
    }
    return NULL;
}

weivel_c_status weivel_c_host_find_held_id(weivel_c_host *host, const weivel_standard_library *declaration,
                                           const char **held) {
    size_t at;
    *held = NULL;
    for (at = 0; at < declaration->service_count; at++) {
        void *service = NULL;
        weivel_c_status status = weivel_c_context_get_service(host->own_context, declaration->services[at].id,
                                                              &service, NULL);
        if (status == WEIVEL_C_STATUS_OK) {
            *held = declaration->services[at].id;
            return WEIVEL_C_STATUS_OK;
        }
        if (status != WEIVEL_C_STATUS_NONE) {
            return status;
        }
    }
    return WEIVEL_C_STATUS_OK;
}

static int declares_id(const weivel_c_library *library, const char *id) {
    uint32_t at;
    for (at = 0; at < library->service_count; at++) {
        if (strcmp(library->services[at].id, id) == 0) {
            return 1;
        }
    }
    return 0;
}

static int another_library_declares(weivel_c_host *host, const weivel_c_library *library, const char *id) {
    const weivel_c_library *other;
    for (other = host->libraries; other != NULL; other = other->next) {
        if (other != library && !other->unloaded && declares_id(other, id)) {
            return 1;
        }
    }
    return 0;
}

const library_service *weivel_c_host_service_another_library_declares(weivel_c_host *host,
                                                                      const weivel_c_library *library) {
    uint32_t at;
    for (at = 0; at < library->service_count; at++) {
        if (another_library_declares(host, library, library->services[at].id)) {
            return &library->services[at];
        }
    }
    return NULL;
}
