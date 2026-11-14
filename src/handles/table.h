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

/**
 * Where the search for a key begins, before it is cut to the size of the table. `space` says which of the table's
 * kinds of key it is, an entry's, a holding's or an owner's, and the two halves of the key follow.
 */
typedef uint64_t (*weivel_c_handles_hash)(uint32_t space, uint64_t first_key, uint64_t second_key);

/**
 * Replaces the hash a table places its keys by: the seam a test makes every key collide through, so that nothing but
 * the comparison of whole keys tells two of them apart. The table must be empty and never grown.
 */
void weivel_c_handles_replace_hash(weivel_c_handles *handles, weivel_c_handles_hash hash);

/** Answers how many reservations wait to be used or given back: what a test reads to find one that leaked. */
uint32_t weivel_c_handles_reserved(weivel_c_handles *handles);

#endif
