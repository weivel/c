#include "worlds.h"

#include <string.h>

const char *const benchmark_control_service = "bench:service";

static int set_up(benchmark_control_world *world) {
    weivel_service_want want = WEIVEL_INIT(weivel_service_want);
    weivel_manifest manifest = WEIVEL_INIT(weivel_manifest);
    weivel_handle registration = 0;
    want.id = benchmark_control_service;
    manifest.import_name = "bench";
    manifest.api = WEIVEL_API_VERSION;
    manifest.services.consumes.items = &want;
    manifest.services.consumes.count = 1;
    return weivel_c_core_register_manifest(world->core, &manifest, &world->plugin, NULL) == WEIVEL_C_STATUS_OK &&
           weivel_c_core_open_plugin(world->core, world->plugin, NULL, NULL) == WEIVEL_C_STATUS_OK &&
           weivel_c_core_register_service(world->core, WEIVEL_HOST, benchmark_control_service, &registration,
                                          NULL) == WEIVEL_C_STATUS_OK;
}

int benchmark_control_world_start(benchmark_control_world *world, const weivel_c_core_table *table) {
    weivel_c_core_options options = WEIVEL_INIT(weivel_c_core_options);
    memset(world, 0, sizeof *world);
    options.table = table;
    if (weivel_c_core_create(&options, &world->core) != WEIVEL_C_STATUS_OK) {
        return 0;
    }
    if (!set_up(world)) {
        benchmark_control_world_end(world);
        return 0;
    }
    return 1;
}

int benchmark_control_call(void *data, uint32_t call) {
    benchmark_control_world *world = data;
    weivel_handle implementation = 0;
    (void)call;
    return weivel_c_core_get_service(world->core, world->plugin, benchmark_control_service, &implementation, NULL) ==
               WEIVEL_C_STATUS_OK &&
           implementation != 0;
}

void benchmark_control_world_end(benchmark_control_world *world) {
    if (world->core != NULL) {
        weivel_c_core_destroy(world->core);
        weivel_c_core_free(world->core);
        world->core = NULL;
    }
}
