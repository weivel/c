#ifndef WEIVEL_C_TEST_HOST_SERVICE_LIBRARY_H
#define WEIVEL_C_TEST_HOST_SERVICE_LIBRARY_H

/**
 * @file
 * What the test service library and its tests share. Every function is of the bytes kind; a pointer crosses as the
 * bytes of the pointer itself.
 *
 * - `service_input_address` answers the address of the input it was handed.
 * - `service_own_address` answers a body holding its own address.
 * - `service_attach` takes a `uint32_t *` that every later release of a body increments.
 * - `service_answer` answers the status its input's first byte holds and the rest of its input as the body.
 * - `service_call_back` takes a `service_library_callback *`, runs it, and answers "done".
 * - `service_release` frees a body.
 */

#include <stddef.h>
#include <stdint.h>

typedef struct service_library_callback {
    void (*run)(void *data);
    void *data;
} service_library_callback;

#endif
