/**
 * @file
 * The C layer's benchmarks, one mode a run:
 *
 * - `identity <core>`: a consumer plugin resolves another plugin's service through its context and is handed the
 *   provider's own pointer, and calls through it cross into the core not once; `identity-copied <core>` resolves
 *   through a wrapper that copies the object, which the gate must refuse.
 * - `control-budget <core> <budget.txt> [brief]`: the wrapper's control call against the core's budget.
 * - `control-ratio <core> <c.txt> [brief]`, `library-ratio <core> <library> <c.txt> [brief]` and
 *   `same-language <core> <c.txt> [brief]`: the ratio gates and the same-language noise gate against the C layer's
 *   budget, failing on a platform the budget gives no line.
 * - `same-language-judge <core> <c.txt> <ratio> <noise-ratio>`: the same-language gate's verdict on the ratios given,
 *   measured by nobody, so the bounds it judges by are tested on every platform whatever its clock resolves.
 * - `measure <core> <library> <repetitions>`: prints every measurement the bounds are set from.
 *
 * Exits 0 when the gate holds, 1 when it does not, 2 when nothing usable was measured or read. `brief` samples a
 * fraction of the gate's calls, for a test that needs a measurement and no verdict on its size.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "budget.h"
#include "pairs.h"
#include "tables.h"

#define EXIT_HOLDS 0
#define EXIT_FAILS 1
#define EXIT_UNUSABLE 2
#define IDENTITY_CALLS 1000

static const benchmark_sampling core_budget_sampling = {200000, 1000, 2000};
static const benchmark_sampling pair_sampling = {100000, 1000, 501};
static const benchmark_sampling same_language_sampling = {2000000, 1000000, 301};
static const benchmark_sampling brief_sampling = {1000, 20, 21};

static const char *platform_text(void) {
    const char *platform = benchmark_platform();
    return platform == NULL ? "unknown" : platform;
}

static benchmark_arithmetic copied_object;

/** Resolves as the context does and hands back a copy of what it was handed: a layer that breaks the identity. */
static weivel_c_status resolve_a_copy(void *data, weivel_c_context *context, const char *id, void **service) {
    void *resolved = NULL;
    weivel_c_status status = weivel_c_context_get_service(context, id, &resolved, NULL);
    (void)data;
    if (status == WEIVEL_C_STATUS_OK) {
        copied_object = *(const benchmark_arithmetic *)resolved;
        *service = &copied_object;
    }
    return status;
}

static int judge_identity(benchmark_same_language_world *world) {
    uint32_t before = crossings;
    uint32_t call;
    if (!benchmark_resolved_the_providers_object(world)) {
        printf("weivel-c-benchmark identity: the consumer was handed %p, not the provider's own object %p\n",
               (void *)world->resolved, (void *)&world->provided);
        return EXIT_FAILS;
    }
    for (call = 0; call < IDENTITY_CALLS; call++) {
        benchmark_same_language_call(&world->resolved, call);
    }
    if (crossings != before) {
        printf("weivel-c-benchmark identity: %d calls through the resolved object crossed into the core %u times\n",
               IDENTITY_CALLS, crossings - before);
        return EXIT_FAILS;
    }
    printf("weivel-c-benchmark identity: the consumer resolved the provider's own object, and %d calls through it "
           "crossed into the core 0 times, where building the world and resolving crossed %u times\n",
           IDENTITY_CALLS, before);
    return EXIT_HOLDS;
}

static int run_identity(int copied) {
    benchmark_same_language_world world;
    int verdict;
    crossings = 0;
    if (!benchmark_same_language_world_start(&world, &counting_table, copied ? resolve_a_copy : NULL, NULL)) {
        fprintf(stderr, "the same-language world did not start\n");
        return EXIT_UNUSABLE;
    }
    if (crossings == 0) {
        fprintf(stderr, "the counting table counted no crossing at all, so it counts nothing the layer does\n");
        benchmark_same_language_world_end(&world);
        return EXIT_UNUSABLE;
    }
    verdict = judge_identity(&world);
    benchmark_same_language_world_end(&world);
    return verdict;
}

static void print_summary(const char *name, const benchmark_summary *summary) {
    printf("weivel-c-benchmark %s platform=%s median_ns=%.2f lower_quartile_ns=%.2f upper_quartile_ns=%.2f "
           "p99_ns=%.2f minimum_ns=%.2f maximum_ns=%.2f\n",
           name, platform_text(), summary->median, summary->lower_quartile, summary->upper_quartile,
           summary->percentile_99, summary->minimum, summary->maximum);
}

static int measure_the_control_call(const benchmark_sampling *sampling, benchmark_summary *summary) {
    benchmark_control_world world;
    benchmark_path path;
    benchmark_samples samples;
    int usable = 0;
    if (!benchmark_control_world_start(&world, &real_table)) {
        return 0;
    }
    path.call = benchmark_control_call;
    path.data = &world;
    if (benchmark_sample(&path, 1, sampling, &samples)) {
        usable = benchmark_summarise(samples.times[0], samples.batches, summary);
        benchmark_samples_free(&samples);
    }
    benchmark_control_world_end(&world);
    return usable;
}

static int run_control_budget(const char *budget_path, const benchmark_sampling *sampling) {
    char problem[BENCHMARK_PROBLEM];
    double budget = 0;
    benchmark_summary summary;
    if (!benchmark_read_core_budget(budget_path, &budget, problem, sizeof problem)) {
        fprintf(stderr, "%s\n", problem);
        return EXIT_UNUSABLE;
    }
    if (!measure_the_control_call(sampling, &summary)) {
        fprintf(stderr, "the control call measured nothing usable\n");
        return EXIT_UNUSABLE;
    }
    print_summary("control-call-through-the-wrapper", &summary);
    printf("weivel-c-benchmark control-call-budget median_ns=%.2f budget_ns=%.2f\n", summary.median, budget);
    if (summary.median > budget) {
        printf("weivel-c-benchmark control-call: the median %.2f ns is over the budget of %.2f ns\n", summary.median,
               budget);
        return EXIT_FAILS;
    }
    return EXIT_HOLDS;
}

static int measure_control_pair(const benchmark_sampling *sampling, benchmark_pair *pair) {
    benchmark_control_world wrapped;
    bare_world bare;
    benchmark_path reference = {bare_control_call, &bare};
    benchmark_path measured = {benchmark_control_call, &wrapped};
    int usable = 0;
    if (start_bare_world(&bare) && benchmark_control_world_start(&wrapped, &real_table)) {
        usable = benchmark_measure_pair(&reference, &measured, sampling, pair);
        benchmark_control_world_end(&wrapped);
    }
    end_bare_world(&bare);
    return usable;
}

static weivel_standard_library_function echo_function;
static weivel_standard_library_service echo_service;
static weivel_standard_library echo_declaration;

/** The core's echo test library, declared as a caller declares it: its `echo_echo` answers its input. */
static const weivel_standard_library *declare_echo(void) {
    echo_function.name = "echo";
    echo_function.symbol = "echo_echo";
    echo_function.kind = WEIVEL_STANDARD_FUNCTION_BYTES;
    echo_service.id = "echo";
    echo_service.functions = &echo_function;
    echo_service.function_count = 1;
    echo_declaration = (weivel_standard_library)WEIVEL_INIT(weivel_standard_library);
    echo_declaration.import_name = "echo";
    echo_declaration.version = "1.0.0";
    echo_declaration.release = "echo_release";
    echo_declaration.services = &echo_service;
    echo_declaration.service_count = 1;
    return &echo_declaration;
}

static int measure_library_pair(const char *library_path, const benchmark_sampling *sampling,
                                benchmark_pair *pair) {
    benchmark_library_world world;
    benchmark_path reference = {benchmark_library_call_directly, &world};
    benchmark_path measured = {benchmark_library_call_through_the_host, &world};
    int usable;
    if (!benchmark_library_world_start(&world, &real_table, library_path, declare_echo(), 0)) {
        return 0;
    }
    usable = benchmark_measure_pair(&reference, &measured, sampling, pair);
    benchmark_library_world_end(&world);
    return usable;
}

static int measure_same_language(const benchmark_sampling *sampling, benchmark_same_language *result) {
    benchmark_same_language_world world;
    int usable;
    if (!benchmark_same_language_world_start(&world, &real_table, NULL, NULL)) {
        return 0;
    }
    usable = benchmark_resolved_the_providers_object(&world) &&
             benchmark_measure_same_language(&world, sampling, result);
    benchmark_same_language_world_end(&world);
    return usable;
}

static void print_pair(const char *name, int repetition, const benchmark_pair *pair) {
    printf("weivel-c-benchmark %s platform=%s repetition=%d reference_median_ns=%.2f measured_median_ns=%.2f "
           "ratio_median=%.4f ratio_lower_quartile=%.4f ratio_upper_quartile=%.4f\n",
           name, platform_text(), repetition, pair->reference.median, pair->measured.median, pair->round_ratio.median,
           pair->round_ratio.lower_quartile, pair->round_ratio.upper_quartile);
}

static void print_same_language(int repetition, const benchmark_same_language *result) {
    printf("weivel-c-benchmark same-language platform=%s repetition=%d direct_median_ns=%.3f "
           "through_weivel_median_ns=%.3f noise_ratio=%.6f noise=%.6f ratio=%.6f deviation=%.6f\n",
           platform_text(), repetition, result->direct.median, result->through_weivel.median, result->noise_ratio,
           benchmark_deviation(result->noise_ratio), result->ratio, benchmark_deviation(result->ratio));
}

static int measure_once(const char *library_path, int repetition) {
    benchmark_pair pair;
    benchmark_same_language same_language;
    if (!measure_control_pair(&pair_sampling, &pair)) {
        return 0;
    }
    print_pair("control-call", repetition, &pair);
    if (!measure_library_pair(library_path, &pair_sampling, &pair)) {
        return 0;
    }
    print_pair("library-call", repetition, &pair);
    if (!measure_same_language(&same_language_sampling, &same_language)) {
        return 0;
    }
    print_same_language(repetition, &same_language);
    return 1;
}

static int run_measure(const char *library_path, int repetitions) {
    benchmark_summary summary;
    int repetition;
    if (repetitions <= 0 || !measure_the_control_call(&core_budget_sampling, &summary)) {
        fprintf(stderr, "the measurement read nothing\n");
        return EXIT_UNUSABLE;
    }
    print_summary("control-call-through-the-wrapper", &summary);
    for (repetition = 1; repetition <= repetitions; repetition++) {
        if (!measure_once(library_path, repetition)) {
            fprintf(stderr, "repetition %d measured nothing usable\n", repetition);
            return EXIT_UNUSABLE;
        }
    }
    return EXIT_HOLDS;
}

static int read_language_budget(const char *path, benchmark_budget *budget) {
    char problem[BENCHMARK_PROBLEM];
    if (!benchmark_read_budget(path, budget, problem, sizeof problem)) {
        fprintf(stderr, "%s\n", problem);
        return 0;
    }
    return 1;
}

static double bound_here(const benchmark_budget *budget, benchmark_gate gate, const char *name) {
    double bound = benchmark_ratio_bound(budget, gate, benchmark_platform());
    if (bound == 0) {
        printf("weivel-c-benchmark %s: the budget gives no line for %s on %s, so the gate fails rather than passing "
               "unbounded\n",
               name, name, platform_text());
    }
    return bound;
}

static int judge_ratio(const char *name, const benchmark_pair *pair, double bound) {
    print_pair(name, 1, pair);
    printf("weivel-c-benchmark %s platform=%s ratio_median=%.4f bound=%.4f\n", name, platform_text(),
           pair->round_ratio.median, bound);
    if (pair->round_ratio.median > bound) {
        printf("weivel-c-benchmark %s: the ratio %.4f is over its bound of %.4f\n", name, pair->round_ratio.median,
               bound);
        return EXIT_FAILS;
    }
    return EXIT_HOLDS;
}

static int run_control_ratio(const char *budget_path, const benchmark_sampling *sampling) {
    benchmark_budget budget;
    benchmark_pair pair;
    double bound;
    if (!read_language_budget(budget_path, &budget)) {
        return EXIT_UNUSABLE;
    }
    bound = bound_here(&budget, BENCHMARK_CONTROL_CALL, "control-call");
    if (bound == 0) {
        return EXIT_FAILS;
    }
    if (!measure_control_pair(sampling, &pair)) {
        fprintf(stderr, "the control call measured nothing usable\n");
        return EXIT_UNUSABLE;
    }
    return judge_ratio("control-call", &pair, bound);
}

static int run_library_ratio(const char *library_path, const char *budget_path, const benchmark_sampling *sampling) {
    benchmark_budget budget;
    benchmark_pair pair;
    double bound;
    if (!read_language_budget(budget_path, &budget)) {
        return EXIT_UNUSABLE;
    }
    bound = bound_here(&budget, BENCHMARK_LIBRARY_CALL, "library-call");
    if (bound == 0) {
        return EXIT_FAILS;
    }
    if (!measure_library_pair(library_path, sampling, &pair)) {
        fprintf(stderr, "the library call measured nothing usable\n");
        return EXIT_UNUSABLE;
    }
    return judge_ratio("library-call", &pair, bound);
}

static int judge_same_language(benchmark_noise_bounds noise_bounds, const benchmark_same_language *result) {
    double bound = benchmark_same_language_bound(noise_bounds, result->noise_ratio);
    print_same_language(1, result);
    printf("weivel-c-benchmark same-language platform=%s ratio=%.4f bound=%.4f noise_margin=%.4f quiet_noise=%.6f "
           "worst_noise=%.4f\n",
           platform_text(), result->ratio, bound, noise_bounds.margin, noise_bounds.quiet, noise_bounds.worst);
    if (benchmark_too_noisy_to_judge(noise_bounds, result->noise_ratio)) {
        printf("weivel-c-benchmark same-language: the run was too noisy to judge, its two identical direct paths "
               "differing by %.4f\n",
               benchmark_deviation(result->noise_ratio));
        return EXIT_FAILS;
    }
    if (!benchmark_within(result->ratio, bound)) {
        printf("weivel-c-benchmark same-language: the ratio %.4f is outside its bound of %.4f\n", result->ratio,
               bound);
        return EXIT_FAILS;
    }
    return EXIT_HOLDS;
}

/** Reads this platform's noise bounds; answers EXIT_HOLDS when it has them, else the gate's verdict. */
static int noise_bounds_here(const char *budget_path, benchmark_noise_bounds *noise_bounds) {
    benchmark_budget budget;
    if (!read_language_budget(budget_path, &budget)) {
        return EXIT_UNUSABLE;
    }
    *noise_bounds = benchmark_noise_bounds_of(&budget, benchmark_platform());
    if (!benchmark_noise_bounded(*noise_bounds)) {
        printf("weivel-c-benchmark same-language: the budget gives no line for quiet-noise, worst-noise or noise-margin on %s, so "
               "the gate fails rather than passing unbounded\n",
               platform_text());
        return EXIT_FAILS;
    }
    return EXIT_HOLDS;
}

static int run_same_language(const char *budget_path, const benchmark_sampling *sampling) {
    benchmark_same_language result;
    benchmark_noise_bounds noise_bounds;
    int bounded = noise_bounds_here(budget_path, &noise_bounds);
    if (bounded != EXIT_HOLDS) {
        return bounded;
    }
    if (!measure_same_language(sampling, &result)) {
        fprintf(stderr, "the same-language call measured nothing usable\n");
        return EXIT_UNUSABLE;
    }
    return judge_same_language(noise_bounds, &result);
}

static int judge_given_ratios(const char *budget_path, const char *ratio, const char *noise_ratio) {
    benchmark_same_language result;
    benchmark_noise_bounds noise_bounds;
    int bounded = noise_bounds_here(budget_path, &noise_bounds);
    if (bounded != EXIT_HOLDS) {
        return bounded;
    }
    memset(&result, 0, sizeof result);
    result.ratio = atof(ratio);
    result.noise_ratio = atof(noise_ratio);
    return judge_same_language(noise_bounds, &result);
}

static const benchmark_sampling *sampling_of(int argument_count, char **arguments, int position,
                                             const benchmark_sampling *full) {
    return argument_count > position && strcmp(arguments[position], "brief") == 0 ? &brief_sampling : full;
}

static int run_mode(const char *mode, int argument_count, char **arguments) {
    if (strcmp(mode, "identity") == 0 || strcmp(mode, "identity-copied") == 0) {
        return run_identity(strcmp(mode, "identity-copied") == 0);
    }
    if (strcmp(mode, "control-budget") == 0 && argument_count >= 4) {
        return run_control_budget(arguments[3], sampling_of(argument_count, arguments, 4, &core_budget_sampling));
    }
    if (strcmp(mode, "control-ratio") == 0 && argument_count >= 4) {
        return run_control_ratio(arguments[3], sampling_of(argument_count, arguments, 4, &pair_sampling));
    }
    if (strcmp(mode, "library-ratio") == 0 && argument_count >= 5) {
        return run_library_ratio(arguments[3], arguments[4], sampling_of(argument_count, arguments, 5, &pair_sampling));
    }
    if (strcmp(mode, "same-language") == 0 && argument_count >= 4) {
        return run_same_language(arguments[3], sampling_of(argument_count, arguments, 4, &same_language_sampling));
    }
    if (strcmp(mode, "same-language-judge") == 0 && argument_count >= 6) {
        return judge_given_ratios(arguments[3], arguments[4], arguments[5]);
    }
    if (strcmp(mode, "measure") == 0 && argument_count >= 5) {
        return run_measure(arguments[3], atoi(arguments[4]));
    }
    fprintf(stderr, "usage: benchmarks_benchmark <mode> <core-library> [arguments], as the file's comment lists\n");
    return EXIT_UNUSABLE;
}

int main(int argument_count, char **arguments) {
    int verdict;
    if (argument_count < 3 || !load_tables(arguments[2])) {
        fprintf(stderr, "usage: benchmarks_benchmark <mode> <core-library> [arguments]\n");
        return EXIT_UNUSABLE;
    }
    verdict = run_mode(arguments[1], argument_count, arguments);
    weivel_c_core_unload(&real_table);
    return verdict;
}
