#include <windows.h>

void *open_it(const char *path) {
    return CreateFile(path, 0, 0, 0, 0, 0, 0);
}
