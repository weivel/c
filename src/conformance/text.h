#ifndef WEIVEL_C_CONFORMANCE_TEXT_H
#define WEIVEL_C_CONFORMANCE_TEXT_H

/**
 * @file
 * A text the driver grows as it writes JSON: what a run shows, and a vector's `then`, in one notation, so the two can
 * be read back by the standard's reader and compared as values.
 */

#include <stddef.h>
#include <stdint.h>

#include <weivel/json.h>

/**
 * A growing, NUL-terminated text. `failed` is 1 once memory ran out, after which every append does nothing.
 *
 * @implNote No `size`: it is the driver's own and never crosses a boundary.
 */
typedef struct conformance_text {
    char *characters;
    size_t length;
    size_t capacity;
    int failed;
} conformance_text;

void conformance_text_start(conformance_text *text);
void conformance_text_end(conformance_text *text);
void conformance_text_append(conformance_text *text, const char *more);
void conformance_text_append_quoted(conformance_text *text, const char *more);
void conformance_text_append_integer(conformance_text *text, int64_t number);
/** Appends `value` as JSON, its members in the order the reader holds them. */
void conformance_text_append_json(conformance_text *text, const weivel_json_value *value);
/** Takes the characters out, leaving the text empty; NULL when memory ran out. The caller frees them. */
char *conformance_text_take(conformance_text *text);

/** 1 when two values are equal as JSON values are: an object's members in any order, a number by its value. */
int conformance_json_equal(const weivel_json_value *left, const weivel_json_value *right);

#endif
