/**
 * @file
 * The count of references on a loaded C library: a reference is taken while the library is open to calls and holds
 * fewer than the most, refused as full at the most rather than overflowing the word, refused once closed, and the one
 * drop that leaves none of a closed library is the one that answers it may close.
 */

#include "check.h"

#include "../../src/host/references.h"

static void a_reference_is_taken_until_the_count_holds_the_most(void) {
    weivel_c_references references;
    weivel_c_references_start_holding(&references, WEIVEL_C_REFERENCES_MOST - 1);
    CHECK(weivel_c_references_take(&references) == WEIVEL_C_REFERENCE_TAKEN);
    CHECK(weivel_c_references_take(&references) == WEIVEL_C_REFERENCE_FULL);
    CHECK(weivel_c_references_take(&references) == WEIVEL_C_REFERENCE_FULL);
    CHECK(weivel_c_references_drop(&references) == 0);
    CHECK(weivel_c_references_take(&references) == WEIVEL_C_REFERENCE_TAKEN);
    CHECK(weivel_c_references_close(&references) == 1);
    CHECK(weivel_c_references_take(&references) == WEIVEL_C_REFERENCE_CLOSED);
    CHECK(weivel_c_references_held(&references));
}

static void the_last_drop_of_a_closed_library_answers_it_may_close(void) {
    weivel_c_references references;
    weivel_c_references_start(&references);
    CHECK(weivel_c_references_take(&references) == WEIVEL_C_REFERENCE_TAKEN);
    CHECK(weivel_c_references_close(&references) == 1 && weivel_c_references_close(&references) == 0);
    CHECK(weivel_c_references_take(&references) == WEIVEL_C_REFERENCE_CLOSED);
    CHECK(weivel_c_references_drop(&references) == 0);
    CHECK(weivel_c_references_drop(&references) == 1);
    CHECK(!weivel_c_references_held(&references));
}

int main(void) {
    a_reference_is_taken_until_the_count_holds_the_most();
    the_last_drop_of_a_closed_library_answers_it_may_close();
    return check_summary("host references");
}
