#include "references.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

void weivel_c_references_start(weivel_c_references *references) {
    InterlockedExchange(&references->word, 2);
}

int weivel_c_references_take(weivel_c_references *references) {
    LONG seen = InterlockedOr(&references->word, 0);
    while ((seen & 1) == 0) {
        LONG found = InterlockedCompareExchange(&references->word, seen + 2, seen);
        if (found == seen) {
            return 1;
        }
        seen = found;
    }
    return 0;
}

int weivel_c_references_close(weivel_c_references *references) {
    return (InterlockedOr(&references->word, 1) & 1) == 0;
}

int weivel_c_references_drop(weivel_c_references *references) {
    return InterlockedExchangeAdd(&references->word, -2) == 3;
}

int weivel_c_references_held(weivel_c_references *references) {
    return InterlockedOr(&references->word, 0) > 1;
}
