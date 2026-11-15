#ifndef WEIVEL_C_BENCHMARKS_BUDGET_H
#define WEIVEL_C_BENCHMARKS_BUDGET_H

/**
 * @file
 * The bounds the `benchmarks` module gates against, every one the kit's and none the layer's own: the core's control
 * call budget, `benchmarks/budget.txt` of the pinned kit, and the C layer's own, `benchmarks/c.txt`, read as the
 * kit's RUNNER.md lays a language's budget out. The caller names both files; the module has no path of its own.
 */

#include <stddef.h>

typedef enum benchmark_gate {
    /** The wrapper's control call over the same call made through the bare table. */
    BENCHMARK_CONTROL_CALL = 0,
    /** A C library's function called through the host's library service over the same function called directly. */
    BENCHMARK_LIBRARY_CALL = 1
} benchmark_gate;

#define BENCHMARK_GATES 2
#define BENCHMARK_PLATFORMS 3
#define BENCHMARK_PROBLEM 256

/**
 * A language's budget: each ratio gate's highest measured ratio, each noise bound and the noise margin per kit
 * platform, 0 where no line gives one, and the ratio margin.
 */
typedef struct benchmark_budget {
    double highest[BENCHMARK_GATES][BENCHMARK_PLATFORMS];
    double quiet_noise[BENCHMARK_PLATFORMS];
    double worst_noise[BENCHMARK_PLATFORMS];
    double noise_margin[BENCHMARK_PLATFORMS];
    double ratio_margin;
} benchmark_budget;

/** One platform's noise bounds and noise margin, each 0 where the budget gives no line. */
typedef struct benchmark_noise_bounds {
    double quiet;
    double worst;
    double margin;
} benchmark_noise_bounds;

/**
 * Reads a language's budget file. A gate, noise or noise margin line missing for a platform is no bound there, which
 * fails that gate when it runs there; a line the format does not define, a single `noise-margin` among them, a number
 * that is not positive, a line given twice or a missing ratio margin makes the file unusable.
 *
 * @return 1; 0 with `problem` saying why in one line.
 */
int benchmark_read_budget(const char *path, benchmark_budget *budget, char *problem, size_t problem_size);

/** Reads the core's budget file, one line `median_nanoseconds <positive number>`. Answers 1, or 0 with `problem`. */
int benchmark_read_core_budget(const char *path, double *median_nanoseconds, char *problem, size_t problem_size);

/** The kit's name of the platform this build runs on, `linux`, `windows` or `macos`; NULL for any other. */
const char *benchmark_platform(void);

/** The bound of `gate` on `platform`, its highest measured ratio times the margin; 0 when the budget gives none. */
double benchmark_ratio_bound(const benchmark_budget *budget, benchmark_gate gate, const char *platform);

/** How far `ratio` is from 1, the same either way: 1.1 and 1/1.1 are both 0.1 away. */
double benchmark_deviation(double ratio);

/** The noise bounds and noise margin of `platform`, each 0 for a platform the budget gives no line. */
benchmark_noise_bounds benchmark_noise_bounds_of(const benchmark_budget *budget, const char *platform);

/** 1 when the budget gave both noise bounds and the noise margin, so the same-language gate can judge at all. */
int benchmark_noise_bounded(benchmark_noise_bounds bounds);

/** 1 when a run whose two identical direct paths differed by `noise_ratio` was noisier than any the kit measured. */
int benchmark_too_noisy_to_judge(benchmark_noise_bounds bounds, double noise_ratio);

/**
 * How far from 1 a same-language pair's ratio may be in a run whose identical direct paths differed by `noise_ratio`:
 * the platform's noise margin times that run's own noise, or times the platform's quiet noise when the run was
 * quieter.
 */
double benchmark_same_language_bound(benchmark_noise_bounds bounds, double noise_ratio);

/** 1 when `ratio` lies within `bound` of 1 either way. */
int benchmark_within(double ratio, double bound);

#endif
