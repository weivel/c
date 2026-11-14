#include "worlds.h"

#include <string.h>

#include "../core/binary.h"

static int resolve_the_service(benchmark_library_world *world, const char *id) {
    weivel_c_context *context = NULL;
    void *service = NULL;
    if (weivel_c_host_context(world->host, &context) != WEIVEL_C_STATUS_OK ||
        weivel_c_context_get_service(context, id, &service, NULL) != WEIVEL_C_STATUS_OK) {
        return 0;
    }
    world->service = service;
    return world->function < world->service->function_count;
}

static int bind_directly(benchmark_library_world *world, const char *path, const weivel_standard_library *declaration) {
    const weivel_standard_library_service *service = &declaration->services[0];
    if (world->function >= service->function_count ||
        weivel_c_binary_open(path, &world->binary) != WEIVEL_C_STATUS_OK) {
        return 0;
    }
    world->direct = (benchmark_library_function)weivel_c_binary_find(world->binary,
                                                                      service->functions[world->function].symbol);
    world->release = (benchmark_library_release)weivel_c_binary_find(world->binary, declaration->release);
    return world->direct != NULL && world->release != NULL;
}

static int load_the_library(benchmark_library_world *world, const char *path,
                            const weivel_standard_library *declaration) {
    weivel_c_library *library = NULL;
    return declaration->service_count > 0 &&
           weivel_c_host_load_library(world->host, path, declaration, &library, NULL, NULL) == WEIVEL_C_STATUS_OK &&
           resolve_the_service(world, declaration->services[0].id) && bind_directly(world, path, declaration);
}

int benchmark_library_world_start(benchmark_library_world *world, const weivel_c_core_table *table, const char *path,
                                  const weivel_standard_library *declaration, uint32_t function) {
    weivel_c_host_options options = WEIVEL_INIT(weivel_c_host_options);
    memset(world, 0, sizeof *world);
    memset(world->input, 7, sizeof world->input);
    world->function = function;
    options.table = table;
    if (weivel_c_host_create(&options, &world->host, NULL) != WEIVEL_C_STATUS_OK) {
        return 0;
    }
    if (!load_the_library(world, path, declaration)) {
        benchmark_library_world_end(world);
        return 0;
    }
    return 1;
}

int benchmark_library_call_through_the_host(void *data, uint32_t call) {
    benchmark_library_world *world = data;
    weivel_c_library_answer answer = WEIVEL_INIT(weivel_c_library_answer);
    int answered;
    (void)call;
    answer.body = (weivel_c_bytes)WEIVEL_INIT(weivel_c_bytes);
    answered = world->service->call(world->service, world->function, world->input, sizeof world->input, &answer) ==
                   WEIVEL_C_STATUS_OK &&
               answer.body.length == sizeof world->input;
    return world->service->release_bytes(&answer.body) == WEIVEL_C_STATUS_OK && answered;
}

int benchmark_library_call_directly(void *data, uint32_t call) {
    benchmark_library_world *world = data;
    uint8_t *output = NULL;
    size_t output_length = 0;
    int answered;
    (void)call;
    answered = world->direct(world->input, sizeof world->input, &output, &output_length) == 0 &&
               output_length == sizeof world->input;
    world->release(output);
    return answered;
}

void benchmark_library_world_end(benchmark_library_world *world) {
    if (world->host != NULL) {
        weivel_c_host_destroy(world->host, NULL);
        weivel_c_host_free(world->host);
        world->host = NULL;
    }
    if (world->binary != NULL) {
        weivel_c_binary_close(world->binary);
        world->binary = NULL;
    }
}
