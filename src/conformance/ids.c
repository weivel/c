#include <weivel/c/conformance.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vectors.h"

/** What a list of ids keeps: the ids, each its own allocation, and a problem's text. */
typedef struct id_storage {
    char **ids;
    uint32_t count;
    uint32_t capacity;
    int out_of_memory;
    char problem[CONFORMANCE_PROBLEM];
} id_storage;

/** One directory of the walk: the storage it fills, the directory, and its path under the vector directory. */
typedef struct walk {
    id_storage *storage;
    const char *root;
    const char *relative;
} walk;

static int keep_id(id_storage *storage, const char *id) {
    size_t length = strlen(id) + 1;
    char *copy = malloc(length);
    if (storage->count == storage->capacity) {
        uint32_t capacity = storage->capacity == 0 ? 16 : storage->capacity * 2;
        char **grown = realloc(storage->ids, capacity * sizeof *grown);
        if (grown == NULL) {
            free(copy);
            storage->out_of_memory = 1;
            return 0;
        }
        storage->ids = grown;
        storage->capacity = capacity;
    }
    if (copy == NULL) {
        storage->out_of_memory = 1;
        return 0;
    }
    memcpy(copy, id, length);
    storage->ids[storage->count++] = copy;
    return 1;
}

static int ends_with_json(const char *name) {
    size_t length = strlen(name);
    return length > 5 && strcmp(name + length - 5, ".json") == 0;
}

static int walk_into(id_storage *storage, const char *root, const char *relative);

static int visit_entry(void *data, const char *name, int is_directory) {
    walk *at = data;
    char joined[4096];
    snprintf(joined, sizeof joined, "%s%s%s", at->relative, at->relative[0] == '\0' ? "" : "/", name);
    if (is_directory) {
        return walk_into(at->storage, at->root, joined);
    }
    if (!ends_with_json(name)) {
        return 1;
    }
    joined[strlen(joined) - 5] = '\0';
    return keep_id(at->storage, joined);
}

static int walk_into(id_storage *storage, const char *root, const char *relative) {
    char directory[4096];
    walk at = {storage, root, relative};
    snprintf(directory, sizeof directory, "%s%s%s", root, relative[0] == '\0' ? "" : "/", relative);
    return conformance_walk_directory(directory, visit_entry, &at, storage->problem);
}

static int compare_ids(const void *left, const void *right) {
    return strcmp(*(char *const *)left, *(char *const *)right);
}

static int every_vector_names_its_path(id_storage *storage, const char *root) {
    uint32_t index;
    for (index = 0; index < storage->count; index++) {
        conformance_vector vector;
        if (!conformance_read_vector(root, storage->ids[index], &vector, storage->problem)) {
            return 0;
        }
        conformance_vector_end(&vector);
    }
    return 1;
}

static void hand_over(weivel_c_conformance_ids *ids, id_storage *storage, int read) {
    ids->storage = storage;
    ids->items = (const char *const *)storage->ids;
    ids->count = storage->count;
    ids->problem = read ? NULL : storage->problem;
}

weivel_c_status weivel_c_conformance_vector_ids(const char *directory, weivel_c_conformance_ids *ids) {
    id_storage *storage;
    int read;
    if (directory == NULL || ids == NULL || ids->size < sizeof *ids || ids->storage != NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    storage = calloc(1, sizeof *storage);
    if (storage == NULL) {
        return WEIVEL_C_STATUS_OUT_OF_MEMORY;
    }
    read = walk_into(storage, directory, "");
    if (read) {
        qsort(storage->ids, storage->count, sizeof *storage->ids, compare_ids);
        read = every_vector_names_its_path(storage, directory);
    }
    if (storage->out_of_memory) {
        hand_over(ids, storage, 1);
        weivel_c_conformance_ids_release(ids);
        return WEIVEL_C_STATUS_OUT_OF_MEMORY;
    }
    hand_over(ids, storage, read);
    return read ? WEIVEL_C_STATUS_OK : WEIVEL_C_STATUS_FAILED;
}

weivel_c_status weivel_c_conformance_ids_release(weivel_c_conformance_ids *ids) {
    id_storage *storage;
    uint32_t index;
    if (ids == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    storage = ids->storage;
    if (storage != NULL) {
        for (index = 0; index < storage->count; index++) {
            free(storage->ids[index]);
        }
        free(storage->ids);
        free(storage);
    }
    ids->items = NULL;
    ids->count = 0;
    ids->problem = NULL;
    ids->storage = NULL;
    return WEIVEL_C_STATUS_OK;
}
