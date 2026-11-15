#ifndef WEIVEL_C_HOST_LIBRARY_H
#define WEIVEL_C_HOST_LIBRARY_H

/**
 * @file
 * C libraries as host services. The host loads a C library from a declaration value, the standard's published
 * weivel_standard_library, whether the standard read it or the caller built it, and registers each declared service
 * under WEIVEL_HOST: a service of bytes functions as one weivel_c_library_service object, and a service that declares
 * a definition as that definition.
 *
 * - A consumer that resolves the service, by a get, a settled want or a watch, is answered that very object, as any
 *   provider's object is. Calling a function through it is a plain call into the library that crosses no core call,
 *   takes no lock of the layer's and copies no byte.
 * - The input is borrowed: the library is handed the caller's own pointer and length for the length of the call.
 * - The output is answered in place, as a weivel_c_bytes holding the library's own pointer: never copied, and freed
 *   only by the library's declared release symbol, exactly once, when its holder releases it.
 * - A definition (WEIVEL_STANDARD_FUNCTION_DEFINITION) is a symbol `const void *f(void)` answering a static struct of
 *   function pointers that begins with its `uint32_t size`. The host calls it once at the load and registers the very
 *   pointer it answered, with no wrapper, count or copy, through weivel_c_context_register_service_awaiting_releases,
 *   so a consumer's call through it is a plain call into the library. The host reads its `size` and nothing past it.
 * - A library unloaded, by weivel_c_host_unload_library or by the host's destroy, has its services unregistered and
 *   refuses every call made after it through a bytes object. A definition is called directly, so no call to it can be
 *   refused: its unregistration revokes every holding of it, each holder told by a WEIVEL_C_PLUGIN_EVENT_REVOKED whose
 *   `handle` it hands weivel_c_context_release_holding once it has let go.
 * - Its binary is closed only once no call runs in it, no bytes it answered are still held, and the core has told the
 *   host that nothing holds any of its definitions, whichever ends last, so nothing ever calls into a closed library,
 *   and bytes still held stay readable until released. A holder that never lets go of a definition keeps the binary
 *   open until weivel_c_host_free.
 *
 * The host includes the standard's declaration types and never loads, links or calls the standard's binary.
 *
 * Threading: any thread may call any function here. A call through a service takes no lock: it counts itself in and
 * out of its library with one atomic word, which is also how an unload made meanwhile is seen.
 */

#include <stddef.h>
#include <stdint.h>

#include <weivel/c/host.h>
#include <weivel/library.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Bytes a library function answered, in place: `data` is the library's own pointer and `length` its length. Held by
 * the caller it was answered to until weivel_c_bytes_release, which hands `data` to the library's release symbol.
 * Empty, with `data` NULL, `length` 0 and `owner` NULL, as WEIVEL_INIT or a release leaves it.
 *
 * A copy of the struct is the same bytes, not a second holding: release one of the two only, since the layer cannot
 * tell a copy from the original.
 */
typedef struct weivel_c_bytes {
    uint32_t size;
    const uint8_t *data;
    size_t length;
    /** What the release needs to reach the library that answered the bytes. Private to the layer. */
    void *owner;
} weivel_c_bytes;

/**
 * Releases bytes a library function answered: hands them to the library's declared release symbol, once, and empties
 * `bytes`. A library unloaded while the bytes were held closes now if they were the last thing keeping it open; a
 * close the platform refuses is told to the error sink, unless the host has been destroyed, after which it is told to
 * nobody.
 *
 * @return WEIVEL_C_STATUS_OK, also as a no-op for empty bytes, a second release of the same struct among them;
 * WEIVEL_C_STATUS_MISUSE for NULL or a struct smaller than its first shape.
 * Threading: any thread; defined until weivel_c_host_free of the host whose library answered the bytes, which refuses
 * to free while any are held.
 */
WEIVEL_C_API weivel_c_status weivel_c_bytes_release(weivel_c_bytes *bytes);

/** What one call of a library function answered. */
typedef struct weivel_c_library_answer {
    uint32_t size;
    /** The status the function answered: 0 for a result, any other value for a failure. */
    int32_t status;
    /**
     * For a failure, 1 when `body` is well-formed UTF-8, so it reads as the English text saying why, and 0 when it is
     * not, so it is only bytes. A failure with an empty or absent body answers 1, since no bytes are well-formed UTF-8.
     * 0 for a result, whose bytes are never inspected.
     */
    uint32_t body_is_utf8;
    /**
     * The result, or the failure's body, answered in place; empty when the function answered none. A `size` of 0, as
     * WEIVEL_INIT of this struct leaves it, is taken as the layer's own size and set to it by a call.
     */
    weivel_c_bytes body;
} weivel_c_library_answer;

typedef struct weivel_c_library_service weivel_c_library_service;

/**
 * Calls one declared function of a service.
 *
 * @param service the object the call is made through. Required.
 * @param function the function's index in `service->function_names`.
 * @param input the caller's bytes, handed to the library unchanged for the length of the call; NULL only with a
 * length of 0.
 * @param answer the caller's struct with `size` set and an empty `body`, as WEIVEL_INIT of it alone leaves it; filled
 * on OK and on FAILED, and left with an empty body on every other answer.
 * @return WEIVEL_C_STATUS_OK for a status of 0; WEIVEL_C_STATUS_FAILED for any other status, which `answer->status`
 * holds with its body; WEIVEL_C_STATUS_UNLOADED, with nothing called, once the library is unloaded;
 * WEIVEL_C_STATUS_OUT_OF_MEMORY, with nothing called, while the library already holds as many calls and bodies as its
 * count of them can hold; WEIVEL_C_STATUS_MISUSE (a NULL service or answer, an answer smaller than its first shape or
 * still holding a body, an index past the last function, or NULL input with a length).
 */
typedef weivel_c_status (*weivel_c_library_call)(const weivel_c_library_service *service, uint32_t function,
                                                  const uint8_t *input, size_t input_length,
                                                  weivel_c_library_answer *answer);

/**
 * The object a C library's service is registered with, under WEIVEL_HOST: one function per declared function, each
 * reached through `call` by its index. Everything in it is the host's, unchanged and valid until weivel_c_host_free.
 *
 * @implNote `call` and `release_bytes` are pointers rather than exported names, so a plugin that consumes the service
 * from a shared library of its own calls and releases through the object and links nothing of the layer's.
 */
struct weivel_c_library_service {
    uint32_t size;
    /** The service id, as declared. */
    const char *id;
    /** The declared functions' names, in the declaration's order: the index of a name is its function's index. */
    const char *const *function_names;
    uint32_t function_count;
    weivel_c_library_call call;
    /** weivel_c_bytes_release itself. */
    weivel_c_status (*release_bytes)(weivel_c_bytes *bytes);
};

/** One C library the host loaded. Opaque; the host's, valid until weivel_c_host_free. */
typedef struct weivel_c_library weivel_c_library;

/** Why weivel_c_host_load_library did not load, beyond its status. */
typedef struct weivel_c_library_load_failure {
    uint32_t size;
    /**
     * After WEIVEL_C_STATUS_MISSING_SYMBOL, the symbol the library lacks, the declaration's own text: its release
     * symbol or a function's. NULL otherwise.
     */
    const char *missing_symbol;
    /**
     * After WEIVEL_C_STATUS_MISUSE for a function the host does not load as declared, that function's name, the
     * declaration's own text: one of a kind other than WEIVEL_STANDARD_FUNCTION_BYTES and
     * WEIVEL_STANDARD_FUNCTION_DEFINITION, a definition beside another function of its service, or a definition given
     * context facts. NULL otherwise.
     */
    const char *wrong_kind;
    /**
     * After WEIVEL_C_STATUS_MISUSE for a service id the declaration lists twice, or one that a registration under this
     * host holds already (a plugin's, another live library's or the host's own), that id, the declaration's own text.
     * NULL otherwise.
     */
    const char *taken_id;
    /**
     * After WEIVEL_C_STATUS_MISUSE for a definition whose symbol answered NULL, or a struct whose `size` is below
     * `2 * sizeof(void *)`, that definition's symbol, the declaration's own text. NULL otherwise.
     */
    const char *refused_definition;
} weivel_c_library_load_failure;

/**
 * Loads a C library and registers each service it declares under WEIVEL_HOST.
 *
 * Opens the shared library at `path` with the platform's loader, as weivel_c_core_load does: a path with no directory
 * separator is searched for by the platform, so give an absolute path to load exactly one file. Binds the declared
 * release symbol and every declared function's symbol, calls each definition's symbol once, then registers each
 * service through the host's own context.
 *
 * A load that does not finish once a service is registered is undone as an unload is: its services are closed to
 * calls and unregistered, and its binary closes once nothing holds it, so an object a consumer was told of meanwhile
 * stays readable until weivel_c_host_free and answers WEIVEL_C_STATUS_UNLOADED.
 *
 * @param path the library, UTF-8; the declaration names none. Read only during the call. Required.
 * @param declaration read only during the call: every text the host keeps it copies. Required, with at least one
 * service, each with an id no other service of it repeats and at least one function, and each function with a name, a
 * symbol and the kind WEIVEL_STANDARD_FUNCTION_BYTES or WEIVEL_STANDARD_FUNCTION_DEFINITION, a definition being its
 * service's only function and reading no context; and with a release symbol when any function is of the bytes kind.
 * @param library receives the loaded library. Required; NULL on every answer but OK.
 * @param failure receives what a status alone cannot say; may be NULL. Cleared first when given and large enough.
 * @param refusal the core's refusal of a service's registration; may be NULL.
 * @return WEIVEL_C_STATUS_OK; WEIVEL_C_STATUS_MISUSE (a NULL or empty path, a NULL argument, a declaration or failure
 * smaller than its first shape, an empty text or list the declaration needs, a function the host does not load as
 * declared, a definition that answers NULL or a struct whose `size` is below `2 * sizeof(void *)`, an id it lists
 * twice, or an id a registration under this host holds already, a plugin's, another live library's or the
 * host's own, since registering it under WEIVEL_HOST would replace that registration; each the caller's mistake that
 * is told to nobody else); WEIVEL_C_STATUS_NOT_A_LIBRARY; WEIVEL_C_STATUS_MISSING_SYMBOL; WEIVEL_C_STATUS_REFUSED with
 * the core's refusal of a registration; WEIVEL_C_STATUS_OUT_OF_MEMORY; WEIVEL_C_STATUS_DESTROYED. Nothing stays
 * loaded, registered or held on any answer but OK: an id found registered already is found from the layer's own
 * records, never by a get, so the host comes to hold nothing it would later hear revoked.
 *
 * The refusal of an id registered already holds against every registration made before this call began. One a plugin
 * makes on another thread while the load runs can still be replaced by the library's registration under WEIVEL_HOST,
 * since the core registers for the host whether or not another owner holds the id; closing that window needs a
 * register-if-absent call for WEIVEL_HOST in the core, which it does not have. The host's own registrations hold both
 * ways, on any thread: weivel_c_host_register_service claims its id under the host's lock before it asks the core,
 * refusing an id a live library declares, and a load that finds the claim, or a registration it made, is refused.
 *
 * A load undone while the host is being destroyed, and not yet destroyed, tells the error sink when the platform
 * refuses to close its binary, as the destroy's own unloading of every library reports a failed unload (the
 * contract's destroy step 3); only once the host is destroyed is such a close told to nobody.
 */
WEIVEL_C_API weivel_c_status weivel_c_host_load_library(weivel_c_host *host, const char *path,
                                                        const weivel_standard_library *declaration,
                                                        weivel_c_library **library,
                                                        weivel_c_library_load_failure *failure,
                                                        weivel_c_refusal *refusal);

/**
 * Unloads a library: every later call through its bytes objects answers WEIVEL_C_STATUS_UNLOADED, every service it
 * registered is unregistered at once, and its binary is closed as soon as no call runs in it, no bytes it answered are
 * held and nothing holds any of its definitions: at once when none of them is so, else by whichever ends last.
 *
 * @return WEIVEL_C_STATUS_OK; WEIVEL_C_STATUS_NOT_UNLOADED when the platform refused to close the binary now;
 * WEIVEL_C_STATUS_NONE for a library unloaded already; WEIVEL_C_STATUS_MISUSE for NULL or a library of another host;
 * WEIVEL_C_STATUS_DESTROYED, since the destroy unloads every library itself.
 */
WEIVEL_C_API weivel_c_status weivel_c_host_unload_library(weivel_c_host *host, weivel_c_library *library);

#ifdef __cplusplus
}
#endif

#endif
