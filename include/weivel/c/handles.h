#ifndef WEIVEL_C_HANDLES_H
#define WEIVEL_C_HANDLES_H

/**
 * @file
 * The `handles` module of the weivel C language implementation: the table from the core's handles to the layer's own
 * objects. The core holds no object, only handles, so whoever is handed a handle by the core keeps here the object it
 * stands for, and finds it again when a later answer or event names that handle.
 *
 * The table decides nothing and calls nothing: it never calls into the core, and it compares handles for equality
 * and for nothing else. Its rules are the contract's:
 *
 * - An entry is made when the core answers the handle and ended when what the handle stands for ends: a service is
 *   unregistered, an implementation withdrawn, a watch stopped, a want settled or withdrawn, a plugin released or
 *   forgotten. The table cannot know either moment, so its caller makes and ends each entry then.
 * - A handle the table does not hold answers nothing, WEIVEL_C_STATUS_NONE, whether it ended or was never known. It is
 *   never matched to another entry, however alike the two handles are.
 * - What a plugin holds, the object it was handed for another's registration or implementation, is a holding, kept
 *   by the pair of the two handles and dropped by the WEIVEL_EVENT_REVOKED that names the pair. That event's `plugin`
 *   may be a plugin already forgotten: it is then only half of a key and is never used to call the core with.
 * - Each entry has a token of the table's own, which reaches the entry again without a search. A plugin is opened
 *   with its token as its payload (`weivel_plugin_options.payload`), so the payload the core answers for a plugin
 *   handle leads straight to the plugin's record.
 *
 * Every object is a `void *` the table neither reads nor frees. The table does not check who asks: its caller looks
 * up only a handle the core answered to the plugin it is working for.
 *
 * Threading, for every call here: any thread. The table has one lock of its own, held only inside a call and never
 * while anything outside the table runs, so a call may be made from a listener, from a step, and under the layer lock
 * by the step a call runs before its events are dispatched.
 */

#include <stdint.h>

#include <weivel/c/core.h>

#ifdef __cplusplus
extern "C" {
#endif

/** What a handle of an entry stands for. */
typedef enum weivel_c_handle_kind {
    /** A plugin, as weivel_register_manifest answered it. */
    WEIVEL_C_HANDLE_PLUGIN = 1,
    /** A service's registration, as weivel_register_service answered it. */
    WEIVEL_C_HANDLE_SERVICE_REGISTRATION = 2,
    /** An implementation of a capability, as weivel_provide_capability answered it. */
    WEIVEL_C_HANDLE_CAPABILITY_IMPLEMENTATION = 3,
    /** A watch of a service or of a capability. */
    WEIVEL_C_HANDLE_WATCH = 4,
    /** A want. */
    WEIVEL_C_HANDLE_WANT = 5
} weivel_c_handle_kind;

/** The table. Opaque; made by weivel_c_handles_create and freed by weivel_c_handles_free. */
typedef struct weivel_c_handles weivel_c_handles;

/** One entry, as it is added and as it is answered. */
typedef struct weivel_c_handle_entry {
    uint32_t size;
    /** A weivel_c_handle_kind. */
    uint32_t kind;
    /** The handle the core answered. Never 0. */
    weivel_handle handle;
    /**
     * The plugin the entry belongs to, WEIVEL_HOST for the host's own, or 0 for an entry nothing owns, which is what
     * a plugin's own entry is. weivel_c_handles_end_owned finds entries by it.
     */
    weivel_handle owner;
    /** The layer's object for the handle. Never NULL. */
    void *object;
    /** The table's own token for the entry, never 0; ignored when an entry is added. */
    uint64_t token;
} weivel_c_handle_entry;

/**
 * Makes an empty table.
 *
 * @param handles receives the table, owned by the caller until weivel_c_handles_free. Required; NULL on every answer
 * but OK.
 * @return WEIVEL_C_STATUS_OK, WEIVEL_C_STATUS_MISUSE or WEIVEL_C_STATUS_OUT_OF_MEMORY.
 */
WEIVEL_C_API weivel_c_status weivel_c_handles_create(weivel_c_handles **handles);

/**
 * Frees a table and every entry and holding left in it; the objects are their owners' and are not touched.
 *
 * @return WEIVEL_C_STATUS_OK, also for NULL.
 * Threading: no call on this table may be in flight or follow.
 */
WEIVEL_C_API weivel_c_status weivel_c_handles_free(weivel_c_handles *handles);

/**
 * Makes the entry for a handle the core just answered.
 *
 * @param entry its `kind`, `handle`, `owner` and `object`; read only during the call. Required.
 * @param token receives the entry's token; may be NULL.
 * @return WEIVEL_C_STATUS_OK; WEIVEL_C_STATUS_MISUSE, the table unchanged, for a NULL argument, an entry smaller than
 * its first shape, a `handle` of 0, a NULL `object`, a `kind` that is none, or a handle the table already holds, since
 * the core never answers a handle again while its object lives; WEIVEL_C_STATUS_OUT_OF_MEMORY, the table unchanged.
 */
WEIVEL_C_API weivel_c_status weivel_c_handles_add(weivel_c_handles *handles, const weivel_c_handle_entry *entry,
                                                  uint64_t *token);

/**
 * Reserves the room one later addition takes, an entry or a holding, so that addition needs no memory and cannot
 * fail for want of it. It is what a caller does before it asks the core for a handle: the entry is then made, with
 * weivel_c_handles_add_reserved, by a step that has no way to fail, and the core never holds an object whose handle
 * the table lacks. A reservation that was not used is given back with weivel_c_handles_unreserve.
 *
 * @return WEIVEL_C_STATUS_OK; WEIVEL_C_STATUS_OUT_OF_MEMORY, nothing reserved; WEIVEL_C_STATUS_MISUSE for NULL.
 */
WEIVEL_C_API weivel_c_status weivel_c_handles_reserve(weivel_c_handles *handles);

/**
 * Gives back one reservation that was not used.
 *
 * @return WEIVEL_C_STATUS_OK; WEIVEL_C_STATUS_MISUSE for NULL or when nothing is reserved.
 */
WEIVEL_C_API weivel_c_status weivel_c_handles_unreserve(weivel_c_handles *handles);

/**
 * weivel_c_handles_add into room a reservation holds, which it uses up whatever it answers. It allocates nothing.
 *
 * @return WEIVEL_C_STATUS_OK; WEIVEL_C_STATUS_MISUSE as weivel_c_handles_add answers it, and when nothing is
 * reserved. Never WEIVEL_C_STATUS_OUT_OF_MEMORY.
 */
WEIVEL_C_API weivel_c_status weivel_c_handles_add_reserved(weivel_c_handles *handles,
                                                           const weivel_c_handle_entry *entry, uint64_t *token);

/**
 * Answers the entry of a handle. It allocates nothing and costs one probe of a hash table on average.
 *
 * @param entry receives the entry; required, with its `size` set.
 * @return WEIVEL_C_STATUS_OK; WEIVEL_C_STATUS_NONE, `entry` untouched, for a handle the table does not hold;
 * WEIVEL_C_STATUS_MISUSE.
 */
WEIVEL_C_API weivel_c_status weivel_c_handles_find(weivel_c_handles *handles, weivel_handle handle,
                                                   weivel_c_handle_entry *entry);

/**
 * Answers the entry a token was answered for, by the token alone: no handle is compared and nothing is searched.
 *
 * @return WEIVEL_C_STATUS_OK; WEIVEL_C_STATUS_NONE for a token of an entry that ended, or one this table never
 * answered; WEIVEL_C_STATUS_MISUSE.
 */
WEIVEL_C_API weivel_c_status weivel_c_handles_find_token(weivel_c_handles *handles, uint64_t token,
                                                         weivel_c_handle_entry *entry);

/**
 * Ends the entry of a handle, because what it stands for ended.
 *
 * @param ended receives the entry as it was, so its owner can free the object; may be NULL.
 * @return WEIVEL_C_STATUS_OK; WEIVEL_C_STATUS_NONE for a handle the table does not hold; WEIVEL_C_STATUS_MISUSE.
 */
WEIVEL_C_API weivel_c_status weivel_c_handles_end(weivel_c_handles *handles, weivel_handle handle,
                                                  weivel_c_handle_entry *ended);

/**
 * Ends one entry that `owner` owns, whichever, and answers it: called until it answers WEIVEL_C_STATUS_NONE, it ends
 * every registration, implementation, watch and want of a plugin whose release has been heard. Holdings are not
 * entries and stay until their revocations.
 *
 * @param ended receives the entry as it was; may be NULL.
 * @return WEIVEL_C_STATUS_OK; WEIVEL_C_STATUS_NONE when `owner` owns no entry; WEIVEL_C_STATUS_MISUSE.
 */
WEIVEL_C_API weivel_c_status weivel_c_handles_end_owned(weivel_c_handles *handles, weivel_handle owner,
                                                        weivel_c_handle_entry *ended);

/**
 * Records that `plugin` holds `implementation`, with the object it was handed for it. Holding it again replaces the
 * object.
 *
 * @return WEIVEL_C_STATUS_OK; WEIVEL_C_STATUS_MISUSE for a NULL table or object or a handle of 0;
 * WEIVEL_C_STATUS_OUT_OF_MEMORY, the table unchanged.
 */
WEIVEL_C_API weivel_c_status weivel_c_handles_hold(weivel_c_handles *handles, weivel_handle plugin,
                                                   weivel_handle implementation, void *object);

/**
 * weivel_c_handles_hold into room a reservation holds, which it uses up whatever it answers. It allocates nothing.
 *
 * @return WEIVEL_C_STATUS_OK; WEIVEL_C_STATUS_MISUSE as weivel_c_handles_hold answers it, and when nothing is
 * reserved. Never WEIVEL_C_STATUS_OUT_OF_MEMORY.
 */
WEIVEL_C_API weivel_c_status weivel_c_handles_hold_reserved(weivel_c_handles *handles, weivel_handle plugin,
                                                            weivel_handle implementation, void *object);

/**
 * Answers the object `plugin` was handed for `implementation`.
 *
 * @param object receives it; required.
 * @return WEIVEL_C_STATUS_OK; WEIVEL_C_STATUS_NONE when the plugin holds no such thing; WEIVEL_C_STATUS_MISUSE.
 */
WEIVEL_C_API weivel_c_status weivel_c_handles_held(weivel_c_handles *handles, weivel_handle plugin,
                                                   weivel_handle implementation, void **object);

/**
 * Drops the holding a WEIVEL_EVENT_REVOKED names. The event's `plugin` is used as half of the key and for nothing
 * else, so it may name a plugin the core has forgotten.
 *
 * @param event the event heard; required.
 * @param object receives the object the plugin was handed; may be NULL.
 * @return WEIVEL_C_STATUS_OK; WEIVEL_C_STATUS_NONE for an event of another kind, or a pair the table does not hold;
 * WEIVEL_C_STATUS_MISUSE.
 */
WEIVEL_C_API weivel_c_status weivel_c_handles_revoke(weivel_c_handles *handles, const weivel_c_event *event,
                                                     void **object);

/**
 * Answers how many entries and how many holdings the table holds.
 *
 * @param entries receives the entries; may be NULL.
 * @param holdings receives the holdings; may be NULL.
 * @return WEIVEL_C_STATUS_OK or WEIVEL_C_STATUS_MISUSE.
 */
WEIVEL_C_API weivel_c_status weivel_c_handles_count(weivel_c_handles *handles, uint32_t *entries,
                                                    uint32_t *holdings);

#ifdef __cplusplus
}
#endif

#endif
