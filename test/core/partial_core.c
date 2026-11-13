/**
 * @file
 * A shared library that exports the first two functions of the core and none of the rest, so a load of it must fail
 * naming the third. On Windows it also notes, in the process environment, the error mode of the thread loading it.
 */

#if defined(_WIN32)
#define PARTIAL_CORE_API __declspec(dllexport)
#else
#define PARTIAL_CORE_API __attribute__((visibility("default")))
#endif

PARTIAL_CORE_API int weivel_core_create(void);
PARTIAL_CORE_API int weivel_core_destroy(void);

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <stdio.h>
#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE library, DWORD reason, LPVOID reserved) {
    char mode[16];
    (void)library;
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        snprintf(mode, sizeof mode, "%lu", (unsigned long)GetThreadErrorMode());
        SetEnvironmentVariableA("PARTIAL_CORE_ERROR_MODE_AT_LOAD", mode);
    }
    return TRUE;
}
#endif

int weivel_core_create(void) {
    return 0;
}

int weivel_core_destroy(void) {
    return 0;
}
