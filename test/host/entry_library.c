/**
 * @file
 * A plugin written in C, built as a shared library: its entry is read by the symbol the test names, and a second
 * symbol answers that it cannot provide one.
 */

#include <stddef.h>

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
    (void)reason;
    self->deactivations++;
    if (self->deactivate_waits) {
        self->deactivation = completion;
        return WEIVEL_C_ENTRY_PENDING;
    }
    return WEIVEL_C_ENTRY_DONE;
}

static uint32_t finish_from_inside(void) {
    volatile uint32_t survived = 0;
    state.finish(state.deactivation, WEIVEL_C_ENTRY_DONE, NULL);
    survived = ENTRY_LIBRARY_SURVIVED;
    return survived;
}

ENTRY_LIBRARY_EXPORT weivel_c_status test_plugin_entry(weivel_c_entry *entry) {
    entry->activate = activate;
    entry->deactivate = deactivate;
    entry->data = &state;
    state.finish_from_inside = finish_from_inside;
    return WEIVEL_C_STATUS_OK;
}

ENTRY_LIBRARY_EXPORT weivel_c_status test_plugin_entry_that_cannot(weivel_c_entry *entry) {
    (void)entry;
    return WEIVEL_C_STATUS_OUT_OF_MEMORY;
}
