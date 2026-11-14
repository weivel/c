#define _POSIX_C_SOURCE 200809L

#include "vectors.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static int is_dot_entry(const char *name) {
    return strcmp(name, ".") == 0 || strcmp(name, "..") == 0;
}

static int names_a_directory(const char *directory, const char *name) {
    char path[4096];
    struct stat status;
    snprintf(path, sizeof path, "%s/%s", directory, name);
    return stat(path, &status) == 0 && S_ISDIR(status.st_mode);
}

int conformance_walk_directory(const char *directory, conformance_entry_visitor visit, void *data, char *problem) {
    DIR *opened = opendir(directory);
    struct dirent *entry;
    int went_on = 1;
    if (opened == NULL) {
        snprintf(problem, CONFORMANCE_PROBLEM, "the directory %s cannot be read", directory);
        return 0;
    }
    while (went_on && (entry = readdir(opened)) != NULL) {
        if (!is_dot_entry(entry->d_name)) {
            went_on = visit(data, entry->d_name, names_a_directory(directory, entry->d_name));
        }
    }
    closedir(opened);
    return went_on;
}
