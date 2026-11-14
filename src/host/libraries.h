#ifndef WEIVEL_C_HOST_LIBRARIES_H
#define WEIVEL_C_HOST_LIBRARIES_H

/**
 * @file
 * What the sources of C libraries as host services share. None of it is exported from the shared library.
 *
 * A library's record, its services and their objects live until weivel_c_host_free, so an object a consumer still
 * holds after the library was unloaded is still memory it may read, and a call through it is refused by the record's
 * references rather than reaching a closed binary.
 */

#include <stddef.h>
#include <stdint.h>

#include <weivel/c/host/library.h>

#include "host.h"
#include "references.h"

/** A declared function of the bytes kind, as WEIVEL_STANDARD_FUNCTION_BYTES gives its signature. */
typedef int32_t (*library_function)(const uint8_t *input, size_t input_length, uint8_t **output,
                                    size_t *output_length);

/** A declaration's release symbol. */
typedef void (*library_release)(uint8_t *buffer);

/** One declared service. Its object comes first, so the object a consumer calls through leads back to it. */
typedef struct library_service {
    weivel_c_library_service object;
    weivel_c_library *library;
    library_function *functions;
    char **names;
    char *id;
    weivel_handle registration;
    /** 1 while its registration under WEIVEL_HOST stands. */
    uint32_t registered;
} library_service;

struct weivel_c_library {
    weivel_c_host *host;
    /** The host's next library, the one loaded before; guarded by the host's lock. */
    weivel_c_library *next;
    weivel_c_references references;
    void *binary;
    library_release release;
    /** 1 once an unload or the destroy took the library; guarded by the host's lock. */
    uint32_t unloaded;
    uint32_t service_count;
    library_service *services;
};

/** Calls one declared function through a service's object; what every object's `call` is. */
weivel_c_status weivel_c_host_call_library(const weivel_c_library_service *service, uint32_t function,
                                           const uint8_t *input, size_t input_length,
                                           weivel_c_library_answer *answer);

/** Closes a library's binary. Answers 1, or 0 when the platform refused. */
int weivel_c_host_close_library_binary(void *binary);

/** Drops a reference on a library, closing its binary when it was the last one, and tells the sink when that fails. */
void weivel_c_host_drop_library_reference(weivel_c_library *library);

#endif
