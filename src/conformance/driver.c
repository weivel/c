#include <weivel/c/conformance.h>

#include <stdlib.h>
#include <string.h>

#include "ops.h"

/** What a witness keeps. */
typedef struct witness_storage {
    char *observed;
    char *expected;
    char problem[CONFORMANCE_PROBLEM];
} witness_storage;

typedef struct conformance_op {
    const char *name;
    conformance_drive drive;
    void (*core_options)(const conformance_vector *vector, weivel_core_options *options, const char **well_known);
} conformance_op;

static const conformance_op OPS[] = {{"consume", conformance_drive_consume, NULL},
                                     {"want", conformance_drive_want, conformance_want_core_options},
                                     {"lifecycle", conformance_drive_lifecycle, NULL},
                                     {"library", conformance_drive_library, NULL}};

static const conformance_op *op_named(const char *name) {
    uint32_t index;
    for (index = 0; index < sizeof OPS / sizeof OPS[0]; index++) {
        if (strcmp(OPS[index].name, name) == 0) {
            return &OPS[index];
        }
    }
    return NULL;
}

/** Whether the observed text reads back, by the standard's reader, as a value equal to `then`. */
static int agrees_with(const char *observed, const weivel_json_value *then) {
    weivel_json_error error = WEIVEL_INIT(weivel_json_error);
    weivel_json_value *read = observed == NULL ? NULL : weivel_json_parse(observed, strlen(observed), &error);
    int agrees = read != NULL && conformance_json_equal(read, then);
    weivel_json_free(read);
    return agrees;
}

static int drive_vector(const weivel_c_conformance_options *options, const conformance_vector *vector,
                        conformance_world *world, conformance_text *observed, char *problem) {
    const conformance_op *op = op_named(vector->op);
    weivel_core_options core_options = WEIVEL_INIT(weivel_core_options);
    const char *well_known = NULL;
    if (op == NULL) {
        memset(world, 0, sizeof *world);
        world->problem = problem;
        return conformance_world_fail(world, "the vector has an op this driver does not drive", vector->op);
    }
    if (op->core_options != NULL) {
        op->core_options(vector, &core_options, &well_known);
    }
    if (!conformance_world_begin(world, options->core_path, &core_options, problem)) {
        return 0;
    }
    world->library_path = options->library_path;
    if (!op->drive(world, vector, observed)) {
        conformance_world_end(world);
        return 0;
    }
    return !world->has_problem;
}

static void fill(weivel_c_conformance_witness *witness, witness_storage *storage, const conformance_world *world,
                 int driven) {
    witness->storage = storage;
    witness->observed = storage->observed;
    witness->expected = storage->expected;
    witness->problem = driven ? NULL : storage->problem;
    witness->sink_failures = world->sink_failures_of_the_run;
    witness->listener_failures = world->listener_failures_of_the_run;
    witness->error_lines = world->error_lines_of_the_run;
}

static weivel_c_status witness_read(const weivel_c_conformance_options *options, const conformance_vector *vector,
                                    witness_storage *storage, weivel_c_conformance_witness *witness) {
    conformance_world world;
    conformance_text observed;
    conformance_text expected;
    int driven;
    conformance_text_start(&observed);
    conformance_text_start(&expected);
    driven = drive_vector(options, vector, &world, &observed, storage->problem);
    conformance_text_append_json(&expected, vector->then);
    storage->observed = conformance_text_take(&observed);
    storage->expected = conformance_text_take(&expected);
    fill(witness, storage, &world, driven);
    witness->agrees = driven && agrees_with(storage->observed, vector->then);
    if (storage->expected == NULL || (driven && storage->observed == NULL)) {
        weivel_c_conformance_witness_release(witness);
        return WEIVEL_C_STATUS_OUT_OF_MEMORY;
    }
    return driven ? WEIVEL_C_STATUS_OK : WEIVEL_C_STATUS_FAILED;
}

weivel_c_status weivel_c_conformance_witness_vector(const weivel_c_conformance_options *options, const char *id,
                                                    weivel_c_conformance_witness *witness) {
    conformance_vector vector;
    witness_storage *storage;
    weivel_c_status status;
    if (options == NULL || id == NULL || witness == NULL || options->size < sizeof *options ||
        witness->size < sizeof *witness || options->core_path == NULL || options->vector_directory == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    storage = calloc(1, sizeof *storage);
    if (storage == NULL) {
        return WEIVEL_C_STATUS_OUT_OF_MEMORY;
    }
    if (!conformance_read_vector(options->vector_directory, id, &vector, storage->problem)) {
        witness->storage = storage;
        witness->problem = storage->problem;
        return WEIVEL_C_STATUS_FAILED;
    }
    status = witness_read(options, &vector, storage, witness);
    conformance_vector_end(&vector);
    return status;
}

weivel_c_status weivel_c_conformance_witness_release(weivel_c_conformance_witness *witness) {
    witness_storage *storage;
    if (witness == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    storage = witness->storage;
    if (storage != NULL) {
        free(storage->observed);
        free(storage->expected);
        free(storage);
    }
    memset((char *)witness + sizeof witness->size, 0, sizeof *witness - sizeof witness->size);
    return WEIVEL_C_STATUS_OK;
}
