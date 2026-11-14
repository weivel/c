#ifndef WEIVEL_C_HOST_REFERENCES_H
#define WEIVEL_C_HOST_REFERENCES_H

/**
 * @file
 * The references held on a loaded C library, counted without a lock so that a call through one of its services takes
 * none: one for the host until it unloads the library, one for each call running in it, and one for each body it
 * answered that is still held. Once the library is closed to calls no reference is taken any more, so the count only
 * falls, and the one drop that leaves none is the one that closes the binary.
 *
 * @implNote One word holds both facts, twice the count and the lowest bit set once closed, so that taking a reference
 * and finding the library closed are one atomic step: a reference is never taken on a library whose close has begun.
 */

#include <stdint.h>

#if defined(_WIN32)

/** @implNote A Windows LONG, which the Interlocked functions take, so this header needs none of the platform's. */
typedef struct weivel_c_references {
    volatile long word;
} weivel_c_references;

#else

#include <stdatomic.h>

typedef struct weivel_c_references {
    atomic_uint word;
} weivel_c_references;

#endif

/** Starts the count with one reference, the host's, and open to calls. */
void weivel_c_references_start(weivel_c_references *references);

/** Takes a reference unless the library is closed to calls. Answers 1 when it took one. */
int weivel_c_references_take(weivel_c_references *references);

/** Closes the library to calls. Answers 1 when this call closed it, 0 when it was closed already. */
int weivel_c_references_close(weivel_c_references *references);

/** Drops a reference. Answers 1 when it was the last one of a library closed to calls, so its binary may close. */
int weivel_c_references_drop(weivel_c_references *references);

/** Answers 1 while any reference is held. */
int weivel_c_references_held(weivel_c_references *references);

#endif
