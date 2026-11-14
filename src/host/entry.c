#include "host.h"
#include "libraries.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "../core/binary.h"

#define END_OF(type, field) (offsetof(type, field) + sizeof(((type *)0)->field))
#define FIRST_ENTRY_SIZE END_OF(weivel_c_entry, library)
#define FIRST_FAILURE_SIZE END_OF(weivel_c_entry_load_failure, provided)

static weivel_c_status tell_the_outcome(void *data, weivel_c_core *core);

void weivel_c_host_ready_completion(weivel_c_completion *completion, host_plugin *plugin, uint32_t purpose) {
    memset(completion, 0, sizeof *completion);
    completion->host = plugin->host;
    completion->plugin = plugin;
    completion->purpose = purpose;
}

static void ready_the_outcome(host_outcome *outcome, weivel_c_completion *activation, weivel_handle plugin) {
    outcome->step = (weivel_c_step)WEIVEL_INIT(weivel_c_step);
    outcome->step.run = tell_the_outcome;
    outcome->step.data = activation;
    outcome->refusal = (weivel_c_refusal)WEIVEL_INIT(weivel_c_refusal);
    outcome->plugin = plugin;
}

weivel_c_completion *weivel_c_host_make_completion(weivel_c_host *host, host_plugin *plugin, weivel_handle handle) {
    weivel_c_completion *made = calloc(1, sizeof *made);
    if (made == NULL) {
        return NULL;
    }
    made->host = host;
    made->plugin = plugin;
    made->purpose = COMPLETION_OF_AN_ACTIVATE;
    ready_the_outcome(&made->outcome, made, handle);
    weivel_c_mutex_lock(&host->lock);
    made->next = host->completions;
    host->completions = made;
    weivel_c_mutex_unlock(&host->lock);
    return made;
}

int weivel_c_host_choose_outcome(weivel_c_completion *activation, uint32_t kind) {
    if (activation->outcome.kind != 0) {
        return 0;
    }
    activation->outcome.kind = kind;
    return 1;
}

static int tells_a_refusal(uint32_t kind) {
    return kind == WEIVEL_C_OUTCOME_REFUSED || kind == WEIVEL_C_OUTCOME_QUARANTINED;
}

/** The step that tells an outcome: it runs in the dispatch, once, and then releases the refusal it told. */
static weivel_c_status tell_the_outcome(void *data, weivel_c_core *core) {
    weivel_c_completion *activation = data;
    host_outcome *outcome = &activation->outcome;
    weivel_c_outcome told = WEIVEL_INIT(weivel_c_outcome);
    weivel_c_status heard;
    (void)core;
    told.plugin = outcome->plugin;
    told.import_name = activation->plugin == NULL ? NULL : activation->plugin->manifest->import_name;
    told.kind = outcome->kind;
    if (tells_a_refusal(outcome->kind)) {
        told.refusal = outcome->refusal;
    }
    heard = outcome->listener.hear(outcome->listener.data, &told);
    weivel_c_refusal_release(&outcome->refusal);
    if (heard != WEIVEL_C_STATUS_OK) {
        weivel_c_host_report(activation->host, WEIVEL_C_FAILURE_OUTCOME_LISTENER, heard,
                             "an activation's outcome listener answered a failure");
    }
    return WEIVEL_C_STATUS_OK;
}

void weivel_c_host_tell_outcome(weivel_c_completion *activation) {
    if (weivel_c_core_defer(activation->host->core, &activation->outcome.step, NULL) != WEIVEL_C_STATUS_OK) {
        weivel_c_refusal_release(&activation->outcome.refusal);
    }
}

static int is_an_answer(uint32_t answer) {
    return answer == WEIVEL_C_ENTRY_DONE || answer == WEIVEL_C_ENTRY_FAILED;
}

weivel_c_status weivel_c_completion_finish(weivel_c_completion *completion, uint32_t answer, const char *reason) {
    if (completion == NULL || !is_an_answer(answer)) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    if (completion->purpose == COMPLETION_OF_A_DEACTIVATE) {
        return weivel_c_host_settle_deactivation(completion, answer, reason);
    }
    return weivel_c_host_settle_activation(completion, answer, reason);
}

static int is_writable(const weivel_c_entry_load_failure *failure) {
    return failure != NULL && failure->size >= FIRST_FAILURE_SIZE;
}

static int is_loadable(const char *path, const char *symbol, const weivel_c_entry *entry,
                       const weivel_c_entry_load_failure *failure) {
    return path != NULL && path[0] != '\0' && symbol != NULL && symbol[0] != '\0' && entry != NULL &&
           entry->size >= FIRST_ENTRY_SIZE && entry->library == NULL && (failure == NULL || is_writable(failure));
}

static size_t covered_size(const weivel_c_entry *entry) {
    return entry->size < sizeof *entry ? entry->size : sizeof *entry;
}

/** Calls the provider `symbol` names into the layer's own entry, and answers what it filled. */
static weivel_c_status provide(void *binary, const char *symbol, weivel_c_entry *read,
                               weivel_c_entry_load_failure *failure) {
    weivel_c_binary_function found = weivel_c_binary_find(binary, symbol);
    weivel_c_status provided;
    if (found == NULL) {
        if (failure != NULL) {
            failure->missing_symbol = symbol;
        }
        return WEIVEL_C_STATUS_MISSING_SYMBOL;
    }
    provided = ((weivel_c_entry_provider)found)(read);
    if (provided != WEIVEL_C_STATUS_OK || read->activate == NULL) {
        if (failure != NULL) {
            failure->provided = (uint32_t)provided;
        }
        return WEIVEL_C_STATUS_FAILED;
    }
    return WEIVEL_C_STATUS_OK;
}

weivel_c_status weivel_c_entry_load(const char *path, const char *symbol, weivel_c_entry *entry,
                                    weivel_c_entry_load_failure *failure) {
    weivel_c_entry read;
    void *binary = NULL;
    weivel_c_status status;
    if (is_writable(failure)) {
        failure->missing_symbol = NULL;
        failure->provided = WEIVEL_C_STATUS_OK;
    }
    if (!is_loadable(path, symbol, entry, failure)) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    status = weivel_c_binary_open(path, &binary);
    if (status != WEIVEL_C_STATUS_OK) {
        return status;
    }
    memset(&read, 0, sizeof read);
    read.size = (uint32_t)covered_size(entry);
    status = provide(binary, symbol, &read, failure);
    if (status != WEIVEL_C_STATUS_OK) {
        weivel_c_host_close_library_binary(binary);
        return status;
    }
    read.size = entry->size;
    read.library = binary;
    memcpy(entry, &read, covered_size(entry));
    return WEIVEL_C_STATUS_OK;
}

weivel_c_status weivel_c_entry_unload(weivel_c_entry *entry) {
    uint32_t size;
    int closed;
    if (entry == NULL || entry->size < FIRST_ENTRY_SIZE) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    if (entry->library == NULL) {
        return WEIVEL_C_STATUS_OK;
    }
    closed = weivel_c_binary_close(entry->library);
    size = entry->size;
    memset(entry, 0, covered_size(entry));
    entry->size = size;
    return closed ? WEIVEL_C_STATUS_OK : WEIVEL_C_STATUS_NOT_UNLOADED;
}

void weivel_c_host_close_library(host_plugin *plugin) {
    void *library = plugin->entry.library;
    plugin->entry.library = NULL;
    if (library != NULL && !weivel_c_host_close_library_binary(library)) {
        weivel_c_host_report(plugin->host, WEIVEL_C_FAILURE_UNLOAD, WEIVEL_C_STATUS_NOT_UNLOADED,
                             "the library a plugin's entry was read from could not be closed");
    }
}
