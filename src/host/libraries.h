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

/** Closes the binary of a library, a plugin entry's or one loaded as host services. Answers 1, or 0 when refused. */
int weivel_c_host_close_library_binary(void *binary);

/** Closes the binary of a library loaded as host services, as weivel_c_host_close_library_binary answers. */
int weivel_c_host_close_service_library(weivel_c_library *library);

/** Tells the sink that a library's binary did not close, unless the host has been destroyed, which is quiet after. */
void weivel_c_host_report_unclosed(weivel_c_host *host, const char *text);

/** Drops a reference on a library, closing its binary when it was the last one, and reports when that fails. */
void weivel_c_host_drop_library_reference(weivel_c_library *library);

/** The id a later service of the declaration repeats from an earlier one, the declaration's own text, or NULL. */
const char *weivel_c_host_repeated_id(const weivel_standard_library *declaration);

/**
 * The first declared id a registration under the host's core stands for now, whoever owns it, the declaration's own
 * text; NULL when none does.
 *
 * @implNote It reads the layer's records of every context rather than getting the id as WEIVEL_HOST: the host holds
 * what a get hands it, so a get would leave it holding the registration it found and hearing it revoked later.
 * Every registration under the core is made through a context, so the records see each one; a registration whose
 * ending the layer has not yet taken in reads as standing, which refuses a load the core would have taken.
 */
const char *weivel_c_host_find_registered_id(weivel_c_host *host, const weivel_standard_library *declaration);

/** Under the host's lock: 1 when a library the host loaded and has not unloaded declares `id`. */
int weivel_c_host_a_live_library_declares_held(weivel_c_host *host, const char *id);

/**
 * Under the host's lock: the service of `library` whose id another library on the host's list, not yet unloaded,
 * declares too, or a service the host registered or is registering holds, or NULL. It covers a library whose load
 * has kept it and not finished registering, and a host registration whose core call has not answered yet, which no
 * record and no get sees.
 */
const library_service *weivel_c_host_service_already_held(weivel_c_host *host, const weivel_c_library *library);

#endif
