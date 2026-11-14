#ifndef WEIVEL_C_CONFORMANCE_OBSERVE_H
#define WEIVEL_C_CONFORMANCE_OBSERVE_H

/**
 * @file
 * Answering a vector's `then` in its own shape: an `{ "equals": ... }` field as `{ "equals": <what the run showed> }`,
 * any other field as what the run showed, so the answer equals `then` exactly when the run witnessed it. A field no
 * observer answers is a problem, never a pass.
 */

#include <weivel/c/core.h>

#include "text.h"
#include "world.h"

/** Writes what the run shows for one field, handed the field's expectation for a matcher such as `before`. */
typedef void (*conformance_observer)(void *run, const weivel_json_value *expectation, conformance_text *out);

/** @implNote No `size`: the driver's own. */
typedef struct conformance_field {
    const char *name;
    conformance_observer observe;
} conformance_field;

/** Writes the observed object for every field of `then`. Answers 1, or 0 with the world's problem written. */
int conformance_observe(conformance_world *world, const weivel_json_value *then, const conformance_field *fields,
                        uint32_t count, void *run, conformance_text *out);

/** Writes a refusal as a kit vector states it: `{ "code": <its kit name>, "subject": ..., "hasFix": ... }`. */
void conformance_write_refusal(conformance_text *out, uint32_t code, const char *subject, uint32_t has_fix);

/** The kit's name for a refusal code, `timed_out` for WEIVEL_REFUSAL_TIMED_OUT; NULL for a code it has none for. */
const char *conformance_code_name(uint32_t code);

/** Writes `count` texts as a JSON array of strings, sorted when `sorted` is 1. */
void conformance_write_names(conformance_text *out, const char **names, uint32_t count, int sorted);

#endif
