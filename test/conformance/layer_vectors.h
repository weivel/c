#ifndef WEIVEL_C_TEST_CONFORMANCE_LAYER_VECTORS_H
#define WEIVEL_C_TEST_CONFORMANCE_LAYER_VECTORS_H

/**
 * @file
 * The layer suite: one test per layer vector of the pinned kit, each named by the vector's id, in one table that
 * layer_vectors.c defines, the runner runs and the coverage test reads.
 */

#include <stdint.h>

/** One test of the suite: the id of the vector it witnesses, and the test, handed that id. */
typedef struct layer_vector_test {
    const char *id;
    void (*run)(const char *id);
} layer_vector_test;

extern const layer_vector_test LAYER_VECTOR_TESTS[];
extern const uint32_t LAYER_VECTOR_TEST_COUNT;

/**
 * Where the suite runs: the core's binary, the kit's layer vector directory and the core's echo library a `library`
 * vector loads, as the build passed them.
 */
extern const char *layer_core_path;
extern const char *layer_vector_directory;
extern const char *layer_library_path;

/** Runs every test of the table, and answers what check_summary answers for them. */
int run_layer_vector_tests(void);

#endif
