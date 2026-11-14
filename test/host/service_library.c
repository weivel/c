/**
 * @file
 * A C library of the bytes kind the host tests load as a host service, built as a shared library that links nothing.
 * Its functions tell the test what crossed: the address of the input it was handed, the address of the body it
 * answered, a status and body the test asks for, and each release, counted where the test attached a counter.
 */

#include "service_library.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define SERVICE_LIBRARY_EXPORT __declspec(dllexport)
#else
#define SERVICE_LIBRARY_EXPORT __attribute__((visibility("default")))
#endif

static uint32_t *releases;

static int32_t answer_copy(const void *bytes, size_t length, int32_t status, uint8_t **output, size_t *output_length) {
    uint8_t *copy = malloc(length == 0 ? 1 : length);
    *output = copy;
    *output_length = copy == NULL ? 0 : length;
    if (copy == NULL) {
        return 2;
    }
    if (length > 0) {
        memcpy(copy, bytes, length);
    }
    return status;
}

static int input_holds_a_pointer(const uint8_t *input, size_t input_length) {
    return input != NULL && input_length == sizeof(void *);
}

SERVICE_LIBRARY_EXPORT int32_t service_input_address(const uint8_t *input, size_t input_length, uint8_t **output,
                                                     size_t *output_length) {
    (void)input_length;
    return answer_copy(&input, sizeof input, 0, output, output_length);
}

SERVICE_LIBRARY_EXPORT int32_t service_own_address(const uint8_t *input, size_t input_length, uint8_t **output,
                                                   size_t *output_length) {
    int32_t status = answer_copy(&input, sizeof input, 0, output, output_length);
    (void)input_length;
    if (*output != NULL) {
        const void *address = *output;
        memcpy(*output, &address, sizeof address);
    }
    return status;
}

SERVICE_LIBRARY_EXPORT int32_t service_attach(const uint8_t *input, size_t input_length, uint8_t **output,
                                              size_t *output_length) {
    *output = NULL;
    *output_length = 0;
    if (!input_holds_a_pointer(input, input_length)) {
        return 1;
    }
    memcpy(&releases, input, sizeof releases);
    return 0;
}

SERVICE_LIBRARY_EXPORT int32_t service_answer(const uint8_t *input, size_t input_length, uint8_t **output,
                                              size_t *output_length) {
    if (input_length == 0) {
        *output = NULL;
        *output_length = 0;
        return 0;
    }
    return answer_copy(input + 1, input_length - 1, (int32_t)input[0], output, output_length);
}

SERVICE_LIBRARY_EXPORT int32_t service_call_back(const uint8_t *input, size_t input_length, uint8_t **output,
                                                 size_t *output_length) {
    const service_library_callback *callback;
    if (!input_holds_a_pointer(input, input_length)) {
        return answer_copy(NULL, 0, 1, output, output_length);
    }
    memcpy(&callback, input, sizeof callback);
    callback->run(callback->data);
    return answer_copy("done", 4, 0, output, output_length);
}

SERVICE_LIBRARY_EXPORT void service_release(uint8_t *buffer) {
    free(buffer);
    if (buffer != NULL && releases != NULL) {
        (*releases)++;
    }
}
