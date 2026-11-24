#ifndef WEIVEL_C_CONFORMANCE_OPS_H
#define WEIVEL_C_CONFORMANCE_OPS_H

/**
 * @file
 * The ops a layer vector may have, each driven through the host's public surface as the kit's docs/vectors/layer/
 * states it.
 */

#include "text.h"
#include "vectors.h"
#include "world.h"

/**
 * Drives the vector on the world, writes what the run shows for every `then` field, and ends the world before it
 * returns, so nothing the host's destroy sets off reaches a run that lived on the op's stack. Answers 1, or 0.
 */
typedef int (*conformance_drive)(conformance_world *world, const conformance_vector *vector,
                                 conformance_text *observed);

int conformance_drive_consume(conformance_world *world, const conformance_vector *vector, conformance_text *observed);
int conformance_drive_want(conformance_world *world, const conformance_vector *vector, conformance_text *observed);
int conformance_drive_lifecycle(conformance_world *world, const conformance_vector *vector,
                                conformance_text *observed);
int conformance_drive_library(conformance_world *world, const conformance_vector *vector, conformance_text *observed);

/**
 * The core options a `want` vector runs under: its service is well-known, so the provider registers it as named.
 * `well_known` holds the one id and must outlive the options.
 */
void conformance_want_core_options(const conformance_vector *vector, weivel_core_options *options,
                                   const char **well_known);

#endif
