#ifndef WEIVEL_C_CONFORMANCE_H
#define WEIVEL_C_CONFORMANCE_H

/**
 * @file
 * The `conformance` module of the weivel C language implementation: the driver for the layer suite. It reads a layer
 * vector of the kit, builds the vector's plugins as manifest values and entries, registers them through the host's
 * public surface on the real core, writes no file, drives the vector's steps, and answers what the run showed in the
 * shape of the vector's own `then`, so the two compare as values. It is a library of its own,
 * weivel_c_conformance: it reads each vector with the standard's JSON reader, the one place a JSON document enters
 * C, so it links the standard and the host library never does.
 *
 * Nothing here chooses a path: the core's binary and the kit's vector directory are its caller's.
 */

#include <stdint.h>

#include <weivel/c/core.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Where the driver finds what it runs on. */
typedef struct weivel_c_conformance_options {
    uint32_t size;
    /** The core's shared binary, as weivel_c_core_load takes it. Required. */
    const char *core_path;
    /** The kit's layer vector directory, `vectors/layer` of the kit commit the language pins. Required. */
    const char *vector_directory;
} weivel_c_conformance_options;

/** What the driver saw of one vector. Everything in it lives until weivel_c_conformance_witness_release. */
typedef struct weivel_c_conformance_witness {
    uint32_t size;
    /** 1 when the run showed every field of the vector's `then` exactly as it states it, else 0. */
    uint32_t agrees;
    /** What the run showed, as JSON in the shape of `then`: an `{ "equals": ... }` field answered so. */
    const char *observed;
    /** The vector's `then`, as JSON. */
    const char *expected;
    /** Why the vector could not be driven, a field, an op or a step the driver does not know among them; else NULL. */
    const char *problem;
    /**
     * How many failures reached the host's error sink during the run, and how many of those a listener's: up to the
     * moment the driver destroys the host after reading the run, as the vector's own fields are read.
     */
    uint32_t sink_failures;
    uint32_t listener_failures;
    /** How many lines of level WEIVEL_LOG_ERROR reached the host's log during the run. */
    uint32_t error_lines;
    void *storage;
} weivel_c_conformance_witness;

/**
 * Witnesses the vector `id` of the kit: reads `<vector_directory>/<id>.json`, refuses one that names itself another
 * id, and drives it on a host made for it alone, on a clock and a scheduler the driver owns.
 *
 * @param witness an empty witness, as WEIVEL_INIT leaves it; filled on OK and on WEIVEL_C_STATUS_FAILED.
 * @return WEIVEL_C_STATUS_OK once the vector was driven, whether or not the run agrees; WEIVEL_C_STATUS_FAILED with
 * `problem` set when it could not be; WEIVEL_C_STATUS_MISUSE for a NULL argument or options smaller than their
 * shape; WEIVEL_C_STATUS_OUT_OF_MEMORY.
 */
WEIVEL_C_API weivel_c_status weivel_c_conformance_witness_vector(const weivel_c_conformance_options *options,
                                                                 const char *id,
                                                                 weivel_c_conformance_witness *witness);

/** Frees what a witness holds and leaves it empty. Answers WEIVEL_C_STATUS_OK, or MISUSE for NULL. */
WEIVEL_C_API weivel_c_status weivel_c_conformance_witness_release(weivel_c_conformance_witness *witness);

/** The ids of the layer vectors under one directory, in the order of their paths. */
typedef struct weivel_c_conformance_ids {
    uint32_t size;
    const char *const *items;
    uint32_t count;
    /** Why the directory could not be read, or a vector there names itself another id; else NULL. */
    const char *problem;
    void *storage;
} weivel_c_conformance_ids;

/**
 * Lists every layer vector under `directory`, at any depth: each `.json` file, its id its path there without
 * `.json`, with `/` between directories, read with the standard's reader so that a vector naming itself another id is
 * refused.
 *
 * @param ids an empty list, as WEIVEL_INIT leaves it; filled on OK and on WEIVEL_C_STATUS_FAILED.
 * @return WEIVEL_C_STATUS_OK; WEIVEL_C_STATUS_FAILED with `problem` set; WEIVEL_C_STATUS_MISUSE;
 * WEIVEL_C_STATUS_OUT_OF_MEMORY.
 */
WEIVEL_C_API weivel_c_status weivel_c_conformance_vector_ids(const char *directory, weivel_c_conformance_ids *ids);

/** Frees what a list holds and leaves it empty. Answers WEIVEL_C_STATUS_OK, or MISUSE for NULL. */
WEIVEL_C_API weivel_c_status weivel_c_conformance_ids_release(weivel_c_conformance_ids *ids);

#ifdef __cplusplus
}
#endif

#endif
