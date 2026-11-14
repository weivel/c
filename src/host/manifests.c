#include "host.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define END_OF(type, field) (offsetof(type, field) + sizeof(((type *)0)->field))
#define FIRST_MANIFEST_SIZE offsetof(weivel_manifest, permissions)
#define FIRST_OFFER_SIZE END_OF(weivel_service_offer, id)
#define FIRST_WANT_SIZE END_OF(weivel_service_want, degradable)
#define FIRST_NUMBER_SIZE END_OF(weivel_number, value)

/** The strictest alignment any part of a manifest's copy needs. */
typedef union widest {
    double number;
    void *pointer;
    uint64_t integer;
} widest;

/**
 * Lays a manifest's copy out in one block: with no `base` it only measures how large that block must be, with one it
 * writes into it, so the two passes take the very same room.
 */
typedef struct copier {
    char *base;
    size_t used;
} copier;

static void *take(copier *self, size_t size) {
    size_t alignment = _Alignof(widest);
    size_t at = (self->used + alignment - 1) / alignment * alignment;
    self->used = at + size;
    return self->base == NULL ? NULL : self->base + at;
}

static const char *copy_text(copier *self, const char *text) {
    size_t length;
    char *made;
    if (text == NULL) {
        return NULL;
    }
    length = strlen(text) + 1;
    made = take(self, length);
    if (made != NULL) {
        memcpy(made, text, length);
    }
    return made;
}

static weivel_strings copy_strings(copier *self, weivel_strings given) {
    weivel_strings made = {NULL, given.count};
    const char **items = given.count == 0 ? NULL : take(self, given.count * sizeof *items);
    uint32_t index;
    if (given.items == NULL) {
        return given;
    }
    for (index = 0; index < given.count; index++) {
        const char *text = copy_text(self, given.items[index]);
        if (items != NULL) {
            items[index] = text;
        }
    }
    made.items = items;
    return made;
}

/** Copies an element at `stride` into `into`, which is `size` bytes: what it does not reach is absent, so zero. */
static void read_element(void *into, size_t size, const void *from, size_t stride) {
    memset(into, 0, size);
    memcpy(into, from, stride < size ? stride : size);
}

static const void *element_at(const void *first, uint32_t index, size_t stride) {
    return (const char *)first + (size_t)index * stride;
}

static weivel_numbers copy_numbers(copier *self, weivel_numbers given) {
    weivel_numbers made = {NULL, given.count};
    weivel_number *items = given.count == 0 ? NULL : take(self, given.count * sizeof *items);
    uint32_t index;
    if (given.items == NULL) {
        return given;
    }
    for (index = 0; index < given.count; index++) {
        weivel_number number;
        read_element(&number, sizeof number, element_at(given.items, index, given.items->size), given.items->size);
        number.size = (uint32_t)sizeof number;
        number.name = copy_text(self, number.name);
        if (items != NULL) {
            items[index] = number;
        }
    }
    made.items = items;
    return made;
}

static weivel_service_offers copy_offers(copier *self, weivel_service_offers given) {
    weivel_service_offers made = {NULL, given.count};
    weivel_service_offer *items = given.count == 0 ? NULL : take(self, given.count * sizeof *items);
    uint32_t index;
    if (given.items == NULL) {
        return given;
    }
    for (index = 0; index < given.count; index++) {
        weivel_service_offer offer;
        read_element(&offer, sizeof offer, element_at(given.items, index, given.items->size), given.items->size);
        offer.size = (uint32_t)sizeof offer;
        offer.id = copy_text(self, offer.id);
        offer.attributes = copy_numbers(self, offer.attributes);
        if (items != NULL) {
            items[index] = offer;
        }
    }
    made.items = items;
    return made;
}

static weivel_service_wants copy_wants(copier *self, weivel_service_wants given) {
    weivel_service_wants made = {NULL, given.count};
    weivel_service_want *items = given.count == 0 ? NULL : take(self, given.count * sizeof *items);
    uint32_t index;
    if (given.items == NULL) {
        return given;
    }
    for (index = 0; index < given.count; index++) {
        weivel_service_want want;
        read_element(&want, sizeof want, element_at(given.items, index, given.items->size), given.items->size);
        want.size = (uint32_t)sizeof want;
        want.id = copy_text(self, want.id);
        want.provider = copy_text(self, want.provider);
        want.constraints = copy_numbers(self, want.constraints);
        if (items != NULL) {
            items[index] = want;
        }
    }
    made.items = items;
    return made;
}

static void copy_into(copier *self, const weivel_manifest *given) {
    weivel_manifest *made = take(self, sizeof *made);
    weivel_manifest copy;
    read_element(&copy, sizeof copy, given, given->size);
    copy.size = (uint32_t)sizeof copy;
    copy.import_name = copy_text(self, copy.import_name);
    copy.entry = copy_text(self, copy.entry);
    copy.identity = copy_text(self, copy.identity);
    copy.targets = copy_strings(self, copy.targets);
    copy.permissions = copy_strings(self, copy.permissions);
    copy.capabilities.provides = copy_strings(self, copy.capabilities.provides);
    copy.capabilities.consumes = copy_strings(self, copy.capabilities.consumes);
    copy.services.provides = copy_offers(self, copy.services.provides);
    copy.services.consumes = copy_wants(self, copy.services.consumes);
    if (made != NULL) {
        *made = copy;
    }
}

/** 1 when a list's elements each declare the stride its first declares, at least `first_size`; a NULL list counts. */
static int list_is_whole(const void *items, uint32_t count, size_t first_size) {
    size_t stride;
    uint32_t index;
    if (items == NULL || count == 0) {
        return 1;
    }
    stride = *(const uint32_t *)items;
    if (stride < first_size) {
        return 0;
    }
    for (index = 1; index < count; index++) {
        if (*(const uint32_t *)element_at(items, index, stride) != stride) {
            return 0;
        }
    }
    return 1;
}

static int offers_are_whole(weivel_service_offers given) {
    uint32_t index;
    if (!list_is_whole(given.items, given.count, FIRST_OFFER_SIZE)) {
        return 0;
    }
    for (index = 0; given.items != NULL && index < given.count; index++) {
        weivel_service_offer offer;
        read_element(&offer, sizeof offer, element_at(given.items, index, given.items->size), given.items->size);
        if (!list_is_whole(offer.attributes.items, offer.attributes.count, FIRST_NUMBER_SIZE)) {
            return 0;
        }
    }
    return 1;
}

static int wants_are_whole(weivel_service_wants given) {
    uint32_t index;
    if (!list_is_whole(given.items, given.count, FIRST_WANT_SIZE)) {
        return 0;
    }
    for (index = 0; given.items != NULL && index < given.count; index++) {
        weivel_service_want want;
        read_element(&want, sizeof want, element_at(given.items, index, given.items->size), given.items->size);
        if (!list_is_whole(want.constraints.items, want.constraints.count, FIRST_NUMBER_SIZE)) {
            return 0;
        }
    }
    return 1;
}

/**
 * @implNote The first shapes are the core's: each ends where the core's header says the struct's appended fields
 * begin, and a number has none appended.
 */
int weivel_c_host_manifest_is_whole(const weivel_manifest *given) {
    weivel_manifest read;
    if (given->size < FIRST_MANIFEST_SIZE) {
        return 0;
    }
    read_element(&read, sizeof read, given, given->size);
    return offers_are_whole(read.services.provides) && wants_are_whole(read.services.consumes);
}

/**
 * @implNote Every array the caller built is read at the stride its first element declares, and laid out in the copy
 * at this layer's own, each element's `size` saying so; a field beyond what the caller's `size` reaches is absent in
 * the copy too. A list whose `items` is NULL with a count is the core's to call malformed, so it is copied as given.
 */
weivel_manifest *weivel_c_host_copy_manifest(const weivel_manifest *given) {
    copier measuring = {NULL, 0};
    copier writing = {NULL, 0};
    copy_into(&measuring, given);
    writing.base = malloc(measuring.used);
    if (writing.base == NULL) {
        return NULL;
    }
    copy_into(&writing, given);
    return (weivel_manifest *)(void *)writing.base;
}
