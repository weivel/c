/**
 * @file
 * The strong check beside the kit's `layers`: every layer vector of the pinned kit, read from its directory at run
 * time, is named by a test of the suite's table. A probe directory under the build shows the check finds a vector no
 * test names however deep it sits, and refuses a vector whose id is not its path.
 *
 * @implNote MSVC calls the standard `fopen` unsafe unless told not to before any header is read.
 */

#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <string.h>

#include <weivel/c/conformance.h>

#include "check.h"
#include "layer_vectors.h"

static const char *const PROBED_ID = "nested/deeper/a-vector-no-test-names";

static int is_named_by_a_test(const char *id) {
    uint32_t index;
    for (index = 0; index < LAYER_VECTOR_TEST_COUNT; index++) {
        if (strcmp(LAYER_VECTOR_TESTS[index].id, id) == 0) {
            return 1;
        }
    }
    return 0;
}

/** How many vectors under `directory` no test names, each printed, or -1 when the directory cannot be listed. */
static int unnamed_vectors(const char *directory, const char *only_unnamed) {
    weivel_c_conformance_ids ids = WEIVEL_INIT(weivel_c_conformance_ids);
    int unnamed = 0;
    uint32_t index;
    if (weivel_c_conformance_vector_ids(directory, &ids) != WEIVEL_C_STATUS_OK) {
        fprintf(stderr, "the vectors under %s cannot be listed: %s\n", directory,
                ids.problem == NULL ? "" : ids.problem);
        weivel_c_conformance_ids_release(&ids);
        return -1;
    }
    for (index = 0; index < ids.count; index++) {
        if (!is_named_by_a_test(ids.items[index])) {
            fprintf(stderr, "no test of the suite names the layer vector %s\n", ids.items[index]);
            unnamed += only_unnamed == NULL || strcmp(ids.items[index], only_unnamed) == 0 ? 1 : 100;
        }
    }
    CHECK(ids.count > 0);
    weivel_c_conformance_ids_release(&ids);
    return unnamed;
}

static int write_vector(const char *probe, const char *relative, const char *id) {
    char path[4096];
    FILE *file;
    snprintf(path, sizeof path, "%s/%s.json", probe, relative);
    file = fopen(path, "wb");
    if (file == NULL) {
        return 0;
    }
    fprintf(file, "{\"id\": \"%s\", \"op\": \"want\", \"given\": {}, \"then\": {}}\n", id);
    return fclose(file) == 0;
}

static void every_vector_of_the_pinned_kit_is_named_by_a_test(const char *kit) {
    CHECK(unnamed_vectors(kit, NULL) == 0);
}

static void a_vector_no_test_names_is_found_however_deep_it_sits(const char *probe) {
    CHECK(write_vector(probe, PROBED_ID, PROBED_ID));
    CHECK(unnamed_vectors(probe, PROBED_ID) == 1);
}

static void a_vector_whose_id_is_not_its_path_is_refused(const char *probe) {
    weivel_c_conformance_ids ids = WEIVEL_INIT(weivel_c_conformance_ids);
    char path[4096];
    CHECK(write_vector(probe, "nested/deeper/misnamed", "elsewhere"));
    CHECK(weivel_c_conformance_vector_ids(probe, &ids) == WEIVEL_C_STATUS_FAILED);
    CHECK(ids.problem != NULL && strstr(ids.problem, "names itself elsewhere") != NULL);
    weivel_c_conformance_ids_release(&ids);
    snprintf(path, sizeof path, "%s/nested/deeper/misnamed.json", probe);
    CHECK(remove(path) == 0);
}

int main(int argument_count, char **arguments) {
    if (argument_count != 3) {
        fprintf(stderr, "usage: conformance_coverage_test <layer-vector-directory> <probe-directory>\n");
        return 2;
    }
    every_vector_of_the_pinned_kit_is_named_by_a_test(arguments[1]);
    a_vector_no_test_names_is_found_however_deep_it_sits(arguments[2]);
    a_vector_whose_id_is_not_its_path_is_refused(arguments[2]);
    return check_summary("conformance coverage");
}
