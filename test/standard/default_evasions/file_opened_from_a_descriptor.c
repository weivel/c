#include <stdio.h>

FILE *open_it(int descriptor, const char *mode) {
    return fdopen(descriptor, mode);
}
