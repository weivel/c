#include "libraries.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../core/binary.h"

#define END_OF(type, field) (offsetof(type, field) + sizeof(((type *)0)->field))
#define FIRST_DECLARATION_SIZE END_OF(weivel_standard_library, storage)
#define FIRST_FAILURE_SIZE END_OF(weivel_c_library_load_failure, wrong_kind)
#define TAKEN_ID_FAILURE_SIZE END_OF(weivel_c_library_load_failure, taken_id)

#if defined(WEIVEL_C_TESTING)
/**
 * When not 0, how many registrations of a library's services from now the one refused as out of memory is: 1 refuses
 * the next. The seam a test fails a later service's registration through.
 */
uint32_t weivel_c_testing_registration_to_refuse;

static int is_refused_for_testing(void) {
    if (weivel_c_testing_registration_to_refuse == 0) {
        return 0;
    }
    weivel_c_testing_registration_to_refuse--;
    return weivel_c_testing_registration_to_refuse == 0;
}
#else
static int is_refused_for_testing(void) {
    return 0;
}
#endif

static int is_text(const char *text) {
    return text != NULL && text[0] != '\0';
}

static int is_writable(const weivel_c_library_load_failure *failure) {
    return failure != NULL && failure->size >= FIRST_FAILURE_SIZE;
}

static int holds_some_items(const void *items, size_t count) {
    return items != NULL && count > 0 && count <= UINT32_MAX;
}

static void tell_taken_id(weivel_c_library_load_failure *failure, const char *id) {
    if (failure != NULL && failure->size >= TAKEN_ID_FAILURE_SIZE) {
        failure->taken_id = id;
    }
}

static weivel_c_status check_function(const weivel_standard_library_function *function,
                                      weivel_c_library_load_failure *failure) {
    if (!is_text(function->name) || !is_text(function->symbol)) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    if (function->kind != WEIVEL_STANDARD_FUNCTION_BYTES) {
        if (failure != NULL) {
            failure->wrong_kind = function->name;
        }
        return WEIVEL_C_STATUS_MISUSE;
    }
    return WEIVEL_C_STATUS_OK;
}

static weivel_c_status check_service(const weivel_standard_library_service *service,
                                     weivel_c_library_load_failure *failure) {
    weivel_c_status status = WEIVEL_C_STATUS_OK;
    size_t at;
    if (!is_text(service->id) || !holds_some_items(service->functions, service->function_count)) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    for (at = 0; at < service->function_count && status == WEIVEL_C_STATUS_OK; at++) {
        status = check_function(&service->functions[at], failure);
    }
    return status;
}

/**
 * Checks everything of the declaration the host relies on before anything is opened: the caller's mistakes.
 *
 * @implNote A declaration of no service, or a service of no function, is refused, as the standard's reader refuses
 * both, so a declaration the caller builds cannot load what a read one never could.
 */
static weivel_c_status check_declaration(const weivel_standard_library *declaration,
                                         weivel_c_library_load_failure *failure) {
    weivel_c_status status = WEIVEL_C_STATUS_OK;
    const char *repeated;
    size_t at;
    if (declaration->size < FIRST_DECLARATION_SIZE || !is_text(declaration->release) ||
        !holds_some_items(declaration->services, declaration->service_count)) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    for (at = 0; at < declaration->service_count && status == WEIVEL_C_STATUS_OK; at++) {
        status = check_service(&declaration->services[at], failure);
    }
    repeated = status == WEIVEL_C_STATUS_OK ? weivel_c_host_repeated_id(declaration) : NULL;
    if (repeated != NULL) {
        tell_taken_id(failure, repeated);
        return WEIVEL_C_STATUS_MISUSE;
    }
    return status;
}

static weivel_c_status check_before_opening(weivel_c_host *host, const weivel_standard_library *declaration,
                                            weivel_c_library_load_failure *failure) {
    const char *registered;
    weivel_c_status status = check_declaration(declaration, failure);
    status = status == WEIVEL_C_STATUS_OK && !weivel_c_host_is_running(host) ? WEIVEL_C_STATUS_DESTROYED : status;
    if (status != WEIVEL_C_STATUS_OK) {
        return status;
    }
    registered = weivel_c_host_find_registered_id(host, declaration);
    if (registered != NULL) {
        tell_taken_id(failure, registered);
        return WEIVEL_C_STATUS_MISUSE;
    }
    return WEIVEL_C_STATUS_OK;
}

static weivel_c_status find_symbol(void *binary, const char *symbol, weivel_c_binary_function *found,
                                   weivel_c_library_load_failure *failure) {
    *found = weivel_c_binary_find(binary, symbol);
    if (*found == NULL) {
        if (failure != NULL) {
            failure->missing_symbol = symbol;
        }
        return WEIVEL_C_STATUS_MISSING_SYMBOL;
    }
    return WEIVEL_C_STATUS_OK;
}

static char *copy_text(const char *text) {
    size_t length = strlen(text) + 1;
    char *copy = malloc(length);
    if (copy != NULL) {
        memcpy(copy, text, length);
    }
    return copy;
}

static void fill_object(library_service *made) {
    made->object.size = (uint32_t)sizeof made->object;
    made->object.id = made->id;
    made->object.function_names = (const char *const *)made->names;
    made->object.call = weivel_c_host_call_library;
    made->object.release_bytes = weivel_c_bytes_release;
}

/** Binds each declared function of a service and copies every text it keeps. */
static weivel_c_status build_service(library_service *made, weivel_c_library *library,
                                     const weivel_standard_library_service *declared,
                                     weivel_c_library_load_failure *failure) {
    size_t count = declared->function_count;
    size_t at;
    made->library = library;
    made->id = copy_text(declared->id);
    made->functions = calloc(count, sizeof *made->functions);
    made->names = calloc(count, sizeof *made->names);
    if (made->id == NULL || made->functions == NULL || made->names == NULL) {
        return WEIVEL_C_STATUS_OUT_OF_MEMORY;
    }
    made->object.function_count = (uint32_t)count;
    for (at = 0; at < count; at++) {
        weivel_c_binary_function found;
        weivel_c_status status = find_symbol(library->binary, declared->functions[at].symbol, &found, failure);
        if (status != WEIVEL_C_STATUS_OK) {
            return status;
        }
        made->functions[at] = (library_function)found;
        made->names[at] = copy_text(declared->functions[at].name);
        if (made->names[at] == NULL) {
            return WEIVEL_C_STATUS_OUT_OF_MEMORY;
        }
    }
    fill_object(made);
    return WEIVEL_C_STATUS_OK;
}

static weivel_c_status build_library(weivel_c_library *made, const weivel_standard_library *declaration,
                                     weivel_c_library_load_failure *failure) {
    weivel_c_binary_function release;
    weivel_c_status status = find_symbol(made->binary, declaration->release, &release, failure);
    size_t at;
    if (status != WEIVEL_C_STATUS_OK) {
        return status;
    }
    made->release = (library_release)release;
    made->services = calloc(declaration->service_count, sizeof *made->services);
    if (made->services == NULL) {
        return WEIVEL_C_STATUS_OUT_OF_MEMORY;
    }
    made->service_count = (uint32_t)declaration->service_count;
    for (at = 0; at < declaration->service_count && status == WEIVEL_C_STATUS_OK; at++) {
        status = build_service(&made->services[at], made, &declaration->services[at], failure);
    }
    return status;
}

static void free_service(library_service *service) {
    uint32_t at;
    for (at = 0; service->names != NULL && at < service->object.function_count; at++) {
        free(service->names[at]);
    }
    free(service->names);
    free(service->functions);
    free(service->id);
}

static void free_library(weivel_c_library *library) {
    uint32_t at;
    for (at = 0; library->services != NULL && at < library->service_count; at++) {
        free_service(&library->services[at]);
    }
    free(library->services);
    free(library);
}

static weivel_c_status register_one(library_service *service, weivel_c_refusal *refusal) {
    weivel_c_status status;
    if (is_refused_for_testing()) {
        return WEIVEL_C_STATUS_OUT_OF_MEMORY;
    }
    status = weivel_c_host_register_kept_service(service->library->host, service->id, &service->object, NULL,
                                                 &service->registration, refusal);
    service->registered = status == WEIVEL_C_STATUS_OK;
    return status;
}

static weivel_c_status register_services(weivel_c_library *library, weivel_c_refusal *refusal) {
    uint32_t at;
    for (at = 0; at < library->service_count; at++) {
        weivel_c_status status = register_one(&library->services[at], refusal);
        if (status != WEIVEL_C_STATUS_OK) {
            return status;
        }
    }
    return WEIVEL_C_STATUS_OK;
}

static void unregister_services(weivel_c_library *library) {
    uint32_t at;
    for (at = 0; at < library->service_count; at++) {
        library_service *service = &library->services[at];
        if (service->registered) {
            service->registered = 0;
            weivel_c_host_report_call(library->host,
                                      weivel_c_host_unregister_service(library->host, service->registration, NULL),
                                      "the host did not unregister a service of a C library it unloaded");
        }
    }
}

/**
 * Keeps a library on the host's list before anything of it is registered, so every later ending of it finds it, the
 * destroy's among them, unless the host has begun to be destroyed or another live library declares one of its ids.
 *
 * @implNote The check and the keeping are one step under the host's lock, so two loads declaring one id cannot both
 * be kept.
 */
static weivel_c_status keep_library(weivel_c_library *library, const weivel_standard_library *declaration,
                                    weivel_c_library_load_failure *failure) {
    weivel_c_host *host = library->host;
    const library_service *taken;
    weivel_c_status status;
    weivel_c_mutex_lock(&host->lock);
    taken = weivel_c_host_service_another_library_declares(host, library);
    status = host->state != HOST_RUNNING ? WEIVEL_C_STATUS_DESTROYED
             : taken != NULL             ? WEIVEL_C_STATUS_MISUSE
                                         : WEIVEL_C_STATUS_OK;
    if (status == WEIVEL_C_STATUS_OK) {
        library->next = host->libraries;
        host->libraries = library;
    }
    weivel_c_mutex_unlock(&host->lock);
    if (status == WEIVEL_C_STATUS_MISUSE) {
        tell_taken_id(failure, declaration->services[taken - library->services].id);
    }
    return status;
}

/** Undoes a load that kept and registered nothing: frees what it built and closes the binary it opened. */
static void discard(weivel_c_host *host, weivel_c_library *made, void *binary) {
    int closed;
    if (made == NULL) {
        closed = weivel_c_binary_close(binary);
    } else {
        closed = weivel_c_host_close_service_library(made);
        free_library(made);
    }
    if (!closed) {
        weivel_c_host_report_unclosed(host, "a C library whose load the host undid could not be closed");
    }
}

static weivel_c_status open_and_build(weivel_c_host *host, const char *path,
                                      const weivel_standard_library *declaration, weivel_c_library **made,
                                      weivel_c_library_load_failure *failure, void **binary) {
    weivel_c_status status = weivel_c_binary_open(path, binary);
    if (status != WEIVEL_C_STATUS_OK) {
        return status;
    }
    *made = calloc(1, sizeof **made);
    if (*made == NULL) {
        return WEIVEL_C_STATUS_OUT_OF_MEMORY;
    }
    (*made)->host = host;
    (*made)->binary = *binary;
    weivel_c_references_start(&(*made)->references);
    return build_library(*made, declaration, failure);
}

static void clear_failure(weivel_c_library_load_failure *failure) {
    if (is_writable(failure)) {
        failure->missing_symbol = NULL;
        failure->wrong_kind = NULL;
        tell_taken_id(failure, NULL);
    }
}

/** Drops the host's own reference, closing the binary now when nothing else holds the library. */
static weivel_c_status close_once_unheld(weivel_c_library *library) {
    if (!weivel_c_references_drop(&library->references)) {
        return WEIVEL_C_STATUS_OK;
    }
    return weivel_c_host_close_service_library(library) ? WEIVEL_C_STATUS_OK : WEIVEL_C_STATUS_NOT_UNLOADED;
}

static int take_for_unloading(weivel_c_library *library) {
    weivel_c_host *host = library->host;
    int taken;
    weivel_c_mutex_lock(&host->lock);
    taken = !library->unloaded;
    library->unloaded = 1;
    weivel_c_mutex_unlock(&host->lock);
    return taken;
}

/** Closes a library to calls, unregisters what it registered, and closes its binary once nothing holds it. */
static weivel_c_status unload_taken(weivel_c_library *library) {
    weivel_c_references_close(&library->references);
    unregister_services(library);
    return close_once_unheld(library);
}

/**
 * Undoes a load the host kept, through the unload path, unless the destroy took the library first and unloads it
 * itself. Its record stays until weivel_c_host_free, since a consumer told of one of its objects may still hold it.
 */
static void undo_kept(weivel_c_library *library) {
    if (take_for_unloading(library) && unload_taken(library) != WEIVEL_C_STATUS_OK) {
        weivel_c_host_report_unclosed(library->host, "a C library whose load the host undid could not be closed");
    }
}

static weivel_c_status load_checked(weivel_c_host *host, const char *path, const weivel_standard_library *declaration,
                                    weivel_c_library **library, weivel_c_library_load_failure *failure,
                                    weivel_c_refusal *refusal) {
    weivel_c_library *made = NULL;
    void *binary = NULL;
    weivel_c_status status = open_and_build(host, path, declaration, &made, failure, &binary);
    status = status == WEIVEL_C_STATUS_OK ? keep_library(made, declaration, failure) : status;
    if (status != WEIVEL_C_STATUS_OK) {
        if (binary != NULL) {
            discard(host, made, binary);
        }
        return status;
    }
    status = register_services(made, refusal);
    status = status == WEIVEL_C_STATUS_OK && !weivel_c_host_is_running(host) ? WEIVEL_C_STATUS_DESTROYED : status;
    if (status != WEIVEL_C_STATUS_OK) {
        undo_kept(made);
        return status;
    }
    *library = made;
    return WEIVEL_C_STATUS_OK;
}

weivel_c_status weivel_c_host_load_library(weivel_c_host *host, const char *path,
                                           const weivel_standard_library *declaration, weivel_c_library **library,
                                           weivel_c_library_load_failure *failure, weivel_c_refusal *refusal) {
    weivel_c_status status;
    if (library == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    *library = NULL;
    clear_failure(failure);
    if (host == NULL || !is_text(path) || declaration == NULL || (failure != NULL && !is_writable(failure))) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    status = check_before_opening(host, declaration, failure);
    if (status != WEIVEL_C_STATUS_OK) {
        return status;
    }
    return load_checked(host, path, declaration, library, failure, refusal);
}

weivel_c_status weivel_c_host_unload_library(weivel_c_host *host, weivel_c_library *library) {
    if (host == NULL || library == NULL || library->host != host) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    if (!weivel_c_host_is_running(host)) {
        return WEIVEL_C_STATUS_DESTROYED;
    }
    if (!take_for_unloading(library)) {
        return WEIVEL_C_STATUS_NONE;
    }
    return unload_taken(library);
}

static weivel_c_library *next_to_unload(weivel_c_host *host) {
    weivel_c_library *library;
    weivel_c_mutex_lock(&host->lock);
    library = host->libraries;
    while (library != NULL && library->unloaded) {
        library = library->next;
    }
    if (library != NULL) {
        library->unloaded = 1;
    }
    weivel_c_mutex_unlock(&host->lock);
    return library;
}

void weivel_c_host_unload_every_library(weivel_c_host *host) {
    weivel_c_library *library;
    while ((library = next_to_unload(host)) != NULL) {
        weivel_c_references_close(&library->references);
        if (close_once_unheld(library) != WEIVEL_C_STATUS_OK) {
            weivel_c_host_report(host, WEIVEL_C_FAILURE_UNLOAD, WEIVEL_C_STATUS_NOT_UNLOADED,
                                 "a C library loaded as host services could not be closed as the host was destroyed");
        }
    }
}

int weivel_c_host_libraries_are_held(weivel_c_host *host) {
    weivel_c_library *library;
    for (library = host->libraries; library != NULL; library = library->next) {
        if (weivel_c_references_held(&library->references)) {
            return 1;
        }
    }
    return 0;
}

void weivel_c_host_free_libraries(weivel_c_host *host) {
    while (host->libraries != NULL) {
        weivel_c_library *library = host->libraries;
        host->libraries = library->next;
        free_library(library);
    }
}
