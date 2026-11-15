#include "references.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

void weivel_c_references_start(weivel_c_references *references) {
    InterlockedExchange(&references->word, 2);
}

#if defined(WEIVEL_C_TESTING)
void weivel_c_references_start_holding(weivel_c_references *references, uint32_t held) {
    InterlockedExchange(&references->word, (LONG)held * 2);
}
#endif

weivel_c_reference_answer weivel_c_references_take(weivel_c_references *references) {
    LONG seen = InterlockedOr(&references->word, 0);
    while ((seen & 1) == 0) {
        LONG found;
        if ((uint32_t)seen / 2u >= WEIVEL_C_REFERENCES_MOST) {
            return WEIVEL_C_REFERENCE_FULL;
        }
        found = InterlockedCompareExchange(&references->word, seen + 2, seen);
        if (found == seen) {
            return WEIVEL_C_REFERENCE_TAKEN;
        }
        seen = found;
    }
    return WEIVEL_C_REFERENCE_CLOSED;
}

int weivel_c_references_close(weivel_c_references *references) {
    return (InterlockedOr(&references->word, 1) & 1) == 0;
}

int weivel_c_references_drop(weivel_c_references *references) {
    return InterlockedExchangeAdd(&references->word, -2) == 3;
}

int weivel_c_references_held_beyond(weivel_c_references *references, uint32_t count) {
    return (uint32_t)InterlockedOr(&references->word, 0) / 2u > count;
}
