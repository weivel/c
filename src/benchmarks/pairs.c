#include "pairs.h"

int benchmark_measure_pair(const benchmark_path *reference, const benchmark_path *measured,
                           const benchmark_sampling *sampling, benchmark_pair *pair) {
    benchmark_path paths[2];
    benchmark_samples samples;
    int usable;
    paths[0] = *reference;
    paths[1] = *measured;
    if (!benchmark_sample(paths, 2, sampling, &samples)) {
        return 0;
    }
    usable = benchmark_summarise(samples.times[0], samples.batches, &pair->reference) &&
             benchmark_summarise(samples.times[1], samples.batches, &pair->measured) &&
             benchmark_summarise_ratios(samples.times[1], samples.times[0], samples.batches, &pair->round_ratio);
    benchmark_samples_free(&samples);
    return usable;
}

static int summarise_same_language(const benchmark_samples *samples, benchmark_same_language *result) {
    benchmark_summary noise = {0, 0, 0, 0, 0, 0};
    benchmark_summary ratio = {0, 0, 0, 0, 0, 0};
    int usable = benchmark_summarise(samples->times[0], samples->batches, &result->direct) &&
                 benchmark_summarise(samples->times[1], samples->batches, &result->direct_again) &&
                 benchmark_summarise(samples->times[2], samples->batches, &result->through_weivel) &&
                 benchmark_summarise_ratios(samples->times[1], samples->times[0], samples->batches, &noise) &&
                 benchmark_summarise_ratios(samples->times[2], samples->times[0], samples->batches, &ratio);
    result->noise_ratio = usable ? noise.median : 0;
    result->ratio = usable ? ratio.median : 0;
    return usable;
}

int benchmark_measure_same_language(benchmark_same_language_world *world, const benchmark_sampling *sampling,
                                    benchmark_same_language *result) {
    benchmark_path paths[3];
    benchmark_samples samples;
    int usable;
    paths[0].call = benchmark_same_language_call_directly;
    paths[1].call = benchmark_same_language_call_directly;
    paths[2].call = benchmark_same_language_call_through_weivel;
    paths[0].data = paths[1].data = paths[2].data = world;
    if (!benchmark_sample(paths, 3, sampling, &samples)) {
        return 0;
    }
    usable = summarise_same_language(&samples, result);
    benchmark_samples_free(&samples);
    return usable;
}
