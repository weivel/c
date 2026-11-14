#include "text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void conformance_text_start(conformance_text *text) {
    memset(text, 0, sizeof *text);
}

void conformance_text_end(conformance_text *text) {
    free(text->characters);
    conformance_text_start(text);
}

static int make_room(conformance_text *text, size_t more) {
    size_t wanted = text->length + more + 1;
    size_t capacity = text->capacity == 0 ? 256 : text->capacity;
    char *grown;
    if (text->failed) {
        return 0;
    }
    if (wanted <= text->capacity) {
        return 1;
    }
    while (capacity < wanted) {
        capacity *= 2;
    }
    grown = realloc(text->characters, capacity);
    if (grown == NULL) {
        text->failed = 1;
        return 0;
    }
    text->characters = grown;
    text->capacity = capacity;
    return 1;
}

static void append_bytes(conformance_text *text, const char *bytes, size_t length) {
    if (!make_room(text, length)) {
        return;
    }
    memcpy(text->characters + text->length, bytes, length);
    text->length += length;
    text->characters[text->length] = '\0';
}

void conformance_text_append(conformance_text *text, const char *more) {
    append_bytes(text, more, strlen(more));
}

static void append_escaped(conformance_text *text, unsigned char character) {
    char escaped[8];
    if (character == '"' || character == '\\') {
        escaped[0] = '\\';
        escaped[1] = (char)character;
        append_bytes(text, escaped, 2);
    } else if (character < 0x20) {
        snprintf(escaped, sizeof escaped, "\\u%04x", (unsigned)character);
        append_bytes(text, escaped, 6);
    } else {
        escaped[0] = (char)character;
        append_bytes(text, escaped, 1);
    }
}

static void append_quoted_bytes(conformance_text *text, const char *bytes, size_t length) {
    size_t index;
    append_bytes(text, "\"", 1);
    for (index = 0; index < length; index++) {
        append_escaped(text, (unsigned char)bytes[index]);
    }
    append_bytes(text, "\"", 1);
}

void conformance_text_append_quoted(conformance_text *text, const char *more) {
    append_quoted_bytes(text, more, strlen(more));
}

void conformance_text_append_integer(conformance_text *text, int64_t number) {
    char digits[32];
    snprintf(digits, sizeof digits, "%lld", (long long)number);
    conformance_text_append(text, digits);
}

static void append_number(conformance_text *text, const weivel_json_value *value) {
    int64_t whole = 0;
    double number = 0;
    char digits[40];
    if (weivel_json_as_int64(value, &whole)) {
        conformance_text_append_integer(text, whole);
        return;
    }
    weivel_json_as_double(value, &number);
    snprintf(digits, sizeof digits, "%.17g", number);
    conformance_text_append(text, digits);
}

static void append_array(conformance_text *text, const weivel_json_value *value) {
    size_t index;
    conformance_text_append(text, "[");
    for (index = 0; index < weivel_json_array_length(value); index++) {
        conformance_text_append(text, index == 0 ? "" : ",");
        conformance_text_append_json(text, weivel_json_array_at(value, index));
    }
    conformance_text_append(text, "]");
}

static void append_object(conformance_text *text, const weivel_json_value *value) {
    size_t index;
    conformance_text_append(text, "{");
    for (index = 0; index < weivel_json_object_size(value); index++) {
        conformance_text_append(text, index == 0 ? "" : ",");
        conformance_text_append_quoted(text, weivel_json_object_key_at(value, index));
        conformance_text_append(text, ":");
        conformance_text_append_json(text, weivel_json_object_value_at(value, index));
    }
    conformance_text_append(text, "}");
}

void conformance_text_append_json(conformance_text *text, const weivel_json_value *value) {
    int truth = 0;
    switch (weivel_json_kind_of(value)) {
    case WEIVEL_JSON_BOOL:
        weivel_json_as_bool(value, &truth);
        conformance_text_append(text, truth ? "true" : "false");
        break;
    case WEIVEL_JSON_NUMBER:
        append_number(text, value);
        break;
    case WEIVEL_JSON_STRING:
        append_quoted_bytes(text, weivel_json_as_string(value), weivel_json_string_length(value));
        break;
    case WEIVEL_JSON_ARRAY:
        append_array(text, value);
        break;
    case WEIVEL_JSON_OBJECT:
        append_object(text, value);
        break;
    default:
        conformance_text_append(text, "null");
        break;
    }
}

char *conformance_text_take(conformance_text *text) {
    char *taken;
    append_bytes(text, "", 0);
    taken = text->failed ? NULL : text->characters;
    if (taken == NULL) {
        free(text->characters);
    }
    conformance_text_start(text);
    return taken;
}
