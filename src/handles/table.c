#include "table.h"

#include <stdlib.h>
#include <string.h>

#include "../core/mutex.h"

#define END_OF(type, field) (offsetof(type, field) + sizeof(((type *)0)->field))
#define FIRST_ENTRY_SIZE END_OF(weivel_c_handle_entry, token)
#define FIRST_CAPACITY 16
#define NODES_OF_ONE_ADDITION 2

/** What a node's key belongs to: two nodes are one only when their space and both halves of their key agree. */
enum { SPACE_FREE = 0, SPACE_OF_ENTRIES = 1, SPACE_OF_HOLDINGS = 2, SPACE_OF_OWNERS = 3 };

/**
 * One slot of the table: an entry keyed by its handle, a holding keyed by the implementation and the plugin holding
 * it, or the head of the list of entries one owner owns, keyed by the owner.
 *
 * @implNote A position is an index into the slots plus 1, so 0 is none. `next` is the next entry of the same owner
 * for an entry, the first entry owned for an owner's head, and the next free slot for a free one. `generation` moves
 * each time the slot is freed, which is what makes a token of an ended entry answer nothing.
 */
typedef struct node {
    uint64_t first_key;
    uint64_t second_key;
    uint64_t owner;
    void *object;
    uint32_t space;
    uint32_t kind;
    uint32_t generation;
    uint32_t next;
    uint32_t previous;
} node;

/**
 * @implNote `lock` guards every field below it and is a leaf: nothing is called while it is held but the allocator.
 * `buckets` is an open-addressed hash table of positions, probed linearly and kept at most half full, so a lookup
 * allocates nothing and ends at the first empty bucket.
 */
struct weivel_c_handles {
    weivel_c_mutex lock;
    weivel_c_allocator allocator;
    weivel_c_handles_hash hash;
    node *nodes;
    uint32_t node_capacity;
    uint32_t nodes_begun;
    uint32_t first_free;
    uint32_t free_nodes;
    uint32_t *buckets;
    uint32_t bucket_count;
    uint32_t buckets_in_use;
    uint32_t entries;
    uint32_t holdings;
    uint32_t reserved;
};

static void *allocate_plainly(void *data, size_t size) {
    (void)data;
    return malloc(size);
}

static void release_plainly(void *data, void *memory) {
    (void)data;
    free(memory);
}

static uint64_t mix(uint64_t value) {
    value ^= value >> 33;
    value *= 0xff51afd7ed558ccdULL;
    value ^= value >> 33;
    value *= 0xc4ceb9fe1a85ec53ULL;
    value ^= value >> 33;
    return value;
}

static uint64_t hash_plainly(uint32_t space, uint64_t first_key, uint64_t second_key) {
    return mix(first_key ^ mix(second_key + space));
}

static uint32_t home_of(const weivel_c_handles *table, uint32_t space, uint64_t first_key, uint64_t second_key) {
    return (uint32_t)(table->hash(space, first_key, second_key) & (table->bucket_count - 1));
}

static node *node_at(const weivel_c_handles *table, uint32_t position) {
    return &table->nodes[position - 1];
}

static int is_the_node(const node *candidate, uint32_t space, uint64_t first_key, uint64_t second_key) {
    return candidate->space == space && candidate->first_key == first_key && candidate->second_key == second_key;
}

/** The bucket that holds the node of a key, or the empty bucket its probe ends at. */
static uint32_t bucket_of(const weivel_c_handles *table, uint32_t space, uint64_t first_key, uint64_t second_key) {
    uint32_t bucket = home_of(table, space, first_key, second_key);
    while (table->buckets[bucket] != 0 &&
           !is_the_node(node_at(table, table->buckets[bucket]), space, first_key, second_key)) {
        bucket = (bucket + 1) & (table->bucket_count - 1);
    }
    return bucket;
}

static uint32_t position_of(const weivel_c_handles *table, uint32_t space, uint64_t first_key, uint64_t second_key) {
    return table->bucket_count == 0 ? 0 : table->buckets[bucket_of(table, space, first_key, second_key)];
}

static void place_in_a_bucket(weivel_c_handles *table, uint32_t position) {
    const node *placed = node_at(table, position);
    table->buckets[bucket_of(table, placed->space, placed->first_key, placed->second_key)] = position;
}

static int grow_buckets(weivel_c_handles *table) {
    uint32_t count = table->bucket_count == 0 ? FIRST_CAPACITY * 2 : table->bucket_count * 2;
    uint32_t *grown = table->allocator.allocate(table->allocator.data, count * sizeof *grown);
    uint32_t *old = table->buckets;
    uint32_t position;
    if (grown == NULL) {
        return 0;
    }
    memset(grown, 0, count * sizeof *grown);
    table->buckets = grown;
    table->bucket_count = count;
    for (position = 1; position <= table->nodes_begun; position++) {
        if (node_at(table, position)->space != SPACE_FREE) {
            place_in_a_bucket(table, position);
        }
    }
    if (old != NULL) {
        table->allocator.release(table->allocator.data, old);
    }
    return 1;
}

static int grow_nodes(weivel_c_handles *table) {
    uint32_t capacity = table->node_capacity == 0 ? FIRST_CAPACITY : table->node_capacity * 2;
    node *grown = table->allocator.allocate(table->allocator.data, capacity * sizeof *grown);
    if (grown == NULL) {
        return 0;
    }
    if (table->nodes != NULL) {
        memcpy(grown, table->nodes, table->nodes_begun * sizeof *grown);
        table->allocator.release(table->allocator.data, table->nodes);
    }
    table->nodes = grown;
    table->node_capacity = capacity;
    return 1;
}

/**
 * Makes room for one addition, beyond every addition already reserved, before anything is changed, so an addition
 * that runs out of memory changes nothing and a reserved one never needs memory.
 */
static int has_room_for_an_addition(weivel_c_handles *table) {
    uint32_t needed = (table->reserved + 1) * NODES_OF_ONE_ADDITION;
    while (table->node_capacity - table->nodes_begun + table->free_nodes < needed) {
        if (!grow_nodes(table)) {
            return 0;
        }
    }
    while ((table->buckets_in_use + needed) * 2 > table->bucket_count) {
        if (!grow_buckets(table)) {
            return 0;
        }
    }
    return 1;
}

/** Turns one reservation into the room the addition that follows takes. Answers 0 when nothing is reserved. */
static int use_a_reservation(weivel_c_handles *table) {
    if (table->reserved == 0) {
        return 0;
    }
    table->reserved--;
    return 1;
}

static uint32_t take_a_slot(weivel_c_handles *table) {
    uint32_t position = table->first_free;
    if (position != 0) {
        table->first_free = node_at(table, position)->next;
        table->free_nodes--;
        return position;
    }
    table->nodes_begun++;
    node_at(table, table->nodes_begun)->generation = 1;
    return table->nodes_begun;
}

static uint32_t begin_node(weivel_c_handles *table, uint32_t space, uint64_t first_key, uint64_t second_key) {
    uint32_t position = take_a_slot(table);
    node *begun = node_at(table, position);
    uint32_t generation = begun->generation;
    memset(begun, 0, sizeof *begun);
    begun->generation = generation;
    begun->space = space;
    begun->first_key = first_key;
    begun->second_key = second_key;
    place_in_a_bucket(table, position);
    table->buckets_in_use++;
    return position;
}

/** Empties a bucket and moves back whatever its probe had pushed past it, so no lookup ends early at the gap. */
static void vacate_bucket(weivel_c_handles *table, uint32_t hole) {
    uint32_t mask = table->bucket_count - 1;
    uint32_t bucket = (hole + 1) & mask;
    while (table->buckets[bucket] != 0) {
        const node *moved = node_at(table, table->buckets[bucket]);
        uint32_t home = home_of(table, moved->space, moved->first_key, moved->second_key);
        if (((bucket - home) & mask) >= ((bucket - hole) & mask)) {
            table->buckets[hole] = table->buckets[bucket];
            hole = bucket;
        }
        bucket = (bucket + 1) & mask;
    }
    table->buckets[hole] = 0;
}

static void end_node(weivel_c_handles *table, uint32_t position) {
    node *ended = node_at(table, position);
    vacate_bucket(table, bucket_of(table, ended->space, ended->first_key, ended->second_key));
    ended->space = SPACE_FREE;
    ended->generation++;
    ended->object = NULL;
    ended->next = table->first_free;
    table->first_free = position;
    table->free_nodes++;
    table->buckets_in_use--;
}

static void join_the_owned(weivel_c_handles *table, uint32_t position) {
    node *joining = node_at(table, position);
    uint32_t head = position_of(table, SPACE_OF_OWNERS, joining->owner, 0);
    if (head == 0) {
        head = begin_node(table, SPACE_OF_OWNERS, joining->owner, 0);
    }
    joining = node_at(table, position);
    joining->previous = 0;
    joining->next = node_at(table, head)->next;
    if (joining->next != 0) {
        node_at(table, joining->next)->previous = position;
    }
    node_at(table, head)->next = position;
}

static void leave_the_owned(weivel_c_handles *table, uint32_t position) {
    const node *leaving = node_at(table, position);
    uint32_t head = position_of(table, SPACE_OF_OWNERS, leaving->owner, 0);
    if (leaving->next != 0) {
        node_at(table, leaving->next)->previous = leaving->previous;
    }
    if (leaving->previous != 0) {
        node_at(table, leaving->previous)->next = leaving->next;
        return;
    }
    node_at(table, head)->next = leaving->next;
    if (leaving->next == 0) {
        end_node(table, head);
    }
}

static uint64_t token_of(const weivel_c_handles *table, uint32_t position) {
    return ((uint64_t)node_at(table, position)->generation << 32) | position;
}

/** The position a token names, or 0 when its entry ended or it was never one of this table's. */
static uint32_t position_of_token(const weivel_c_handles *table, uint64_t token) {
    uint32_t position = (uint32_t)(token & UINT32_MAX);
    uint32_t generation = (uint32_t)(token >> 32);
    if (position == 0 || position > table->nodes_begun) {
        return 0;
    }
    if (node_at(table, position)->space != SPACE_OF_ENTRIES || node_at(table, position)->generation != generation) {
        return 0;
    }
    return position;
}

static void answer_entry(const weivel_c_handles *table, uint32_t position, weivel_c_handle_entry *entry) {
    const node *found = node_at(table, position);
    if (entry == NULL) {
        return;
    }
    entry->kind = found->kind;
    entry->handle = found->first_key;
    entry->owner = found->owner;
    entry->object = found->object;
    entry->token = token_of(table, position);
}

static void end_entry(weivel_c_handles *table, uint32_t position, weivel_c_handle_entry *ended) {
    answer_entry(table, position, ended);
    if (node_at(table, position)->owner != 0) {
        leave_the_owned(table, position);
    }
    end_node(table, position);
    table->entries--;
}

static int is_a_kind(uint32_t kind) {
    return kind >= WEIVEL_C_HANDLE_PLUGIN && kind <= WEIVEL_C_HANDLE_WANT;
}

static int is_addable(const weivel_c_handle_entry *entry) {
    return entry != NULL && entry->size >= FIRST_ENTRY_SIZE && entry->handle != 0 && entry->object != NULL &&
           is_a_kind(entry->kind);
}

static int is_answerable(const weivel_c_handle_entry *entry) {
    return entry == NULL || entry->size >= FIRST_ENTRY_SIZE;
}

static weivel_c_status add_entry(weivel_c_handles *table, const weivel_c_handle_entry *entry, uint64_t *token) {
    uint32_t position;
    if (position_of(table, SPACE_OF_ENTRIES, entry->handle, 0) != 0) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    if (!has_room_for_an_addition(table)) {
        return WEIVEL_C_STATUS_OUT_OF_MEMORY;
    }
    position = begin_node(table, SPACE_OF_ENTRIES, entry->handle, 0);
    node_at(table, position)->kind = entry->kind;
    node_at(table, position)->owner = entry->owner;
    node_at(table, position)->object = entry->object;
    if (entry->owner != 0) {
        join_the_owned(table, position);
    }
    table->entries++;
    if (token != NULL) {
        *token = token_of(table, position);
    }
    return WEIVEL_C_STATUS_OK;
}

weivel_c_status weivel_c_handles_create(weivel_c_handles **handles) {
    weivel_c_handles *created;
    if (handles == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    *handles = NULL;
    created = calloc(1, sizeof *created);
    if (created == NULL) {
        return WEIVEL_C_STATUS_OUT_OF_MEMORY;
    }
    if (!weivel_c_mutex_start(&created->lock)) {
        free(created);
        return WEIVEL_C_STATUS_OUT_OF_MEMORY;
    }
    created->allocator = (weivel_c_allocator){allocate_plainly, release_plainly, NULL};
    created->hash = hash_plainly;
    *handles = created;
    return WEIVEL_C_STATUS_OK;
}

weivel_c_status weivel_c_handles_free(weivel_c_handles *handles) {
    if (handles == NULL) {
        return WEIVEL_C_STATUS_OK;
    }
    if (handles->nodes != NULL) {
        handles->allocator.release(handles->allocator.data, handles->nodes);
    }
    if (handles->buckets != NULL) {
        handles->allocator.release(handles->allocator.data, handles->buckets);
    }
    weivel_c_mutex_end(&handles->lock);
    free(handles);
    return WEIVEL_C_STATUS_OK;
}

void weivel_c_handles_replace_allocator(weivel_c_handles *handles, const weivel_c_allocator *allocator) {
    weivel_c_mutex_lock(&handles->lock);
    handles->allocator = *allocator;
    weivel_c_mutex_unlock(&handles->lock);
}

void weivel_c_handles_replace_hash(weivel_c_handles *handles, weivel_c_handles_hash hash) {
    weivel_c_mutex_lock(&handles->lock);
    handles->hash = hash;
    weivel_c_mutex_unlock(&handles->lock);
}

weivel_c_status weivel_c_handles_add(weivel_c_handles *handles, const weivel_c_handle_entry *entry,
                                     uint64_t *token) {
    weivel_c_status status;
    if (handles == NULL || !is_addable(entry)) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    weivel_c_mutex_lock(&handles->lock);
    status = add_entry(handles, entry, token);
    weivel_c_mutex_unlock(&handles->lock);
    return status;
}

weivel_c_status weivel_c_handles_reserve(weivel_c_handles *handles) {
    weivel_c_status status = WEIVEL_C_STATUS_OUT_OF_MEMORY;
    if (handles == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    weivel_c_mutex_lock(&handles->lock);
    if (has_room_for_an_addition(handles)) {
        handles->reserved++;
        status = WEIVEL_C_STATUS_OK;
    }
    weivel_c_mutex_unlock(&handles->lock);
    return status;
}

weivel_c_status weivel_c_handles_unreserve(weivel_c_handles *handles) {
    int reserved;
    if (handles == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    weivel_c_mutex_lock(&handles->lock);
    reserved = use_a_reservation(handles);
    weivel_c_mutex_unlock(&handles->lock);
    return reserved ? WEIVEL_C_STATUS_OK : WEIVEL_C_STATUS_MISUSE;
}

weivel_c_status weivel_c_handles_add_reserved(weivel_c_handles *handles, const weivel_c_handle_entry *entry,
                                              uint64_t *token) {
    weivel_c_status status = WEIVEL_C_STATUS_MISUSE;
    if (handles == NULL || !is_addable(entry)) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    weivel_c_mutex_lock(&handles->lock);
    if (use_a_reservation(handles)) {
        status = add_entry(handles, entry, token);
    }
    weivel_c_mutex_unlock(&handles->lock);
    return status;
}

weivel_c_status weivel_c_handles_find(weivel_c_handles *handles, weivel_handle handle,
                                      weivel_c_handle_entry *entry) {
    uint32_t position;
    if (handles == NULL || entry == NULL || !is_answerable(entry)) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    weivel_c_mutex_lock(&handles->lock);
    position = position_of(handles, SPACE_OF_ENTRIES, handle, 0);
    if (position != 0) {
        answer_entry(handles, position, entry);
    }
    weivel_c_mutex_unlock(&handles->lock);
    return position != 0 ? WEIVEL_C_STATUS_OK : WEIVEL_C_STATUS_NONE;
}

weivel_c_status weivel_c_handles_find_token(weivel_c_handles *handles, uint64_t token,
                                            weivel_c_handle_entry *entry) {
    uint32_t position;
    if (handles == NULL || entry == NULL || !is_answerable(entry)) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    weivel_c_mutex_lock(&handles->lock);
    position = position_of_token(handles, token);
    if (position != 0) {
        answer_entry(handles, position, entry);
    }
    weivel_c_mutex_unlock(&handles->lock);
    return position != 0 ? WEIVEL_C_STATUS_OK : WEIVEL_C_STATUS_NONE;
}

weivel_c_status weivel_c_handles_end(weivel_c_handles *handles, weivel_handle handle, weivel_c_handle_entry *ended) {
    uint32_t position;
    if (handles == NULL || !is_answerable(ended)) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    weivel_c_mutex_lock(&handles->lock);
    position = position_of(handles, SPACE_OF_ENTRIES, handle, 0);
    if (position != 0) {
        end_entry(handles, position, ended);
    }
    weivel_c_mutex_unlock(&handles->lock);
    return position != 0 ? WEIVEL_C_STATUS_OK : WEIVEL_C_STATUS_NONE;
}

weivel_c_status weivel_c_handles_end_owned(weivel_c_handles *handles, weivel_handle owner,
                                           weivel_c_handle_entry *ended) {
    uint32_t head;
    if (handles == NULL || owner == 0 || !is_answerable(ended)) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    weivel_c_mutex_lock(&handles->lock);
    head = position_of(handles, SPACE_OF_OWNERS, owner, 0);
    if (head != 0) {
        end_entry(handles, node_at(handles, head)->next, ended);
    }
    weivel_c_mutex_unlock(&handles->lock);
    return head != 0 ? WEIVEL_C_STATUS_OK : WEIVEL_C_STATUS_NONE;
}

static weivel_c_status hold(weivel_c_handles *table, weivel_handle plugin, weivel_handle implementation,
                            void *object) {
    uint32_t position = position_of(table, SPACE_OF_HOLDINGS, implementation, plugin);
    if (position == 0) {
        if (!has_room_for_an_addition(table)) {
            return WEIVEL_C_STATUS_OUT_OF_MEMORY;
        }
        position = begin_node(table, SPACE_OF_HOLDINGS, implementation, plugin);
        table->holdings++;
    }
    node_at(table, position)->object = object;
    return WEIVEL_C_STATUS_OK;
}

weivel_c_status weivel_c_handles_hold(weivel_c_handles *handles, weivel_handle plugin, weivel_handle implementation,
                                      void *object) {
    weivel_c_status status;
    if (handles == NULL || plugin == 0 || implementation == 0 || object == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    weivel_c_mutex_lock(&handles->lock);
    status = hold(handles, plugin, implementation, object);
    weivel_c_mutex_unlock(&handles->lock);
    return status;
}

weivel_c_status weivel_c_handles_hold_reserved(weivel_c_handles *handles, weivel_handle plugin,
                                               weivel_handle implementation, void *object) {
    weivel_c_status status = WEIVEL_C_STATUS_MISUSE;
    if (handles == NULL || plugin == 0 || implementation == 0 || object == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    weivel_c_mutex_lock(&handles->lock);
    if (use_a_reservation(handles)) {
        status = hold(handles, plugin, implementation, object);
    }
    weivel_c_mutex_unlock(&handles->lock);
    return status;
}

weivel_c_status weivel_c_handles_held(weivel_c_handles *handles, weivel_handle plugin, weivel_handle implementation,
                                      void **object) {
    uint32_t position;
    if (handles == NULL || object == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    weivel_c_mutex_lock(&handles->lock);
    position = position_of(handles, SPACE_OF_HOLDINGS, implementation, plugin);
    if (position != 0) {
        *object = node_at(handles, position)->object;
    }
    weivel_c_mutex_unlock(&handles->lock);
    return position != 0 ? WEIVEL_C_STATUS_OK : WEIVEL_C_STATUS_NONE;
}

static weivel_c_status drop_holding(weivel_c_handles *table, weivel_handle plugin, weivel_handle implementation,
                                    void **object) {
    uint32_t position = position_of(table, SPACE_OF_HOLDINGS, implementation, plugin);
    if (position == 0) {
        return WEIVEL_C_STATUS_NONE;
    }
    if (object != NULL) {
        *object = node_at(table, position)->object;
    }
    end_node(table, position);
    table->holdings--;
    return WEIVEL_C_STATUS_OK;
}

weivel_c_status weivel_c_handles_revoke(weivel_c_handles *handles, const weivel_c_event *event, void **object) {
    weivel_c_status status;
    if (handles == NULL || event == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    if (event->kind != WEIVEL_EVENT_REVOKED) {
        return WEIVEL_C_STATUS_NONE;
    }
    weivel_c_mutex_lock(&handles->lock);
    status = drop_holding(handles, event->data.revoked.plugin, event->data.revoked.implementation, object);
    weivel_c_mutex_unlock(&handles->lock);
    return status;
}

weivel_c_status weivel_c_handles_count(weivel_c_handles *handles, uint32_t *entries, uint32_t *holdings) {
    if (handles == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    weivel_c_mutex_lock(&handles->lock);
    if (entries != NULL) {
        *entries = handles->entries;
    }
    if (holdings != NULL) {
        *holdings = handles->holdings;
    }
    weivel_c_mutex_unlock(&handles->lock);
    return WEIVEL_C_STATUS_OK;
}
