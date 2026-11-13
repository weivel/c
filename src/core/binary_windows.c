#include "binary.h"

#include <stdlib.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static weivel_c_status to_wide_path(const char *path, wchar_t **wide) {
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    if (length <= 0) {
        return WEIVEL_C_STATUS_NOT_A_LIBRARY;
    }
    *wide = malloc((size_t)length * sizeof **wide);
    if (*wide == NULL) {
        return WEIVEL_C_STATUS_OUT_OF_MEMORY;
    }
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, *wide, length);
    return WEIVEL_C_STATUS_OK;
}

/** @implNote The error mode is narrowed for the call: Windows otherwise answers a damaged image with a dialog. */
static HMODULE load_without_a_dialog(const wchar_t *path) {
    DWORD previous_mode = 0;
    HMODULE module;
    SetThreadErrorMode(SEM_FAILCRITICALERRORS, &previous_mode);
    module = LoadLibraryW(path);
    SetThreadErrorMode(previous_mode, NULL);
    return module;
}

weivel_c_status weivel_c_binary_open(const char *path, void **binary) {
    wchar_t *wide = NULL;
    weivel_c_status status = to_wide_path(path, &wide);
    if (status != WEIVEL_C_STATUS_OK) {
        return status;
    }
    *binary = load_without_a_dialog(wide);
    free(wide);
    return *binary != NULL ? WEIVEL_C_STATUS_OK : WEIVEL_C_STATUS_NOT_A_LIBRARY;
}

weivel_c_binary_function weivel_c_binary_find(void *binary, const char *name) {
    return (weivel_c_binary_function)GetProcAddress(binary, name);
}

int weivel_c_binary_close(void *binary) {
    return FreeLibrary(binary) != 0;
}
