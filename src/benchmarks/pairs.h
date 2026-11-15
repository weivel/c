#ifndef WEIVEL_C_BENCHMARKS_PAIRS_H
#define WEIVEL_C_BENCHMARKS_PAIRS_H

/**
 * @file
 * The comparisons the gates judge: a measured path against a reference path sampled beside it, and the same-language
 * call against the same call made directly, twice, so the run's own noise is measured with it.
 */

#include "measure.h"
#include "worlds.h"

typedef struct benchmark_pair {
    benchmark_summary reference;
    benchmark_summary measured;
    /** The measured path's batch over the reference's in the same round, per round. */
    benchmark_summary round_ratio;
} benchmark_pair;

/** Samples `measured` and `reference` interleaved. Answers 1 with `pair` filled, or 0 when the sampling was unusable. */
int benchmark_measure_pair(const benchmark_path *reference, const benchmark_path *measured,
                           const benchmark_sampling *sampling, benchmark_pair *pair);

typedef struct benchmark_same_language {
    benchmark_summary direct;
    benchmark_summary direct_again;
    benchmark_summary through_weivel;
    /** The per-round median of the second direct path over the first: two paths of identical cost, so only noise. */
    double noise_ratio;
    /** The per-round median of the call through what the consumer resolved over the direct call. */
    double ratio;
} benchmark_same_language;

/**
 * Samples `call` on three objects interleaved: `direct` and `direct_again` the provider's own, `through_weivel` what
 * the layer answered. Answers 1 with `result` filled, or 0.
 */
int benchmark_measure_same_language(benchmark_call call, void *direct, void *direct_again, void *through_weivel,
                                    const benchmark_sampling *sampling, benchmark_same_language *result);

#endif
