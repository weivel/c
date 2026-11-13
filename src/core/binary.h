#ifndef WEIVEL_C_CORE_BINARY_H
#define WEIVEL_C_CORE_BINARY_H

#include <weivel/c/core.h>

/** The one shape every function found in a binary is carried in until it is stored under its own type. */
typedef void (*weivel_c_binary_function)(void);

/**
 * Opens the shared library at `path`, UTF-8, with the platform's loader.
 * Answers WEIVEL_C_STATUS_OK with `*binary` set, WEIVEL_C_STATUS_NOT_A_LIBRARY or WEIVEL_C_STATUS_OUT_OF_MEMORY.
 */
weivel_c_status weivel_c_binary_open(const char *path, void **binary);

/** The function `binary` exports as `name`, or NULL when it exports none. */
weivel_c_binary_function weivel_c_binary_find(void *binary, const char *name);

/** Closes what weivel_c_binary_open opened. Answers 1, or 0 when the platform refused. */
int weivel_c_binary_close(void *binary);

#endif
