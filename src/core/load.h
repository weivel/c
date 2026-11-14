#ifndef WEIVEL_C_CORE_LOAD_H
#define WEIVEL_C_CORE_LOAD_H

#include <weivel/c/core.h>

/**
 * 1 when `table` is at least as large as the first version of the table, so it holds every function of that version.
 *
 * @implNote The loader is the one source that names the table's fields by their layout, so the wrapper asks it rather
 * than measure the table itself.
 */
int weivel_c_core_table_is_large_enough(const weivel_c_core_table *table);

#endif
