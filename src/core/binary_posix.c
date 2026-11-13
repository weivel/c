#include "binary.h"

#include <dlfcn.h>
#include <string.h>

_Static_assert(sizeof(void *) == sizeof(weivel_c_binary_function), "a function's address must fit an object pointer");

weivel_c_status weivel_c_binary_open(const char *path, void **binary) {
    *binary = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    return *binary != NULL ? WEIVEL_C_STATUS_OK : WEIVEL_C_STATUS_NOT_A_LIBRARY;
}

/** @implNote Copied, not cast: ISO C defines no conversion from the object pointer dlsym answers to a function. */
weivel_c_binary_function weivel_c_binary_find(void *binary, const char *name) {
    void *address = dlsym(binary, name);
    weivel_c_binary_function function;
    memcpy(&function, &address, sizeof function);
    return function;
}

int weivel_c_binary_close(void *binary) {
    return dlclose(binary) == 0;
}
