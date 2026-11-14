#ifndef WEIVEL_C_CONFORMANCE_VECTORS_H
#define WEIVEL_C_CONFORMANCE_VECTORS_H

/**
 * @file
 * Reading the kit's layer vectors from where the caller says the kit is, with the standard's JSON reader.
 */

#include <stddef.h>
#include <stdint.h>

#include <weivel/json.h>

#define CONFORMANCE_PROBLEM 512

/**
 * One layer vector: its document as the reader holds it, and the parts the driver reads.
 *
 * @implNote No `size`: it is the driver's own and never crosses a boundary.
 */
typedef struct conformance_vector {
    weivel_json_value *document;
    const char *id;
    const char *op;
    const weivel_json_value *given;
    const weivel_json_value *then;
} conformance_vector;

/** Reads the file at `path` whole. NULL, `problem` written, when it cannot be read. The caller frees it. */
char *conformance_read_file(const char *path, size_t *length, char *problem);

/**
 * Reads the vector whose file is `<directory>/<relative>.json`, and refuses it unless it names itself `relative`.
 * Answers 1, or 0 with `problem` written.
 */
int conformance_read_vector(const char *directory, const char *relative, conformance_vector *vector, char *problem);

void conformance_vector_end(conformance_vector *vector);

/**
 * What a directory walk is told of one entry of a directory: its name, and whether it is a directory itself. It
 * answers 1 to go on and 0 to stop the walk.
 */
typedef int (*conformance_entry_visitor)(void *data, const char *name, int is_directory);

/**
 * Tells `visit` of every entry of `directory` but `.` and `..`, in no particular order. Answers 1 when the directory
 * was read whole, 0 with `problem` written when it could not be or `visit` stopped.
 */
int conformance_walk_directory(const char *directory, conformance_entry_visitor visit, void *data, char *problem);

#endif
