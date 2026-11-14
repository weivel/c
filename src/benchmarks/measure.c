#define _POSIX_C_SOURCE 200809L

#include "measure.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#endif

int benchmark_now(double *nanoseconds) {
#if defined(_WIN32)
    LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    if (!QueryPerformanceFrequency(&frequency) || !QueryPerformanceCounter(&counter)) {
        return 0;
    }
    *nanoseconds = (double)counter.QuadPart * 1e9 / (double)frequency.QuadPart;
    return 1;
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }
    *nanoseconds = (double)now.tv_sec * 1e9 + (double)now.tv_nsec;
    return 1;
#endif
}

static int time_batch(const benchmark_path *path, uint32_t calls, double *nanoseconds_per_call) {
    double start = 0;
    double stop = 0;
    int done = 1;
    uint32_t call;
    if (!benchmark_now(&start)) {
        return 0;
    }
    for (call = 0; call < calls; call++) {
        done &= path->call(path->data, call);
    }
    if (!benchmark_now(&stop)) {
        return 0;
    }
    *nanoseconds_per_call = calls == 0 ? 0 : (stop - start) / calls;
    return done;
}

static int make_room(benchmark_samples *samples, uint32_t path_count, uint32_t batches) {
    uint32_t path;
    memset(samples, 0, sizeof *samples);
    samples->path_count = path_count;
    samples->batches = batches;
    for (path = 0; path < path_count; path++) {
        samples->times[path] = malloc(sizeof(double) * batches);
        if (samples->times[path] == NULL) {
            return 0;
        }
    }
    return 1;
}

static int warm_up(const benchmark_path *paths, uint32_t path_count, uint32_t calls) {
    double ignored;
    uint32_t path;
    for (path = 0; path < path_count; path++) {
        if (!time_batch(&paths[path], calls, &ignored)) {
            return 0;
        }
    }
    return 1;
}

static int sample_rounds(const benchmark_path *paths, const benchmark_sampling *sampling,
                         benchmark_samples *samples) {
    uint32_t round;
    uint32_t turn;
    for (round = 0; round < sampling->batches; round++) {
        for (turn = 0; turn < samples->path_count; turn++) {
            uint32_t path = (round + turn) % samples->path_count;
            if (!time_batch(&paths[path], sampling->calls_per_batch, &samples->times[path][round])) {
                return 0;
            }
        }
    }
    return 1;
}

int benchmark_sample(const benchmark_path *paths, uint32_t path_count, const benchmark_sampling *sampling,
                     benchmark_samples *samples) {
    int usable;
    if (path_count == 0 || path_count > BENCHMARK_MOST_PATHS || sampling->batches == 0 ||
        sampling->calls_per_batch == 0) {
        memset(samples, 0, sizeof *samples);
        return 0;
    }
    usable = make_room(samples, path_count, sampling->batches) &&
             warm_up(paths, path_count, sampling->warmup_calls) && sample_rounds(paths, sampling, samples);
    if (!usable) {
        benchmark_samples_free(samples);
    }
    return usable;
}

void benchmark_samples_free(benchmark_samples *samples) {
    uint32_t path;
    for (path = 0; path < BENCHMARK_MOST_PATHS; path++) {
        free(samples->times[path]);
        samples->times[path] = NULL;
    }
    samples->path_count = 0;
}

static int compare_values(const void *left, const void *right) {
    double difference = *(const double *)left - *(const double *)right;
    return (difference > 0) - (difference < 0);
}

static double percentile(const double *sorted, uint32_t count, double fraction) {
    double rank = ceil(fraction * count) - 1;
    return sorted[rank < 0 ? 0 : (uint32_t)rank];
}

int benchmark_summarise(const double *values, uint32_t count, benchmark_summary *summary) {
    double *sorted = count == 0 ? NULL : malloc(sizeof(double) * count);
    if (sorted == NULL) {
        return 0;
    }
    memcpy(sorted, values, sizeof(double) * count);
    qsort(sorted, count, sizeof sorted[0], compare_values);
    summary->minimum = sorted[0];
    summary->lower_quartile = percentile(sorted, count, 0.25);
    summary->median = percentile(sorted, count, 0.50);
    summary->upper_quartile = percentile(sorted, count, 0.75);
    summary->percentile_99 = percentile(sorted, count, 0.99);
    summary->maximum = sorted[count - 1];
    free(sorted);
    return 1;
}

int benchmark_summarise_ratios(const double *numerator, const double *denominator, uint32_t count,
                               benchmark_summary *summary) {
    double *ratios = count == 0 ? NULL : malloc(sizeof(double) * count);
    uint32_t round;
    int summarised;
    if (ratios == NULL) {
        return 0;
    }
    for (round = 0; round < count; round++) {
        ratios[round] = denominator[round] > 0 ? numerator[round] / denominator[round] : HUGE_VAL;
    }
    summarised = benchmark_summarise(ratios, count, summary);
    free(ratios);
    return summarised;
}
