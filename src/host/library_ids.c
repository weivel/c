#include "libraries.h"

#include <stddef.h>
#include <string.h>

#include "../context/contexts.h"

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

const char *weivel_c_host_find_registered_id(weivel_c_host *host, const weivel_standard_library *declaration) {
    size_t at;
    for (at = 0; at < declaration->service_count; at++) {
        if (weivel_c_contexts_have_registered(host->contexts, declaration->services[at].id)) {
            return declaration->services[at].id;
        }
    }
    return NULL;
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

int weivel_c_host_a_live_library_declares_held(weivel_c_host *host, const char *id) {
    return another_library_declares(host, NULL, id);
}

const library_service *weivel_c_host_service_already_held(weivel_c_host *host, const weivel_c_library *library) {
    uint32_t at;
    for (at = 0; at < library->service_count; at++) {
        const char *id = library->services[at].id;
        if (another_library_declares(host, library, id) || weivel_c_host_holds_service_id(host, id)) {
            return &library->services[at];
        }
    }
    return NULL;
}
