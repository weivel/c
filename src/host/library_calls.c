#include "libraries.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../core/binary.h"

#define END_OF(type, field) (offsetof(type, field) + sizeof(((type *)0)->field))
#define FIRST_BYTES_SIZE END_OF(weivel_c_bytes, owner)
#define FIRST_ANSWER_SIZE END_OF(weivel_c_library_answer, body)

#if defined(WEIVEL_C_TESTING)
/** How many of the next closes of a library's binary are refused as the platform would, leaving it open. */
uint32_t weivel_c_testing_closes_to_refuse;
/** How many closes of a library's binary were made, refused ones included. */
uint32_t weivel_c_testing_closes_made;
#endif

int weivel_c_host_close_library_binary(void *binary) {
#if defined(WEIVEL_C_TESTING)
    weivel_c_testing_closes_made++;
    if (weivel_c_testing_closes_to_refuse > 0) {
        weivel_c_testing_closes_to_refuse--;
        return 0;
    }
#endif
    return weivel_c_binary_close(binary);
}

void weivel_c_host_drop_library_reference(weivel_c_library *library) {
    if (weivel_c_references_drop(&library->references) && !weivel_c_host_close_library_binary(library->binary)) {
        weivel_c_host_report(library->host, WEIVEL_C_FAILURE_UNLOAD, WEIVEL_C_STATUS_NOT_UNLOADED,
                             "a C library loaded as host services could not be closed");
    }
}

static int is_continuation(uint8_t byte) {
    return (byte & 0xC0u) == 0x80u;
}

/** How long a UTF-8 sequence its first byte says it is, or 0 for a byte no well-formed sequence begins with. */
static size_t length_announced_by(uint8_t first) {
    if (first < 0x80u) {
        return 1;
    }
    if (first >= 0xC2u && first <= 0xDFu) {
        return 2;
    }
    if (first >= 0xE0u && first <= 0xEFu) {
        return 3;
    }
    return first >= 0xF0u && first <= 0xF4u ? 4 : 0;
}

/** The length of the well-formed UTF-8 sequence `bytes` begins, or 0 when it begins none (RFC 3629). */
static size_t sequence_length(const uint8_t *bytes, size_t left) {
    uint8_t first = bytes[0];
    size_t length = length_announced_by(first);
    size_t at;
    if (length == 0 || length > left) {
        return 0;
    }
    for (at = 1; at < length; at++) {
        if (!is_continuation(bytes[at])) {
            return 0;
        }
    }
    if ((first == 0xE0u && bytes[1] < 0xA0u) || (first == 0xEDu && bytes[1] > 0x9Fu) ||
        (first == 0xF0u && bytes[1] < 0x90u) || (first == 0xF4u && bytes[1] > 0x8Fu)) {
        return 0;
    }
    return length;
}

static uint32_t is_utf8(const uint8_t *bytes, size_t length) {
    size_t at = 0;
    while (at < length) {
        size_t step = sequence_length(bytes + at, length - at);
        if (step == 0) {
            return 0;
        }
        at += step;
    }
    return 1;
}

static int answer_is_usable(const weivel_c_library_answer *answer) {
    return answer != NULL && answer->size >= FIRST_ANSWER_SIZE && answer->body.size >= FIRST_BYTES_SIZE &&
           answer->body.owner == NULL;
}

static int call_is_usable(const weivel_c_library_service *service, uint32_t function, const uint8_t *input,
                          size_t input_length, const weivel_c_library_answer *answer) {
    return service != NULL && answer_is_usable(answer) && function < service->function_count &&
           (input != NULL || input_length == 0);
}

static void empty_the_body(weivel_c_bytes *body) {
    body->data = NULL;
    body->length = 0;
    body->owner = NULL;
}

/** Fills the answer with what the function answered; the call's reference passes to a body, when there is one. */
static weivel_c_status take_the_answer(weivel_c_library *library, int32_t status, uint8_t *output, size_t length,
                                       weivel_c_library_answer *answer) {
    answer->status = status;
    answer->body.data = output;
    answer->body.length = output == NULL ? 0 : length;
    answer->body.owner = output == NULL ? NULL : library;
    answer->body_is_utf8 = status != 0 && is_utf8(answer->body.data, answer->body.length);
    if (output == NULL) {
        weivel_c_host_drop_library_reference(library);
    }
    return status == 0 ? WEIVEL_C_STATUS_OK : WEIVEL_C_STATUS_FAILED;
}

weivel_c_status weivel_c_host_call_library(const weivel_c_library_service *service, uint32_t function,
                                           const uint8_t *input, size_t input_length,
                                           weivel_c_library_answer *answer) {
    const library_service *called = (const library_service *)service;
    uint8_t *output = NULL;
    size_t output_length = 0;
    int32_t status;
    if (!call_is_usable(service, function, input, input_length, answer)) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    answer->status = 0;
    answer->body_is_utf8 = 0;
    empty_the_body(&answer->body);
    if (!weivel_c_references_take(&called->library->references)) {
        return WEIVEL_C_STATUS_UNLOADED;
    }
    status = called->functions[function](input, input_length, &output, &output_length);
    return take_the_answer(called->library, status, output, output_length, answer);
}

weivel_c_status weivel_c_bytes_release(weivel_c_bytes *bytes) {
    weivel_c_library *library;
    uint8_t *data;
    if (bytes == NULL || bytes->size < FIRST_BYTES_SIZE) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    library = bytes->owner;
    data = (uint8_t *)bytes->data;
    empty_the_body(bytes);
    if (library != NULL) {
        library->release(data);
        weivel_c_host_drop_library_reference(library);
    }
    return WEIVEL_C_STATUS_OK;
}
