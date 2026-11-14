#include <stdio.h>

#include "layer_vectors.h"

int main(int argument_count, char **arguments) {
    if (argument_count != 3) {
        fprintf(stderr, "usage: conformance_layer_vectors_test <core-library> <layer-vector-directory>\n");
        return 2;
    }
    layer_core_path = arguments[1];
    layer_vector_directory = arguments[2];
    return run_layer_vector_tests();
}
