#ifndef WEIVEL_C_TEST_STANDARD_DISK_H
#define WEIVEL_C_TEST_STANDARD_DISK_H

/**
 * @file
 * Files the `standard` module's tests lay out under a directory each test makes and removes: a test writes every file
 * it needs at run time and removes each by name, so nothing is left in the repository or the build.
 */

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#define DISK_MAKE_DIRECTORY(path) _mkdir(path)
#define DISK_REMOVE_DIRECTORY(path) _rmdir(path)
#else
#include <sys/stat.h>
#include <unistd.h>
#define DISK_MAKE_DIRECTORY(path) mkdir(path, 0755)
#define DISK_REMOVE_DIRECTORY(path) rmdir(path)
#endif

#define DISK_PATH 1024

/** Answers 1 when the joined path fits whole, and 0, the path emptied, when it would be cut. */
static inline int disk_join(char *path, const char *directory, const char *name) {
    int written = snprintf(path, DISK_PATH, "%s/%s", directory, name);
    if (written < 0 || written >= DISK_PATH) {
        path[0] = '\0';
        return 0;
    }
    return 1;
}

static inline int disk_make_directory(const char *path) {
    return DISK_MAKE_DIRECTORY(path) == 0;
}

static inline int disk_remove_directory(const char *path) {
    return DISK_REMOVE_DIRECTORY(path) == 0;
}

static inline int disk_write(const char *directory, const char *name, const char *text) {
    char path[DISK_PATH];
    FILE *file;
    size_t length = strlen(text);
    int written;
    disk_join(path, directory, name);
    file = fopen(path, "wb");
    if (file == NULL) {
        return 0;
    }
    written = fwrite(text, 1, length, file) == length;
    return fclose(file) == 0 && written;
}

static inline int disk_copy_bytes(FILE *from, FILE *to) {
    char buffer[4096];
    size_t count;
    while ((count = fread(buffer, 1, sizeof buffer, from)) > 0) {
        if (fwrite(buffer, 1, count, to) != count) {
            return 0;
        }
    }
    return !ferror(from);
}

/** Copies the file at `from` into `directory` as `name`. */
static inline int disk_copy(const char *from, const char *directory, const char *name) {
    char path[DISK_PATH];
    FILE *source = fopen(from, "rb");
    FILE *target;
    int copied;
    disk_join(path, directory, name);
    target = fopen(path, "wb");
    copied = source != NULL && target != NULL && disk_copy_bytes(source, target);
    if (source != NULL) {
        fclose(source);
    }
    return target != NULL && fclose(target) == 0 && copied;
}

static inline int disk_remove(const char *directory, const char *name) {
    char path[DISK_PATH];
    disk_join(path, directory, name);
    return remove(path) == 0;
}

/** The file name of `path`, what follows its last separator. */
static inline const char *disk_base_name(const char *path) {
    const char *name = path;
    const char *at;
    for (at = path; *at != '\0'; at++) {
        if (*at == '/' || *at == '\\') {
            name = at + 1;
        }
    }
    return name;
}

#endif
