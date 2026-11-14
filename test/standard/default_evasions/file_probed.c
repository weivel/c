#include <io.h>

int is_there(const char *path) {
    return _access(path, 0) == 0;
}
