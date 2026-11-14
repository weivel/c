#include <weivel/c/standard.h>

weivel_c_status weivel_c_standard_status(uint32_t status) {
    switch (status) {
    case WEIVEL_STATUS_OK:
        return WEIVEL_C_STATUS_OK;
    case WEIVEL_STATUS_NONE:
        return WEIVEL_C_STATUS_NONE;
    case WEIVEL_STATUS_REFUSED:
        return WEIVEL_C_STATUS_REFUSED_BY_THE_STANDARD;
    case WEIVEL_STATUS_MALFORMED:
        return WEIVEL_C_STATUS_MISUSE;
    case WEIVEL_STATUS_OUT_OF_MEMORY:
        return WEIVEL_C_STATUS_OUT_OF_MEMORY;
    default:
        return WEIVEL_C_STATUS_FAILED;
    }
}
