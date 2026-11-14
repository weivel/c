#include "vectors.h"

#include <stdio.h>
#include <string.h>

#include <windows.h>

static int is_dot_entry(const char *name) {
    return strcmp(name, ".") == 0 || strcmp(name, "..") == 0;
}

int conformance_walk_directory(const char *directory, conformance_entry_visitor visit, void *data, char *problem) {
    char pattern[MAX_PATH];
    WIN32_FIND_DATAA found;
    HANDLE search;
    int went_on = 1;
    snprintf(pattern, sizeof pattern, "%s\\*", directory);
    search = FindFirstFileA(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) {
        snprintf(problem, CONFORMANCE_PROBLEM, "the directory %s cannot be read", directory);
        return 0;
    }
    do {
        if (!is_dot_entry(found.cFileName)) {
            went_on = visit(data, found.cFileName, (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
        }
    } while (went_on && FindNextFileA(search, &found));
    FindClose(search);
    return went_on;
}
