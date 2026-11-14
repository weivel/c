#include <stdlib.h>

const char *home(const char *variable) {
    return getenv(variable);
}
