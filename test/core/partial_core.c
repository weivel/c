/**
 * @file
 * A shared library that exports the first two functions of the core and none of the rest, so a load of it must fail
 * naming the third.
 */

#if defined(_WIN32)
#define PARTIAL_CORE_API __declspec(dllexport)
#else
#define PARTIAL_CORE_API __attribute__((visibility("default")))
#endif

PARTIAL_CORE_API int weivel_core_create(void);
PARTIAL_CORE_API int weivel_core_destroy(void);

int weivel_core_create(void) {
    return 0;
}

int weivel_core_destroy(void) {
    return 0;
}
