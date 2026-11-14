/**
 * @file
 * A plugin written in C, built as a shared library: its entry is read by the symbol the test names, and a second
 * symbol answers that it cannot provide one.
 */

#include "entry_library.h"

#if defined(_WIN32)
#define ENTRY_LIBRARY_EXPORT __declspec(dllexport)
#else
#define ENTRY_LIBRARY_EXPORT __attribute__((visibility("default")))
#endif

static entry_library_state state;

static uint32_t activate(void *data, weivel_c_context *context, weivel_c_completion *completion,
                         const char **reason) {
    entry_library_state *self = data;
    (void)completion;
    (void)reason;
    self->activations++;
    self->context = context;
    return WEIVEL_C_ENTRY_DONE;
}

static uint32_t deactivate(void *data, weivel_c_context *context, weivel_c_completion *completion,
                           const char **reason) {
    entry_library_state *self = data;
    (void)context;
    (void)completion;
    (void)reason;
    self->deactivations++;
    return WEIVEL_C_ENTRY_DONE;
}

ENTRY_LIBRARY_EXPORT weivel_c_status test_plugin_entry(weivel_c_entry *entry) {
    entry->activate = activate;
    entry->deactivate = deactivate;
    entry->data = &state;
    return WEIVEL_C_STATUS_OK;
}

ENTRY_LIBRARY_EXPORT weivel_c_status test_plugin_entry_that_cannot(weivel_c_entry *entry) {
    (void)entry;
    return WEIVEL_C_STATUS_OUT_OF_MEMORY;
}
