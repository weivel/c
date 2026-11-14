#ifndef WEIVEL_C_BENCHMARKS_WORLDS_H
#define WEIVEL_C_BENCHMARKS_WORLDS_H

/**
 * @file
 * What the `benchmarks` module measures, each built through the layer's public surface on a table the caller loaded,
 * so a caller that counts the table's crossings counts the layer's:
 *
 * - the control call through the wrapper: the core's weivel_get_service for a plugin, the very call the kit's
 *   `control_call.c` times, made as weivel_c_core_get_service;
 * - a C library's function called through the object the host registers for it, and the same function called
 *   directly through the platform's loader, each releasing what it answered;
 * - a same-language call: a consumer plugin resolves another plugin's service through its context, and calls the
 *   resolved object, beside the same call made on the provider's own object.
 */

#include <stdint.h>

#include <weivel/c.h>

#include "measure.h"

/** A core behind the wrapper with one plugin opened that consumes a service the host registered. */
typedef struct benchmark_control_world {
    weivel_c_core *core;
    weivel_handle plugin;
} benchmark_control_world;

/** The id the control call gets, which the plugin consumes and the host registers. */
extern const char *const benchmark_control_service;

/** Builds the world on `table`. Answers 1, or 0 with nothing held. */
int benchmark_control_world_start(benchmark_control_world *world, const weivel_c_core_table *table);

/** The control call through the wrapper; `data` is the world. */
int benchmark_control_call(void *data, uint32_t call);

void benchmark_control_world_end(benchmark_control_world *world);

/** The function of a C library that the library call measures, and how it releases what it answers. */
typedef int32_t (*benchmark_library_function)(const uint8_t *input, size_t input_length, uint8_t **output,
                                              size_t *output_length);
typedef void (*benchmark_library_release)(uint8_t *buffer);

#define BENCHMARK_LIBRARY_INPUT 16

/**
 * A host that loaded a C library as host services, the object of its first service as the host's own context
 * resolves it, and the same library opened again through the platform's loader for the direct call.
 */
typedef struct benchmark_library_world {
    weivel_c_host *host;
    const weivel_c_library_service *service;
    uint32_t function;
    void *binary;
    benchmark_library_function direct;
    benchmark_library_release release;
    uint8_t input[BENCHMARK_LIBRARY_INPUT];
} benchmark_library_world;

/**
 * Builds the world on `table`: loads the library at `path` as `declaration` declares it and resolves its first
 * service, whose function `function` both paths call. Answers 1, or 0 with nothing held.
 */
int benchmark_library_world_start(benchmark_library_world *world, const weivel_c_core_table *table, const char *path,
                                  const weivel_standard_library *declaration, uint32_t function);

/** The call through the service object the host registered, its answer released; `data` is the world. */
int benchmark_library_call_through_the_host(void *data, uint32_t call);

/** The same function called directly, its answer released by the library's release; `data` is the world. */
int benchmark_library_call_directly(void *data, uint32_t call);

void benchmark_library_world_end(benchmark_library_world *world);

/** The object the provider registers: one function, the call the same-language paths time. */
typedef struct benchmark_arithmetic {
    int64_t (*add)(int64_t left, int64_t right);
} benchmark_arithmetic;

/**
 * How the consumer resolves the provider's service: weivel_c_context_get_service unless a caller replaces it, which is
 * how a test shows the identity gate failing for a layer that hands out anything but the provider's object.
 */
typedef weivel_c_status (*benchmark_resolve)(void *data, weivel_c_context *context, const char *id, void **service);

typedef struct benchmark_same_language_world {
    weivel_c_host *host;
    benchmark_arithmetic provided;
    benchmark_arithmetic *resolved;
    weivel_c_context *provider;
    weivel_c_context *consumer;
    weivel_handle plugins[2];
    uint32_t active;
} benchmark_same_language_world;

/**
 * Builds the world on `table`: registers and activates a provider and a consumer, and has the consumer resolve the
 * provider's service through `resolve`, NULL for its context's get. Answers 1, or 0 with nothing held.
 */
int benchmark_same_language_world_start(benchmark_same_language_world *world, const weivel_c_core_table *table,
                                        benchmark_resolve resolve, void *resolve_data);

/** 1 when what the consumer resolved is the provider's own object, the very pointer it registered. */
int benchmark_resolved_the_providers_object(const benchmark_same_language_world *world);

/** The call on the provider's own object; `data` is the world. */
int benchmark_same_language_call_directly(void *data, uint32_t call);

/** The call on the object the consumer resolved; `data` is the world. */
int benchmark_same_language_call_through_weivel(void *data, uint32_t call);

void benchmark_same_language_world_end(benchmark_same_language_world *world);

#endif
