#include <stdio.h>

#include "layer_vectors.h"

int main(int argument_count, char **arguments) {
    if (argument_count != 5) {
        fprintf(stderr, "usage: conformance_layer_vectors_test <core-library> <layer-vector-directory> "
                        "<echo-library> <described-library>\n");
        return 2;
    }
    layer_core_path = arguments[1];
    layer_vector_directory = arguments[2];
    layer_library_path = arguments[3];
    layer_described_path = arguments[4];
    return run_layer_vector_tests();
}
