#define _CRT_SECURE_NO_WARNINGS

#include "budget.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUDGET_LINE 256
#define BUDGET_FIELD 64
#define BUDGET_SCALARS 1
#define BUDGET_PLATFORM_KEYS 5

static const char *const platform_key_names[BUDGET_PLATFORM_KEYS] = {"control-call", "library-call", "quiet-noise",
                                                                     "worst-noise", "noise-margin"};
static const char *const platform_names[BENCHMARK_PLATFORMS] = {"linux", "windows", "macos"};
static const char *const scalar_names[BUDGET_SCALARS] = {"ratio-margin"};

/** A line's fields, split at single spaces as the format separates them. */
typedef struct budget_fields {
    char field[4][BUDGET_FIELD];
    int count;
} budget_fields;

static int index_of(const char *const *names, int count, const char *name) {
    int at;
    for (at = 0; at < count; at++) {
        if (strcmp(names[at], name) == 0) {
            return at;
        }
    }
    return -1;
}

static int read_positive(const char *text, double *number) {
    char *end = NULL;
    *number = strtod(text, &end);
    return end != text && *end == '\0' && isfinite(*number) && *number > 0;
}

static void split_fields(char *line, budget_fields *fields) {
    line[strcspn(line, "\r\n")] = '\0';
    fields->count = sscanf(line, "%63s %63s %63s %63s", fields->field[0], fields->field[1], fields->field[2],
                           fields->field[3]);
}

static double *scalar_of(benchmark_budget *budget, int scalar) {
    double *scalars[BUDGET_SCALARS] = {&budget->ratio_margin};
    return scalars[scalar];
}

static double *platform_row_of(benchmark_budget *budget, int key) {
    double *rows[BUDGET_PLATFORM_KEYS] = {budget->highest[BENCHMARK_CONTROL_CALL],
                                          budget->highest[BENCHMARK_LIBRARY_CALL], budget->quiet_noise,
                                          budget->worst_noise, budget->noise_margin};
    return rows[key];
}

/** Where a line's number goes, or NULL for a line the format does not define. */
static double *place_of(benchmark_budget *budget, const budget_fields *fields) {
    int key = index_of(platform_key_names, BUDGET_PLATFORM_KEYS, fields->field[0]);
    int platform = fields->count == 3 ? index_of(platform_names, BENCHMARK_PLATFORMS, fields->field[1]) : -1;
    int scalar = fields->count == 2 ? index_of(scalar_names, BUDGET_SCALARS, fields->field[0]) : -1;
    if (key >= 0 && platform >= 0) {
        return &platform_row_of(budget, key)[platform];
    }
    return scalar >= 0 ? scalar_of(budget, scalar) : NULL;
}

static int read_line(char *line, benchmark_budget *budget, char *problem, size_t problem_size) {
    budget_fields fields;
    double *place;
    double number;
    split_fields(line, &fields);
    if (fields.count <= 0) {
        return 1;
    }
    place = place_of(budget, &fields);
    if (place == NULL) {
        snprintf(problem, problem_size, "'%s' is neither '<gate or noise> <platform> <number>' nor '<margin> <number>'", line);
        return 0;
    }
    if (!read_positive(fields.field[fields.count - 1], &number)) {
        snprintf(problem, problem_size, "'%s' carries no positive number", line);
        return 0;
    }
    if (*place != 0) {
        snprintf(problem, problem_size, "'%s' is given twice", line);
        return 0;
    }
    *place = number;
    return 1;
}

static int holds_every_scalar(benchmark_budget *budget, char *problem, size_t problem_size) {
    int scalar;
    for (scalar = 0; scalar < BUDGET_SCALARS; scalar++) {
        if (*scalar_of(budget, scalar) == 0) {
            snprintf(problem, problem_size, "'%s' is missing", scalar_names[scalar]);
            return 0;
        }
    }
    return 1;
}

int benchmark_read_budget(const char *path, benchmark_budget *budget, char *problem, size_t problem_size) {
    char line[BUDGET_LINE];
    int usable = 1;
    FILE *file = fopen(path, "r");
    memset(budget, 0, sizeof *budget);
    if (file == NULL) {
        snprintf(problem, problem_size, "the budget file %s cannot be opened", path);
        return 0;
    }
    while (usable && fgets(line, sizeof line, file) != NULL) {
        usable = read_line(line, budget, problem, problem_size);
    }
    fclose(file);
    return usable && holds_every_scalar(budget, problem, problem_size);
}

int benchmark_read_core_budget(const char *path, double *median_nanoseconds, char *problem, size_t problem_size) {
    char key[BUDGET_FIELD];
    char value[BUDGET_FIELD];
    char leftover[BUDGET_FIELD];
    int fields;
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        snprintf(problem, problem_size, "the budget file %s cannot be opened", path);
        return 0;
    }
    fields = fscanf(file, "%63s %63s %63s", key, value, leftover);
    fclose(file);
    if (fields != 2 || strcmp(key, "median_nanoseconds") != 0 || !read_positive(value, median_nanoseconds)) {
        snprintf(problem, problem_size, "the budget file %s must hold exactly: median_nanoseconds <positive number>",
                 path);
        return 0;
    }
    return 1;
}

const char *benchmark_platform(void) {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return NULL;
#endif
}

static int platform_index(const char *platform) {
    return platform == NULL ? -1 : index_of(platform_names, BENCHMARK_PLATFORMS, platform);
}

double benchmark_ratio_bound(const benchmark_budget *budget, benchmark_gate gate, const char *platform) {
    int at = platform_index(platform);
    return at < 0 ? 0 : budget->highest[gate][at] * budget->ratio_margin;
}

benchmark_noise_bounds benchmark_noise_bounds_of(const benchmark_budget *budget, const char *platform) {
    benchmark_noise_bounds bounds = {0, 0, 0};
    int at = platform_index(platform);
    if (at >= 0) {
        bounds.quiet = budget->quiet_noise[at];
        bounds.worst = budget->worst_noise[at];
        bounds.margin = budget->noise_margin[at];
    }
    return bounds;
}

int benchmark_noise_bounded(benchmark_noise_bounds bounds) {
    return bounds.quiet > 0 && bounds.worst > 0 && bounds.margin > 0;
}

double benchmark_deviation(double ratio) {
    return (ratio > 1 / ratio ? ratio : 1 / ratio) - 1;
}

int benchmark_too_noisy_to_judge(benchmark_noise_bounds bounds, double noise_ratio) {
    return benchmark_deviation(noise_ratio) > bounds.worst;
}

double benchmark_same_language_bound(benchmark_noise_bounds bounds, double noise_ratio) {
    double noise = benchmark_deviation(noise_ratio);
    return 1 + bounds.margin * (noise > bounds.quiet ? noise : bounds.quiet);
}

int benchmark_within(double ratio, double bound) {
    return ratio <= bound && ratio >= 1 / bound;
}
