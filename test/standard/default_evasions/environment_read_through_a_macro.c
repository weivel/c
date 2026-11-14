#include <windows.h>

unsigned long home(const char *variable, char *out, unsigned long size) {
    return GetEnvironmentVariable(variable, out, size);
}
