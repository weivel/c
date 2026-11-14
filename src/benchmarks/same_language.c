#include "worlds.h"

#include <string.h>

#define PROVIDER 0
#define CONSUMER 1

static const char *const arithmetic_id = "provider:arithmetic";

static int64_t add(int64_t left, int64_t right) {
    return left + right;
}

static weivel_c_status get_through_the_context(void *data, weivel_c_context *context, const char *id,
                                               void **service) {
    (void)data;
    return weivel_c_context_get_service(context, id, service, NULL);
}

static uint32_t activate_the_provider(void *data, weivel_c_context *context, weivel_c_completion *completion,
                                      const char **reason) {
    benchmark_same_language_world *world = data;
    (void)completion;
    (void)reason;
    world->provider = context;
    return weivel_c_context_register_service(context, arithmetic_id, &world->provided, NULL, NULL, NULL) ==
                   WEIVEL_C_STATUS_OK
               ? WEIVEL_C_ENTRY_DONE
               : WEIVEL_C_ENTRY_FAILED;
}

static uint32_t activate_the_consumer(void *data, weivel_c_context *context, weivel_c_completion *completion,
                                      const char **reason) {
    benchmark_same_language_world *world = data;
    (void)completion;
    (void)reason;
    world->consumer = context;
    return WEIVEL_C_ENTRY_DONE;
}

static weivel_c_status count_the_active(void *data, const weivel_c_outcome *outcome) {
    benchmark_same_language_world *world = data;
    world->active += outcome->kind == WEIVEL_C_OUTCOME_ACTIVE;
    return WEIVEL_C_STATUS_OK;
}

static int register_plugin(benchmark_same_language_world *world, weivel_c_entry_function activate,
                           const weivel_manifest *manifest, weivel_handle *plugin) {
    weivel_c_entry entry = WEIVEL_INIT(weivel_c_entry);
    entry.activate = activate;
    entry.data = world;
    return weivel_c_host_register(world->host, manifest, &entry, plugin, NULL) == WEIVEL_C_STATUS_OK;
}

static int register_both(benchmark_same_language_world *world) {
    weivel_service_offer offer = WEIVEL_INIT(weivel_service_offer);
    weivel_service_want want = WEIVEL_INIT(weivel_service_want);
    weivel_manifest provider = WEIVEL_INIT(weivel_manifest);
    weivel_manifest consumer = WEIVEL_INIT(weivel_manifest);
    offer.id = arithmetic_id;
    want.id = arithmetic_id;
    provider.import_name = "provider";
    provider.api = WEIVEL_API_VERSION;
    provider.services.provides.items = &offer;
    provider.services.provides.count = 1;
    consumer.import_name = "consumer";
    consumer.api = WEIVEL_API_VERSION;
    consumer.services.consumes.items = &want;
    consumer.services.consumes.count = 1;
    return register_plugin(world, activate_the_provider, &provider, &world->plugins[PROVIDER]) &&
           register_plugin(world, activate_the_consumer, &consumer, &world->plugins[CONSUMER]);
}

static int activate_both(benchmark_same_language_world *world) {
    weivel_c_activation activation = WEIVEL_INIT(weivel_c_activation);
    weivel_activation_plan plan = WEIVEL_INIT(weivel_activation_plan);
    activation.plugins = world->plugins;
    activation.count = 2;
    activation.edge_source = WEIVEL_EDGES_CAPABILITIES;
    activation.listener.hear = count_the_active;
    activation.listener.data = world;
    if (weivel_c_host_activate(world->host, &activation, &plan, NULL) != WEIVEL_C_STATUS_OK) {
        return 0;
    }
    weivel_c_host_plan_release(world->host, &plan);
    return world->active == 2 && world->consumer != NULL;
}

int benchmark_same_language_world_start(benchmark_same_language_world *world, const weivel_c_core_table *table,
                                        benchmark_resolve resolve, void *resolve_data) {
    weivel_c_host_options options = WEIVEL_INIT(weivel_c_host_options);
    void *resolved = NULL;
    memset(world, 0, sizeof *world);
    world->provided.add = add;
    options.table = table;
    if (weivel_c_host_create(&options, &world->host, NULL) != WEIVEL_C_STATUS_OK) {
        return 0;
    }
    resolve = resolve == NULL ? get_through_the_context : resolve;
    if (!register_both(world) || !activate_both(world) ||
        resolve(resolve_data, world->consumer, arithmetic_id, &resolved) != WEIVEL_C_STATUS_OK || resolved == NULL) {
        benchmark_same_language_world_end(world);
        return 0;
    }
    world->resolved = resolved;
    return 1;
}

int benchmark_resolved_the_providers_object(const benchmark_same_language_world *world) {
    return world->resolved == &world->provided;
}

/** Where every answer is kept, so an optimiser cannot drop a call whose answer nobody reads. */
static volatile int64_t answers;

int benchmark_same_language_call_directly(void *data, uint32_t call) {
    benchmark_same_language_world *world = data;
    answers = world->provided.add((int64_t)call, 1);
    return 1;
}

int benchmark_same_language_call_through_weivel(void *data, uint32_t call) {
    benchmark_same_language_world *world = data;
    answers = world->resolved->add((int64_t)call, 1);
    return 1;
}

void benchmark_same_language_world_end(benchmark_same_language_world *world) {
    if (world->host != NULL) {
        weivel_c_host_destroy(world->host, NULL);
        weivel_c_host_free(world->host);
        world->host = NULL;
    }
}
