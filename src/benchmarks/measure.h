#ifndef WEIVEL_C_BENCHMARKS_MEASURE_H
#define WEIVEL_C_BENCHMARKS_MEASURE_H

/**
 * @file
 * How the `benchmarks` module times a call: warm-up calls first, then batches of calls, each batch timed as one and
 * divided by its calls. The measured paths are sampled in turn, one batch of each per round, so a change in the
 * machine's speed during the run falls on every path alike, and the path that goes first moves on by one each round,
 * so no path always follows the same one. Two paths are compared by the ratio of their batches in the same round.
 *
 * None of it is exported: the module is its own library, linked by its tests and by nothing of the layer's.
 */

#include <stddef.h>
#include <stdint.h>

#define BENCHMARK_MOST_PATHS 4

typedef struct benchmark_sampling {
    uint32_t warmup_calls;
    uint32_t calls_per_batch;
    uint32_t batches;
} benchmark_sampling;

/** One call of a measured path. `call` counts from 0, so no call is the same as the one before. Answers 1 when the
 * call did what it measures, 0 when it did not, which makes the whole sampling unusable. */
typedef int (*benchmark_call)(void *data, uint32_t call);

typedef struct benchmark_path {
    benchmark_call call;
    void *data;
} benchmark_path;

/** Nanoseconds per call over a set of batches, or a ratio of two paths' batches over a set of rounds. */
typedef struct benchmark_summary {
    double minimum;
    double lower_quartile;
    double median;
    double upper_quartile;
    double percentile_99;
    double maximum;
} benchmark_summary;

/** Each path's batch times, nanoseconds per call, one per round: `times[path][round]`. */
typedef struct benchmark_samples {
    uint32_t path_count;
    uint32_t batches;
    double *times[BENCHMARK_MOST_PATHS];
} benchmark_samples;

/** Reads a monotonic clock in nanoseconds. Answers 1, or 0 when the platform would not. */
int benchmark_now(double *nanoseconds);

/**
 * Samples `path_count` paths, at most BENCHMARK_MOST_PATHS, under `sampling`. Answers 1 with `samples` filled, to be
 * handed to benchmark_samples_free; 0 with nothing held when memory ran out, the clock failed or a call answered 0.
 */
int benchmark_sample(const benchmark_path *paths, uint32_t path_count, const benchmark_sampling *sampling,
                     benchmark_samples *samples);

void benchmark_samples_free(benchmark_samples *samples);

/** Summarises `count` values, which it leaves as they were. Answers 1, or 0 for none or when memory ran out. */
int benchmark_summarise(const double *values, uint32_t count, benchmark_summary *summary);

/** Summarises the ratio of `numerator` over `denominator` in each of `count` rounds, as benchmark_summarise answers. */
int benchmark_summarise_ratios(const double *numerator, const double *denominator, uint32_t count,
                               benchmark_summary *summary);

#endif
