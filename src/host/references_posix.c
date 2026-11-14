#include "references.h"

void weivel_c_references_start(weivel_c_references *references) {
    atomic_init(&references->word, 2u);
}

int weivel_c_references_take(weivel_c_references *references) {
    unsigned int seen = atomic_load(&references->word);
    while ((seen & 1u) == 0) {
        if (atomic_compare_exchange_weak(&references->word, &seen, seen + 2u)) {
            return 1;
        }
    }
    return 0;
}

int weivel_c_references_close(weivel_c_references *references) {
    return (atomic_fetch_or(&references->word, 1u) & 1u) == 0;
}

int weivel_c_references_drop(weivel_c_references *references) {
    return atomic_fetch_sub(&references->word, 2u) == 3u;
}

int weivel_c_references_held(weivel_c_references *references) {
    return atomic_load(&references->word) > 1u;
}
