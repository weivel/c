#include "host.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "../core/load.h"

#define END_OF(type, field) (offsetof(type, field) + sizeof(((type *)0)->field))
#define FIRST_OPTIONS_SIZE END_OF(weivel_c_host_options, log)

static weivel_c_status begin_destroying(void *data, weivel_c_core *core);
static weivel_c_status end_destroying(void *data, weivel_c_core *core);

static int options_are_usable(const weivel_c_host_options *options) {
    return options != NULL && options->size >= FIRST_OPTIONS_SIZE &&
           (options->table != NULL || options->core_path != NULL);
}

static weivel_c_status adopt_the_table(weivel_c_host *made, const weivel_c_host_options *options,
                                       weivel_c_core_load_failure *failure) {
    const weivel_c_core_table *given = options->table;
    weivel_c_status status;
    made->table = (weivel_c_core_table)WEIVEL_INIT(weivel_c_core_table);
    if (given == NULL) {
        status = weivel_c_core_load(options->core_path, &made->table, failure);
        made->loaded_the_table = status == WEIVEL_C_STATUS_OK;
        return status;
    }
    if (!weivel_c_core_table_is_large_enough(given) || given->library == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    memcpy(&made->table, given, given->size < sizeof made->table ? given->size : sizeof made->table);
    made->table.size = (uint32_t)sizeof made->table;
    return WEIVEL_C_STATUS_OK;
}

static weivel_c_status make_the_core(weivel_c_host *made, const weivel_c_host_options *options) {
    weivel_c_core_options core_options = WEIVEL_INIT(weivel_c_core_options);
    weivel_c_status status;
    core_options.table = &made->table;
    core_options.core_options = options->core_options;
    core_options.error_sink = options->error_sink;
    status = weivel_c_core_create(&core_options, &made->core);
    if (status == WEIVEL_C_STATUS_OK) {
        status = weivel_c_handles_create(&made->handles);
    }
    if (status == WEIVEL_C_STATUS_OK) {
        status = weivel_c_timer_create(made->core, options->scheduler, &made->timer);
    }
    return status;
}

static weivel_c_status make_the_contexts(weivel_c_host *made, const weivel_c_host_options *options) {
    weivel_c_contexts_options contexts_options = WEIVEL_INIT(weivel_c_contexts_options);
    weivel_c_status status;
    contexts_options.core = made->core;
    contexts_options.handles = made->handles;
    contexts_options.timer = made->timer;
    contexts_options.log = options->log;
    status = weivel_c_contexts_create(&contexts_options, &made->contexts);
    if (status == WEIVEL_C_STATUS_OK) {
        status = weivel_c_context_create(made->contexts, WEIVEL_HOST, NULL, &made->own_context);
    }
    return status;
}

static void *allocate_plainly(void *data, size_t size) {
    (void)data;
    return malloc(size);
}

static void release_plainly(void *data, void *memory) {
    (void)data;
    free(memory);
}

void weivel_c_host_replace_allocator(weivel_c_host *host, const weivel_c_allocator *allocator) {
    weivel_c_mutex_lock(&host->lock);
    host->allocator = *allocator;
    weivel_c_mutex_unlock(&host->lock);
}

static void ready_the_destroy(weivel_c_host *made) {
    made->destroy_beginning = (weivel_c_step)WEIVEL_INIT(weivel_c_step);
    made->destroy_beginning.run = begin_destroying;
    made->destroy_beginning.data = made;
    made->destroy_ending = (weivel_c_step)WEIVEL_INIT(weivel_c_step);
    made->destroy_ending.run = end_destroying;
    made->destroy_ending.data = made;
}

/** Frees what a host has made of its parts, its core destroyed first; what was never made is NULL and skipped. */
static void free_the_parts(weivel_c_host *host) {
    if (host->timer != NULL) {
        weivel_c_timer_destroy(host->timer);
    }
    if (host->core != NULL) {
        weivel_c_core_destroy(host->core);
    }
    weivel_c_contexts_free(host->contexts);
    weivel_c_timer_free(host->timer);
    weivel_c_core_free(host->core);
    weivel_c_handles_free(host->handles);
    if (host->loaded_the_table) {
        weivel_c_core_unload(&host->table);
    }
    weivel_c_mutex_end(&host->lock);
    free(host);
}

weivel_c_status weivel_c_host_create(const weivel_c_host_options *options, weivel_c_host **host,
                                     weivel_c_core_load_failure *failure) {
    weivel_c_host *made;
    weivel_c_status status;
    if (host == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    *host = NULL;
    if (!options_are_usable(options)) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    made = calloc(1, sizeof *made);
    if (made == NULL || !weivel_c_mutex_start(&made->lock)) {
        free(made);
        return WEIVEL_C_STATUS_OUT_OF_MEMORY;
    }
    ready_the_destroy(made);
    made->allocator = (weivel_c_allocator){allocate_plainly, release_plainly, NULL};
    status = adopt_the_table(made, options, failure);
    status = status == WEIVEL_C_STATUS_OK ? make_the_core(made, options) : status;
    status = status == WEIVEL_C_STATUS_OK ? make_the_contexts(made, options) : status;
    if (status != WEIVEL_C_STATUS_OK) {
        free_the_parts(made);
        return status;
    }
    *host = made;
    return WEIVEL_C_STATUS_OK;
}

int weivel_c_host_destroy_may_end(weivel_c_host *host) {
    if (!host->destroy_waits || host->releases_in_flight != 0 || host->opens_in_flight != 0 ||
        host->settles_in_flight != 0 || host->activations_in_flight != 0) {
        return 0;
    }
    host->destroy_waits = 0;
    return 1;
}

void weivel_c_host_end_the_destroy(weivel_c_host *host) {
    if (weivel_c_core_defer(host->core, &host->destroy_ending, NULL) != WEIVEL_C_STATUS_OK) {
        end_destroying(host, host->core);
    }
}

static host_plugin *last_opened(weivel_c_host *host) {
    host_plugin *last;
    weivel_c_mutex_lock(&host->lock);
    last = host->last_opened;
    weivel_c_mutex_unlock(&host->lock);
    return last;
}

/** The destroy's first step: releases every plugin opened and not released, the last opened first. */
static weivel_c_status begin_destroying(void *data, weivel_c_core *core) {
    weivel_c_host *host = data;
    host_plugin *plugin;
    int may_end;
    (void)core;
    while ((plugin = last_opened(host)) != NULL) {
        weivel_c_host_begin_release(plugin, NULL);
    }
    weivel_c_mutex_lock(&host->lock);
    host->destroy_waits = 1;
    may_end = weivel_c_host_destroy_may_end(host);
    weivel_c_mutex_unlock(&host->lock);
    if (may_end) {
        weivel_c_host_end_the_destroy(host);
    }
    return WEIVEL_C_STATUS_OK;
}

/**
 * The destroy's last step, behind every event and step its releases left: unloads every C library loaded as host
 * services, then disarms the timer and destroys the core. The plugins' own libraries stay open until the free.
 */
static weivel_c_status end_destroying(void *data, weivel_c_core *core) {
    weivel_c_host *host = data;
    (void)core;
    weivel_c_host_unload_every_library(host);
    weivel_c_timer_destroy(host->timer);
    weivel_c_core_destroy(host->core);
    weivel_c_mutex_lock(&host->lock);
    host->state = HOST_DESTROYED;
    host->destroy_finished = 1;
    weivel_c_mutex_unlock(&host->lock);
    return WEIVEL_C_STATUS_OK;
}

weivel_c_status weivel_c_host_destroy(weivel_c_host *host, uint32_t *finished) {
    int first;
    if (finished != NULL) {
        *finished = 0;
    }
    if (host == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    weivel_c_mutex_lock(&host->lock);
    first = host->state == HOST_RUNNING;
    if (first) {
        host->state = HOST_DESTROYING;
    }
    weivel_c_mutex_unlock(&host->lock);
    if (first && weivel_c_core_defer(host->core, &host->destroy_beginning, NULL) != WEIVEL_C_STATUS_OK) {
        begin_destroying(host, host->core);
    }
    if (finished != NULL) {
        weivel_c_mutex_lock(&host->lock);
        *finished = host->destroy_finished;
        weivel_c_mutex_unlock(&host->lock);
    }
    return WEIVEL_C_STATUS_OK;
}

static void free_the_records(weivel_c_host *host) {
    while (host->first_plugin != NULL) {
        host_plugin *plugin = host->first_plugin;
        host->first_plugin = plugin->next;
        weivel_c_host_close_library(plugin);
        free(plugin->manifest);
        free(plugin);
    }
    while (host->completions != NULL) {
        weivel_c_completion *completion = host->completions;
        host->completions = completion->next;
        weivel_c_refusal_release(&completion->outcome.refusal);
        host->allocator.release(host->allocator.data, completion);
    }
    while (host->services != NULL) {
        host_service *kept = host->services;
        host->services = kept->next;
        free(kept);
    }
}

/**
 * 1 when the destroy has finished and every part the free frees reports idle: no bytes of a library held, and no
 * thread still dispatching the core or telling its `follow` hook, the thread that finished the destroy among them.
 *
 * @implNote Each part is asked before anything is freed, so a refusal frees nothing. The core is asked outside the
 * host's lock, which is a leaf.
 */
static int is_idle(weivel_c_host *host) {
    int finished;
    weivel_c_mutex_lock(&host->lock);
    finished = host->destroy_finished != 0 && !weivel_c_host_libraries_are_held(host);
    weivel_c_mutex_unlock(&host->lock);
    return finished && weivel_c_core_is_idle(host->core);
}

weivel_c_status weivel_c_host_free(weivel_c_host *host) {
    if (host == NULL) {
        return WEIVEL_C_STATUS_OK;
    }
    if (!is_idle(host)) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    weivel_c_host_free_libraries(host);
    free_the_records(host);
    free_the_parts(host);
    return WEIVEL_C_STATUS_OK;
}
