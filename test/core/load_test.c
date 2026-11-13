/**
 * @file
 * The loader, against the real core's binary, a library that lacks a function and a file that is no library.
 *
 * @implNote MSVC calls the standard `fopen` unsafe unless told not to before any header is read.
 */

#define _CRT_SECURE_NO_WARNINGS

#include <stddef.h>
#include <stdlib.h>

#include <weivel/c.h>

#include "check.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define PATH_SIZE 2048
/** Two letters no single-byte code page spells together, as UTF-8 bytes: a sharp s and a CJK ideograph. */
#define NAME_BEYOND_ASCII "\xC3\x9F\xE6\xA0\xB8"

typedef void (*any_function)(void);

typedef struct {
    weivel_c_core_table table;
    any_function later[2];
} wider_table;

static const char *core_path;
static const char *partial_core_path;
static const char *text_file_path;
static const char *scratch_prefix;

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
    failure.missing_symbol = "stale";
    CHECK(weivel_c_core_load(NULL, &table, &failure) == WEIVEL_C_STATUS_MISUSE);
    CHECK(failure.missing_symbol == NULL);
    failure.missing_symbol = "stale";
    CHECK(weivel_c_core_load(core_path, NULL, &failure) == WEIVEL_C_STATUS_MISUSE);
    CHECK(failure.missing_symbol == NULL);
    small.size = sizeof small - 1;
    failure.missing_symbol = "stale";
    CHECK(weivel_c_core_load(core_path, &small, &failure) == WEIVEL_C_STATUS_MISUSE);
    CHECK(failure.missing_symbol == NULL);
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

#if defined(_WIN32)

static int to_wide(const char *path, wchar_t *wide) {
    return MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, PATH_SIZE) > 0;
}

static int copy_file(const char *from, const char *to) {
    wchar_t wide_from[PATH_SIZE];
    wchar_t wide_to[PATH_SIZE];
    return to_wide(from, wide_from) && to_wide(to, wide_to) && CopyFileW(wide_from, wide_to, FALSE) != 0;
}

static void remove_file(const char *path) {
    wchar_t wide[PATH_SIZE];
    if (to_wide(path, wide)) {
        DeleteFileW(wide);
    }
}

#else

static int copy_file(const char *from, const char *to) {
    char block[4096];
    FILE *source = fopen(from, "rb");
    FILE *target = source == NULL ? NULL : fopen(to, "wb");
    size_t count;
    int copied = target != NULL;
    while (copied && (count = fread(block, 1, sizeof block, source)) > 0) {
        copied = fwrite(block, 1, count, target) == count;
    }
    if (source != NULL) {
        fclose(source);
    }
    return target != NULL && fclose(target) == 0 && copied;
}

static void remove_file(const char *path) {
    remove(path);
}

#endif

/** @implNote Removed before it is written: macOS keeps a signature's verdict with the file it first read. */
static int copy_the_core_to(const char *path) {
    remove_file(path);
    return copy_file(core_path, path);
}

static void loads_from_a_path_beyond_ascii(void) {
    weivel_c_core_table table = WEIVEL_INIT(weivel_c_core_table);
    weivel_core *core = NULL;
    char path[PATH_SIZE];
    snprintf(path, sizeof path, "%s_" NAME_BEYOND_ASCII ".library", scratch_prefix);
    CHECK(copy_the_core_to(path));
    CHECK(weivel_c_core_load(path, &table, NULL) == WEIVEL_C_STATUS_OK);
    if (table.library != NULL) {
        CHECK(table.core_create(NULL, &core, NULL) == WEIVEL_STATUS_OK);
        CHECK(table.core_destroy(core) == WEIVEL_STATUS_OK);
        CHECK(weivel_c_core_unload(&table) == WEIVEL_C_STATUS_OK);
    }
    remove_file(path);
}

#if defined(_WIN32)

/** The library sits where a lenient conversion of the path would look, so only a refused conversion misses it. */
static void refuses_a_path_that_is_not_utf_8(void) {
    weivel_c_core_table table = WEIVEL_INIT(weivel_c_core_table);
    char replaced[PATH_SIZE];
    char invalid[PATH_SIZE];
    snprintf(replaced, sizeof replaced, "%s_\xEF\xBF\xBD.library", scratch_prefix);
    snprintf(invalid, sizeof invalid, "%s_\xFF.library", scratch_prefix);
    CHECK(copy_the_core_to(replaced));
    CHECK(weivel_c_core_load(invalid, &table, NULL) == WEIVEL_C_STATUS_NOT_A_LIBRARY);
    CHECK(is_empty(&table));
    weivel_c_core_unload(&table);
    remove_file(replaced);
}

static unsigned long error_mode_at_load(void) {
    char noted[16] = "";
    GetEnvironmentVariableA("PARTIAL_CORE_ERROR_MODE_AT_LOAD", noted, sizeof noted);
    return strtoul(noted, NULL, 10);
}

static void adds_to_the_threads_error_mode_and_restores_it(void) {
    weivel_c_core_table table = WEIVEL_INIT(weivel_c_core_table);
    DWORD before = 0;
    DWORD mine = SEM_NOOPENFILEERRORBOX;
    CHECK(SetThreadErrorMode(mine, &before));
    SetEnvironmentVariableA("PARTIAL_CORE_ERROR_MODE_AT_LOAD", NULL);
    CHECK(weivel_c_core_load(partial_core_path, &table, NULL) == WEIVEL_C_STATUS_MISSING_SYMBOL);
    CHECK(error_mode_at_load() == (unsigned long)(mine | SEM_FAILCRITICALERRORS));
    CHECK(GetThreadErrorMode() == mine);
    SetThreadErrorMode(before, NULL);
}

static void run_the_tests_of_one_platform(void) {
    refuses_a_path_that_is_not_utf_8();
    adds_to_the_threads_error_mode_and_restores_it();
}

#else

static void run_the_tests_of_one_platform(void) {
}

#endif

int main(int argument_count, char **arguments) {
    if (argument_count != 5) {
        fprintf(stderr, "usage: load_test <core-library> <partial-core-library> <text-file> <scratch-prefix>\n");
        return 2;
    }
    core_path = arguments[1];
    partial_core_path = arguments[2];
    text_file_path = arguments[3];
    scratch_prefix = arguments[4];
    loads_the_core_and_calls_through_the_table();
    resolves_every_function_of_the_table();
    answers_a_path_that_names_no_file_as_a_value();
    answers_a_file_that_is_no_library_as_a_value();
    names_the_first_function_a_library_lacks();
    refuses_a_misused_load_and_changes_nothing();
    refuses_to_load_over_a_loaded_table();
    leaves_what_lies_past_the_table_it_knows();
    unload_empties_the_table_and_is_safe_to_repeat();
    loads_from_a_path_beyond_ascii();
    run_the_tests_of_one_platform();
    return check_summary("core load");
}
