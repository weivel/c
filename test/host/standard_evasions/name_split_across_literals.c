#include "../../../src/core/binary.h"

weivel_c_binary_function find_the_reader(void *binary) {
    return weivel_c_binary_find(binary, "weivel_standard_" "read_library");
}
