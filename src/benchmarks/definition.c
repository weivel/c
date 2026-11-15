#include "worlds.h"

#include <string.h>

#include "../core/binary.h"
#include "echo.h"

static int resolve_the_definition(benchmark_definition_world *world) {
    weivel_c_context *context = NULL;
    void *service = NULL;
    if (weivel_c_host_context(world->host, &context) != WEIVEL_C_STATUS_OK ||
        weivel_c_context_get_service(context, benchmark_definition_service, &service, NULL) != WEIVEL_C_STATUS_OK) {
        return 0;
    }
    world->resolved = service;
    return 1;
}

static int answer_the_symbol_directly(benchmark_definition_world *world, const char *path, const char *symbol) {
    weivel_c_binary_function found;
    if (weivel_c_binary_open(path, &world->binary) != WEIVEL_C_STATUS_OK) {
        return 0;
    }
    found = weivel_c_binary_find(world->binary, symbol);
    if (found == NULL) {
        return 0;
    }
    world->direct = ((const void *(*)(void))found)();
    world->direct_again = world->direct;
    return world->direct != NULL;
}

int benchmark_definition_world_start(benchmark_definition_world *world, const weivel_c_core_table *table,
                                     const char *path, const weivel_standard_library *declaration) {
    weivel_c_host_options options = WEIVEL_INIT(weivel_c_host_options);
    weivel_c_library *library = NULL;
    memset(world, 0, sizeof *world);
    options.table = table;
    if (weivel_c_host_create(&options, &world->host, NULL) != WEIVEL_C_STATUS_OK) {
        return 0;
    }
    if (declaration->service_count == 0 ||
        weivel_c_host_load_library(world->host, path, declaration, &library, NULL, NULL) != WEIVEL_C_STATUS_OK ||
        !resolve_the_definition(world) ||
        !answer_the_symbol_directly(world, path, declaration->services[0].functions[0].symbol)) {
        benchmark_definition_world_end(world);
        return 0;
    }
    return 1;
}

int benchmark_resolved_the_definitions_own_pointer(const benchmark_definition_world *world) {
    return world->resolved == world->direct;
}

int benchmark_definition_call(void *data, uint32_t call) {
    const echo_definition *const *definition = data;
    (void)call;
    (*definition)->count_unloads((*definition)->context, NULL);
    return 1;
}

void benchmark_definition_world_end(benchmark_definition_world *world) {
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
