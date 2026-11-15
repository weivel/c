#include "references.h"

void weivel_c_references_start(weivel_c_references *references) {
    atomic_init(&references->word, 2u);
}

#if defined(WEIVEL_C_TESTING)
void weivel_c_references_start_holding(weivel_c_references *references, uint32_t held) {
    atomic_init(&references->word, (unsigned int)held * 2u);
}
#endif

weivel_c_reference_answer weivel_c_references_take(weivel_c_references *references) {
    unsigned int seen = atomic_load(&references->word);
    while ((seen & 1u) == 0) {
        if (seen / 2u >= WEIVEL_C_REFERENCES_MOST) {
            return WEIVEL_C_REFERENCE_FULL;
        }
        if (atomic_compare_exchange_weak(&references->word, &seen, seen + 2u)) {
            return WEIVEL_C_REFERENCE_TAKEN;
        }
    }
    return WEIVEL_C_REFERENCE_CLOSED;
}

int weivel_c_references_close(weivel_c_references *references) {
    return (atomic_fetch_or(&references->word, 1u) & 1u) == 0;
}

int weivel_c_references_drop(weivel_c_references *references) {
    return atomic_fetch_sub(&references->word, 2u) == 3u;
}

int weivel_c_references_held_beyond(weivel_c_references *references, uint32_t count) {
    return atomic_load(&references->word) / 2u > count;
}
