#ifndef WEIVEL_C_HANDLES_TABLE_H
#define WEIVEL_C_HANDLES_TABLE_H

/**
 * @file
 * What the layer's other sources and its tests reach of the `handles` table beyond its public calls. None of it is
 * exported from the shared library.
 */

#include <weivel/c/handles.h>

#include "../core/wrapper.h"

/**
 * Replaces the allocator a table grows with: the seam a test fails an allocation through. The table must be empty
 * and never grown.
 */
void weivel_c_handles_replace_allocator(weivel_c_handles *handles, const weivel_c_allocator *allocator);

#endif
