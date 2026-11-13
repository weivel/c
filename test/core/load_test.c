#include <stddef.h>

#include <weivel/c.h>

#include "check.h"

typedef void (*any_function)(void);

typedef struct {
    weivel_c_core_table table;
    any_function later[2];
} wider_table;

static const char *core_path;
static const char *partial_core_path;
static const char *text_file_path;

static void never_called(void) {
}

static size_t function_count(void) {
    return (sizeof(weivel_c_core_table) - offsetof(weivel_c_core_table, core_create)) / sizeof(any_function);
}

static size_t count_resolved(const weivel_c_core_table *table) {
    const char *first = (const char *)table + offsetof(weivel_c_core_table, core_create);
    size_t resolved = 0;
    size_t at;
    for (at = 0; at < function_count(); at++) {
        any_function function;
        memcpy(&function, first + at * sizeof function, sizeof function);
        resolved += function != NULL;
    }
    return resolved;
}

static int is_empty(const weivel_c_core_table *table) {
    return table->size == sizeof *table && table->library == NULL && count_resolved(table) == 0;
}

static void loads_the_core_and_calls_through_the_table(void) {
    weivel_c_core_table table = WEIVEL_INIT(weivel_c_core_table);
    weivel_c_core_load_failure failure = WEIVEL_INIT(weivel_c_core_load_failure);
    weivel_core *core = NULL;
    weivel_event event = WEIVEL_INIT(weivel_event);
    uint32_t pending = 7;
    failure.missing_symbol = "stale";
    CHECK(weivel_c_core_load(core_path, &table, &failure) == WEIVEL_C_STATUS_OK);
    CHECK(failure.missing_symbol == NULL);
    CHECK(table.library != NULL);
    CHECK(table.size == sizeof table);
    if (table.library != NULL) {
        CHECK(table.core_create(NULL, &core, &pending) == WEIVEL_STATUS_OK);
        CHECK(core != NULL);
        CHECK(pending == 0);
        CHECK(table.next_event(core, &event, &pending) == WEIVEL_STATUS_NONE);
        CHECK(table.core_destroy(core) == WEIVEL_STATUS_OK);
        CHECK(weivel_c_core_unload(&table) == WEIVEL_C_STATUS_OK);
    }
}

static void resolves_every_function_of_the_table(void) {
    weivel_c_core_table table = WEIVEL_INIT(weivel_c_core_table);
    CHECK(sizeof(weivel_c_core_table) ==
          offsetof(weivel_c_core_table, core_create) + function_count() * sizeof(any_function));
    CHECK(weivel_c_core_load(core_path, &table, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(count_resolved(&table) == function_count());
    CHECK(weivel_c_core_unload(&table) == WEIVEL_C_STATUS_OK);
}

static void answers_a_path_that_names_no_file_as_a_value(void) {
    weivel_c_core_table table = WEIVEL_INIT(weivel_c_core_table);
    weivel_c_core_load_failure failure = WEIVEL_INIT(weivel_c_core_load_failure);
    char absent[1024];
    snprintf(absent, sizeof absent, "%s.absent", text_file_path);
    CHECK(weivel_c_core_load(absent, &table, &failure) == WEIVEL_C_STATUS_NOT_A_LIBRARY);
    CHECK(failure.missing_symbol == NULL);
    CHECK(is_empty(&table));
    CHECK(weivel_c_core_load("", &table, &failure) == WEIVEL_C_STATUS_NOT_A_LIBRARY);
    CHECK(is_empty(&table));
}

static void answers_a_file_that_is_no_library_as_a_value(void) {
    weivel_c_core_table table = WEIVEL_INIT(weivel_c_core_table);
    weivel_c_core_load_failure failure = WEIVEL_INIT(weivel_c_core_load_failure);
    CHECK(weivel_c_core_load(text_file_path, &table, &failure) == WEIVEL_C_STATUS_NOT_A_LIBRARY);
    CHECK(failure.missing_symbol == NULL);
    CHECK(is_empty(&table));
}

static void names_the_first_function_a_library_lacks(void) {
    weivel_c_core_table table = WEIVEL_INIT(weivel_c_core_table);
    weivel_c_core_load_failure failure = WEIVEL_INIT(weivel_c_core_load_failure);
    CHECK(weivel_c_core_load(partial_core_path, &table, &failure) == WEIVEL_C_STATUS_MISSING_SYMBOL);
    check_text(failure.missing_symbol, "weivel_next_event", "the missing symbol");
    CHECK(is_empty(&table));
    CHECK(weivel_c_core_load(partial_core_path, &table, NULL) == WEIVEL_C_STATUS_MISSING_SYMBOL);
    CHECK(is_empty(&table));
}

static void refuses_a_misused_load_and_changes_nothing(void) {
    weivel_c_core_table table = WEIVEL_INIT(weivel_c_core_table);
    weivel_c_core_table small = WEIVEL_INIT(weivel_c_core_table);
    weivel_c_core_load_failure failure = WEIVEL_INIT(weivel_c_core_load_failure);
    weivel_c_core_load_failure small_failure = {.size = sizeof(uint32_t), .missing_symbol = "untouched"};
    CHECK(weivel_c_core_load(NULL, &table, &failure) == WEIVEL_C_STATUS_MISUSE);
    CHECK(weivel_c_core_load(core_path, NULL, &failure) == WEIVEL_C_STATUS_MISUSE);
    small.size = sizeof small - 1;
    CHECK(weivel_c_core_load(core_path, &small, &failure) == WEIVEL_C_STATUS_MISUSE);
    CHECK(small.library == NULL && count_resolved(&small) == 0);
    CHECK(weivel_c_core_load(core_path, &table, &small_failure) == WEIVEL_C_STATUS_MISUSE);
    check_text(small_failure.missing_symbol, "untouched", "a failure too small to write");
    CHECK(is_empty(&table));
}

static void refuses_to_load_over_a_loaded_table(void) {
    weivel_c_core_table table = WEIVEL_INIT(weivel_c_core_table);
    void *library;
    CHECK(weivel_c_core_load(core_path, &table, NULL) == WEIVEL_C_STATUS_OK);
    library = table.library;
    CHECK(weivel_c_core_load(core_path, &table, NULL) == WEIVEL_C_STATUS_MISUSE);
    CHECK(table.library == library);
    CHECK(count_resolved(&table) == function_count());
    CHECK(weivel_c_core_unload(&table) == WEIVEL_C_STATUS_OK);
}

static void leaves_what_lies_past_the_table_it_knows(void) {
    wider_table wider;
    memset(&wider, 0, sizeof wider);
    wider.table.size = (uint32_t)sizeof wider;
    wider.later[0] = never_called;
    wider.later[1] = never_called;
    CHECK(weivel_c_core_load(core_path, &wider.table, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(wider.table.size == sizeof wider);
    CHECK(count_resolved(&wider.table) == function_count());
    CHECK(wider.later[0] == never_called && wider.later[1] == never_called);
    CHECK(weivel_c_core_unload(&wider.table) == WEIVEL_C_STATUS_OK);
    CHECK(wider.table.size == sizeof wider);
    CHECK(wider.later[0] == never_called && wider.later[1] == never_called);
}

static void unload_empties_the_table_and_is_safe_to_repeat(void) {
    weivel_c_core_table table = WEIVEL_INIT(weivel_c_core_table);
    weivel_c_core_table small = WEIVEL_INIT(weivel_c_core_table);
    CHECK(weivel_c_core_load(core_path, &table, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_core_unload(&table) == WEIVEL_C_STATUS_OK);
    CHECK(is_empty(&table));
    CHECK(weivel_c_core_unload(&table) == WEIVEL_C_STATUS_OK);
    CHECK(is_empty(&table));
    CHECK(weivel_c_core_unload(NULL) == WEIVEL_C_STATUS_MISUSE);
    small.size = sizeof small - 1;
    small.library = &small;
    CHECK(weivel_c_core_unload(&small) == WEIVEL_C_STATUS_MISUSE);
    CHECK(small.library == &small);
    CHECK(weivel_c_core_load(core_path, &table, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(count_resolved(&table) == function_count());
    CHECK(weivel_c_core_unload(&table) == WEIVEL_C_STATUS_OK);
}

int main(int argument_count, char **arguments) {
    if (argument_count != 4) {
        fprintf(stderr, "usage: load_test <core-library> <partial-core-library> <text-file>\n");
        return 2;
    }
    core_path = arguments[1];
    partial_core_path = arguments[2];
    text_file_path = arguments[3];
    loads_the_core_and_calls_through_the_table();
    resolves_every_function_of_the_table();
    answers_a_path_that_names_no_file_as_a_value();
    answers_a_file_that_is_no_library_as_a_value();
    names_the_first_function_a_library_lacks();
    refuses_a_misused_load_and_changes_nothing();
    refuses_to_load_over_a_loaded_table();
    leaves_what_lies_past_the_table_it_knows();
    unload_empties_the_table_and_is_safe_to_repeat();
    return check_summary("core load");
}
