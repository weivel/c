#include <stdio.h>

void spell(char *name, size_t size, const char *middle) {
    snprintf(name, size, "weivel_%s_read_library", middle);
}
