/**
 * @file
 * @implNote MSVC calls the standard `fopen` unsafe unless told not to before any header is read.
 */

#define _CRT_SECURE_NO_WARNINGS

#include "vectors.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <weivel/core.h>

static void keep_whole_if_cut(char *problem, int written) {
    static const char cut[] = " (cut short)";
    if (written < 0) {
        memcpy(problem, "a problem whose text could not be written", 42);
    } else if (written >= CONFORMANCE_PROBLEM) {
        memcpy(problem + CONFORMANCE_PROBLEM - sizeof cut, cut, sizeof cut);
    }
}

static long length_of(FILE *file) {
    long length;
    if (fseek(file, 0, SEEK_END) != 0) {
        return -1;
    }
    length = ftell(file);
    return fseek(file, 0, SEEK_SET) == 0 ? length : -1;
}

char *conformance_read_file(const char *path, size_t *length, char *problem) {
    FILE *file = fopen(path, "rb");
    long measured = file == NULL ? -1 : length_of(file);
    char *bytes = measured < 0 ? NULL : malloc((size_t)measured + 1);
    if (bytes != NULL && fread(bytes, 1, (size_t)measured, file) != (size_t)measured) {
        free(bytes);
        bytes = NULL;
    }
    if (file != NULL) {
        fclose(file);
    }
    if (bytes == NULL) {
        keep_whole_if_cut(problem, snprintf(problem, CONFORMANCE_PROBLEM, "the file %s cannot be read", path));
        return NULL;
    }
    bytes[measured] = '\0';
    *length = (size_t)measured;
    return bytes;
}

static weivel_json_value *parse_file(const char *path, char *problem) {
    weivel_json_error error = WEIVEL_INIT(weivel_json_error);
    size_t length = 0;
    char *text = conformance_read_file(path, &length, problem);
    weivel_json_value *document = text == NULL ? NULL : weivel_json_parse(text, length, &error);
    if (text != NULL && document == NULL) {
        keep_whole_if_cut(problem, snprintf(problem, CONFORMANCE_PROBLEM, "%s is not JSON at line %d column %d: %s",
                                            path, error.line, error.column,
                                            error.message == NULL ? "" : error.message));
    }
    free(text);
    return document;
}

static int read_parts(conformance_vector *vector, const char *relative, const char *path, char *problem) {
    vector->id = weivel_json_as_string(weivel_json_object_get(vector->document, "id"));
    vector->op = weivel_json_as_string(weivel_json_object_get(vector->document, "op"));
    vector->given = weivel_json_object_get(vector->document, "given");
    vector->then = weivel_json_object_get(vector->document, "then");
    if (vector->id == NULL || strcmp(vector->id, relative) != 0) {
        keep_whole_if_cut(problem,
                          snprintf(problem, CONFORMANCE_PROBLEM,
                                   "the layer vector at %s names itself %s, where its path names %s", path,
                                   vector->id == NULL ? "nothing" : vector->id, relative));
        return 0;
    }
    if (vector->op == NULL || weivel_json_kind_of(vector->given) != WEIVEL_JSON_OBJECT ||
        weivel_json_kind_of(vector->then) != WEIVEL_JSON_OBJECT) {
        keep_whole_if_cut(problem, snprintf(problem, CONFORMANCE_PROBLEM,
                                            "the layer vector at %s lacks an op, a given or a then", path));
        return 0;
    }
    return 1;
}

int conformance_read_vector(const char *directory, const char *relative, conformance_vector *vector, char *problem) {
    char path[4096];
    int written;
    memset(vector, 0, sizeof *vector);
    written = snprintf(path, sizeof path, "%s/%s.json", directory, relative);
    if (written < 0 || (size_t)written >= sizeof path) {
        keep_whole_if_cut(problem, snprintf(problem, CONFORMANCE_PROBLEM,
                                            "the path of the layer vector %s under %s is longer than %u bytes",
                                            relative, directory, (unsigned)(sizeof path - 1)));
        return 0;
    }
    vector->document = parse_file(path, problem);
    if (vector->document == NULL) {
        return 0;
    }
    if (!read_parts(vector, relative, path, problem)) {
        conformance_vector_end(vector);
        return 0;
    }
    return 1;
}

void conformance_vector_end(conformance_vector *vector) {
    weivel_json_free(vector->document);
    memset(vector, 0, sizeof *vector);
}
