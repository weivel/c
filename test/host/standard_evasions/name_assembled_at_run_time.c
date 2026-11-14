#include <stdio.h>

void spell(char *name, size_t size) {
    const char *start = "weivel_json_";
    snprintf(name, size, "%s%s", start, "parse");
}
