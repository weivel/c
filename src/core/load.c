#include <stddef.h>
#include <string.h>

#include <weivel/c/core.h>

#include "binary.h"
#include "functions.h"

typedef struct {
    const char *symbol;
    size_t offset;
} table_slot;

#define SLOT(name) {"weivel_" #name, offsetof(weivel_c_core_table, name)},
static const table_slot SLOTS[] = {WEIVEL_C_CORE_FUNCTIONS(SLOT)};
#undef SLOT

#define SLOT_COUNT (sizeof SLOTS / sizeof SLOTS[0])
#define END_OF(type, field) (offsetof(type, field) + sizeof(((type *)0)->field))
#define FIRST_TABLE_SIZE END_OF(weivel_c_core_table, ledger_row_release)
#define FIRST_FAILURE_SIZE END_OF(weivel_c_core_load_failure, missing_symbol)

/**
 * @implNote Unevaluated, so it names no core function to the linker, yet a field whose type drifts from the core's
 * declaration fails to compile.
 */
#define SAME_TYPE_AS_THE_CORE(name) \
    _Static_assert(sizeof(1 ? ((weivel_c_core_table *)0)->name : weivel_##name) > 0, #name);
WEIVEL_C_CORE_FUNCTIONS(SAME_TYPE_AS_THE_CORE)
#undef SAME_TYPE_AS_THE_CORE

_Static_assert(sizeof(weivel_c_binary_function) == sizeof(((weivel_c_core_table *)0)->core_create),
               "every function pointer must have one size");

static size_t covered_size(const weivel_c_core_table *table) {
    return table->size < sizeof *table ? table->size : sizeof *table;
}

static int is_covered(const table_slot *slot, size_t size) {
    return slot->offset + sizeof(weivel_c_binary_function) <= size;
}

/** Fills every slot `size` covers, or answers the first symbol the binary lacks. */
static const char *resolve_slots(void *binary, weivel_c_core_table *loaded, size_t size) {
    size_t at;
    for (at = 0; at < SLOT_COUNT && is_covered(&SLOTS[at], size); at++) {
        weivel_c_binary_function function = weivel_c_binary_find(binary, SLOTS[at].symbol);
        if (function == NULL) {
            return SLOTS[at].symbol;
        }
        memcpy((char *)loaded + SLOTS[at].offset, &function, sizeof function);
    }
    return NULL;
}

static int is_writable(const weivel_c_core_load_failure *failure) {
    return failure != NULL && failure->size >= FIRST_FAILURE_SIZE;
}

static int is_loadable(const char *path, const weivel_c_core_table *table,
                       const weivel_c_core_load_failure *failure) {
    return path != NULL && table != NULL && table->size >= FIRST_TABLE_SIZE && table->library == NULL &&
           (failure == NULL || is_writable(failure));
}

static void report_missing(weivel_c_core_load_failure *failure, const char *symbol) {
    if (is_writable(failure)) {
        failure->missing_symbol = symbol;
    }
}

weivel_c_status weivel_c_core_load(const char *path, weivel_c_core_table *table,
                                   weivel_c_core_load_failure *failure) {
    weivel_c_core_table loaded;
    const char *missing;
    void *binary = NULL;
    weivel_c_status status;
    report_missing(failure, NULL);
    if (!is_loadable(path, table, failure)) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    status = path[0] == '\0' ? WEIVEL_C_STATUS_NOT_A_LIBRARY : weivel_c_binary_open(path, &binary);
    if (status != WEIVEL_C_STATUS_OK) {
        return status;
    }
    memset(&loaded, 0, sizeof loaded);
    missing = resolve_slots(binary, &loaded, covered_size(table));
    if (missing != NULL) {
        weivel_c_binary_close(binary);
        report_missing(failure, missing);
        return WEIVEL_C_STATUS_MISSING_SYMBOL;
    }
    loaded.size = table->size;
    loaded.library = binary;
    memcpy(table, &loaded, covered_size(table));
    return WEIVEL_C_STATUS_OK;
}

weivel_c_status weivel_c_core_unload(weivel_c_core_table *table) {
    uint32_t size;
    int closed;
    if (table == NULL || table->size < FIRST_TABLE_SIZE) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    if (table->library == NULL) {
        return WEIVEL_C_STATUS_OK;
    }
    closed = weivel_c_binary_close(table->library);
    size = table->size;
    memset(table, 0, covered_size(table));
    table->size = size;
    return closed ? WEIVEL_C_STATUS_OK : WEIVEL_C_STATUS_NOT_UNLOADED;
}
