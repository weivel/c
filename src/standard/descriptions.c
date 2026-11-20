#include <weivel/c/standard.h>

#include <weivel/description.h>

weivel_c_status weivel_c_standard_check_description(void *refusal,
                                                    const struct weivel_standard_description *description) {
    weivel_status status = weivel_standard_description_check(description, refusal);
    if (status == WEIVEL_STATUS_OK) {
        return WEIVEL_C_STATUS_OK;
    }
    return status == WEIVEL_STATUS_OUT_OF_MEMORY ? WEIVEL_C_STATUS_OUT_OF_MEMORY
                                                 : WEIVEL_C_STATUS_REFUSED_BY_THE_STANDARD;
}
