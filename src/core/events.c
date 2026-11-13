#include "events.h"

#include <string.h>

#define END_OF(type, field) (offsetof(type, field) + sizeof(((type *)0)->field))
#define FIRST_NUMBER_SIZE END_OF(weivel_number, value)
#define FIRST_REFUSAL_SIZE END_OF(weivel_c_refusal, storage)
#define MAXIMUM_TEXTS 4

_Static_assert(sizeof(((weivel_c_event *)0)->data) == sizeof(((weivel_event *)0)->data),
               "the layer's payload must be the core's, byte for byte");

/** What a refusal's storage begins with, so a release needs no core. The texts follow it. */
typedef struct refusal_storage {
    weivel_c_allocator allocator;
} refusal_storage;

typedef struct refusal_parts {
    uint32_t code;
    const char *detail;
    const char *fix;
    const char *subject;
    uint32_t has_fix;
} refusal_parts;

static const char TEXT_NOT_COPIED[] = WEIVEL_C_TEXT_NOT_COPIED;
static const char EMPTY_TEXT[] = "";

static int is_known(uint32_t kind) {
    return kind >= WEIVEL_EVENT_LOG && kind <= WEIVEL_EVENT_DEADLINE;
}

static size_t text_size(const char *text) {
    return text == NULL ? 0 : strlen(text) + 1;
}

static const char *keep_text(char **cursor, const char *text) {
    size_t size = text_size(text);
    char *kept = *cursor;
    if (text == NULL) {
        return NULL;
    }
    memcpy(kept, text, size);
    *cursor += size;
    return kept;
}

/** What a text reads as when there is no memory to copy it: nothing is still nothing, and empty is still empty. */
static const char *stand_in_for(const char *text) {
    if (text == NULL) {
        return NULL;
    }
    return text[0] == '\0' ? EMPTY_TEXT : TEXT_NOT_COPIED;
}

static uint32_t texts_of(weivel_c_event *event, const char **texts[MAXIMUM_TEXTS]) {
    switch (event->kind) {
    case WEIVEL_EVENT_LOG:
        texts[0] = &event->data.log.text;
        return 1;
    case WEIVEL_EVENT_REFUSAL:
        texts[0] = &event->data.refused.refusal.detail;
        texts[1] = &event->data.refused.refusal.fix;
        texts[2] = &event->data.refused.subject;
        return 3;
    case WEIVEL_EVENT_WANT_SETTLED:
        texts[0] = &event->data.want.refusal.detail;
        texts[1] = &event->data.want.refusal.fix;
        texts[2] = &event->data.want.subject;
        return 3;
    case WEIVEL_EVENT_REACH_REFUSED:
        texts[0] = &event->data.reach.id;
        texts[1] = &event->data.reach.refusal.detail;
        texts[2] = &event->data.reach.refusal.fix;
        texts[3] = &event->data.reach.subject;
        return 4;
    default:
        return 0;
    }
}

static weivel_numbers *numbers_of(weivel_c_event *event) {
    return event->kind == WEIVEL_EVENT_CAPACITY_WANTED ? &event->data.capacity.constraints : NULL;
}

static int numbers_are_readable(const weivel_numbers *numbers) {
    return numbers == NULL || numbers->count == 0 ||
           (numbers->items != NULL && numbers->items[0].size >= FIRST_NUMBER_SIZE);
}

/** The element at `index`, read at the stride the first element declares and only as far as both sides know it. */
static weivel_number number_at(const weivel_numbers *numbers, uint32_t index) {
    weivel_number number;
    size_t stride = numbers->items[0].size;
    size_t known = stride < sizeof number ? stride : sizeof number;
    memset(&number, 0, sizeof number);
    memcpy(&number, (const char *)numbers->items + index * stride, known);
    number.size = (uint32_t)sizeof number;
    return number;
}

static size_t names_size(const weivel_numbers *numbers) {
    size_t size = 0;
    uint32_t index;
    for (index = 0; numbers != NULL && index < numbers->count; index++) {
        size += text_size(number_at(numbers, index).name);
    }
    return size;
}

static size_t texts_size(weivel_c_event *event) {
    const char **texts[MAXIMUM_TEXTS];
    uint32_t count = texts_of(event, texts);
    size_t size = 0;
    uint32_t index;
    for (index = 0; index < count; index++) {
        size += text_size(*texts[index]);
    }
    return size;
}

static void keep_texts(weivel_c_event *event, char **cursor) {
    const char **texts[MAXIMUM_TEXTS];
    uint32_t count = texts_of(event, texts);
    uint32_t index;
    for (index = 0; index < count; index++) {
        *texts[index] = keep_text(cursor, *texts[index]);
    }
}

static void keep_numbers(weivel_numbers *numbers, weivel_number *kept, char **cursor) {
    uint32_t index;
    for (index = 0; index < numbers->count; index++) {
        kept[index] = number_at(numbers, index);
        kept[index].name = keep_text(cursor, kept[index].name);
    }
    numbers->items = numbers->count == 0 ? NULL : kept;
}

static weivel_c_event read_event(const weivel_event *source, uint64_t sequence) {
    weivel_c_event event;
    memset(&event, 0, sizeof event);
    event.size = (uint32_t)sizeof event;
    event.kind = source->kind;
    event.sequence = sequence;
    memcpy(&event.data, &source->data, sizeof event.data);
    return event;
}

static void replace_texts(weivel_c_event *event) {
    const char **texts[MAXIMUM_TEXTS];
    uint32_t count = texts_of(event, texts);
    uint32_t index;
    for (index = 0; index < count; index++) {
        *texts[index] = stand_in_for(*texts[index]);
    }
}

static size_t beyond_the_fixed_part(weivel_c_event *event) {
    weivel_numbers *numbers = numbers_of(event);
    size_t numbers_bytes = numbers == NULL ? 0 : numbers->count * sizeof(weivel_number);
    return numbers_bytes + names_size(numbers) + texts_size(event);
}

static weivel_c_queued_event *allocate_block(const weivel_c_allocator *allocator, const weivel_c_event *event,
                                             size_t beyond) {
    weivel_c_queued_event *block = allocator->allocate(allocator->data, sizeof *block + beyond);
    if (block != NULL) {
        memset(&block->place, 0, sizeof block->place);
        block->event = *event;
    }
    return block;
}

static void keep_what_lies_beyond(weivel_c_queued_event *block) {
    weivel_numbers *numbers = numbers_of(&block->event);
    size_t numbers_bytes = numbers == NULL ? 0 : numbers->count * sizeof(weivel_number);
    char *cursor = (char *)(block + 1) + numbers_bytes;
    if (numbers != NULL) {
        keep_numbers(numbers, (weivel_number *)(void *)(block + 1), &cursor);
    }
    keep_texts(&block->event, &cursor);
}

static void leave_out_what_lies_beyond(weivel_c_queued_event *block) {
    weivel_numbers *numbers = numbers_of(&block->event);
    if (numbers != NULL) {
        numbers->items = NULL;
        numbers->count = 0;
    }
    replace_texts(&block->event);
    block->event.incomplete = 1;
}

/**
 * @implNote An event with nothing beyond its fixed part is never tried a second time: the second allocation would be
 * the first one again.
 */
weivel_c_copied weivel_c_event_copy(const weivel_c_allocator *allocator, const weivel_event *source,
                                    uint64_t sequence, weivel_c_queued_event **copy) {
    weivel_c_event event;
    int readable;
    size_t beyond;
    *copy = NULL;
    if (!is_known(source->kind)) {
        return WEIVEL_C_COPY_SKIPPED;
    }
    event = read_event(source, sequence);
    readable = numbers_are_readable(numbers_of(&event));
    beyond = readable ? beyond_the_fixed_part(&event) : 0;
    if (readable) {
        *copy = allocate_block(allocator, &event, beyond);
    }
    if (*copy != NULL) {
        keep_what_lies_beyond(*copy);
        return WEIVEL_C_COPIED;
    }
    if (readable && beyond == 0) {
        return WEIVEL_C_COPY_FAILED;
    }
    *copy = allocate_block(allocator, &event, 0);
    if (*copy == NULL) {
        return WEIVEL_C_COPY_FAILED;
    }
    leave_out_what_lies_beyond(*copy);
    return WEIVEL_C_COPIED_INCOMPLETE;
}

static int parts_of(const weivel_c_event *event, refusal_parts *parts) {
    if (event->kind == WEIVEL_EVENT_REFUSAL) {
        const weivel_refusal_event *refused = &event->data.refused;
        *parts = (refusal_parts){refused->refusal.code, refused->refusal.detail, refused->refusal.fix,
                                 refused->subject, refused->has_fix};
        return 1;
    }
    if (event->kind == WEIVEL_EVENT_REACH_REFUSED) {
        const weivel_reach_refusal *reach = &event->data.reach;
        *parts = (refusal_parts){reach->refusal.code, reach->refusal.detail, reach->refusal.fix, reach->subject,
                                 reach->has_fix};
        return 1;
    }
    return 0;
}

int weivel_c_event_is_refusal(const weivel_c_event *event) {
    refusal_parts parts;
    return parts_of(event, &parts);
}

const char *weivel_c_event_refusal_detail(const weivel_c_event *event) {
    refusal_parts parts;
    return parts_of(event, &parts) ? parts.detail : NULL;
}

int weivel_c_refusal_is_ready(const weivel_c_refusal *refusal) {
    return refusal == NULL || (refusal->size >= FIRST_REFUSAL_SIZE && refusal->storage == NULL &&
                               refusal->code == WEIVEL_REFUSAL_NONE && refusal->detail == NULL);
}

int weivel_c_refusal_copy(const weivel_c_allocator *allocator, const weivel_c_event *event,
                          weivel_c_refusal *refusal) {
    refusal_parts parts;
    refusal_storage *storage;
    char *cursor;
    if (!parts_of(event, &parts)) {
        return 1;
    }
    storage = allocator->allocate(allocator->data, sizeof *storage + text_size(parts.detail) + text_size(parts.fix) +
                                                       text_size(parts.subject));
    refusal->code = parts.code;
    refusal->has_fix = parts.has_fix;
    if (storage == NULL) {
        refusal->detail = stand_in_for(parts.detail);
        refusal->fix = stand_in_for(parts.fix);
        refusal->subject = stand_in_for(parts.subject);
        return 0;
    }
    storage->allocator = *allocator;
    cursor = (char *)(storage + 1);
    refusal->detail = keep_text(&cursor, parts.detail);
    refusal->fix = keep_text(&cursor, parts.fix);
    refusal->subject = keep_text(&cursor, parts.subject);
    refusal->storage = storage;
    return 1;
}

weivel_c_status weivel_c_refusal_release(weivel_c_refusal *refusal) {
    refusal_storage *storage;
    if (refusal == NULL || refusal->size < FIRST_REFUSAL_SIZE) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    storage = refusal->storage;
    if (storage != NULL) {
        storage->allocator.release(storage->allocator.data, storage);
    }
    refusal->code = WEIVEL_REFUSAL_NONE;
    refusal->detail = NULL;
    refusal->fix = NULL;
    refusal->subject = NULL;
    refusal->has_fix = 0;
    refusal->storage = NULL;
    return WEIVEL_C_STATUS_OK;
}
