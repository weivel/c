#ifndef WEIVEL_C_STANDARD_H
#define WEIVEL_C_STANDARD_H

/**
 * @file
 * The `standard` module of the weivel C language implementation: C's access to the standard's defaults, each of which
 * feeds the host. It is optional and a library of its own, weivel_c_standard (weivel_c_standard_shared for the shared
 * build), the one target of this repository that links the standard; the host never links, loads or requires it.
 *
 * The standard is a C library, so C reaches it through the same interface the core uses: by calling its functions.
 * Linking this module links the standard, and every function of the standard's headers is the caller's to call
 * directly where nothing needs translating: weivel_standard_discover lays out and discovers plugins on disk,
 * weivel_standard_read_library reads a library declaration whose answer is exactly what weivel_c_host_load_library
 * takes, with the path the caller gives, and weivel_standard_identity_parse and weivel_standard_identity_compare take
 * identities apart and order them. What this module adds is what joins the standard's answers to the host:
 *
 * - a manifest read from JSON, validated first, registered with an entry, the standard's manifest released at once
 *   because the host copies what it registers;
 * - a plugin discovery found, registered with its manifest, its identity and the entry read from the file its
 *   manifest names, by the symbol the caller names;
 * - the config, secrets and paths services, each offered as a host service under the id the standard's binary
 *   answers, as the very object the caller fills, which a plugin resolves through its context like any other.
 *
 * It holds no default of its own: no file name, format, variable or service id is written here, every one of them
 * being the standard's, read from its binary when it is needed.
 *
 * Threading: as the host's and the standard's functions it calls each allow; it keeps no state of its own.
 */

#include <stddef.h>
#include <stdint.h>

#include <weivel/c/host.h>
#include <weivel/layout.h>
#include <weivel/services.h>
#include <weivel/standard.h>

#if defined(_WIN32)
#if defined(WEIVEL_C_STANDARD_BUILDING_SHARED)
#define WEIVEL_C_STANDARD_API __declspec(dllexport)
#elif defined(WEIVEL_C_STANDARD_USING_SHARED)
#define WEIVEL_C_STANDARD_API __declspec(dllimport)
#else
#define WEIVEL_C_STANDARD_API
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define WEIVEL_C_STANDARD_API __attribute__((visibility("default")))
#else
#define WEIVEL_C_STANDARD_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Translates a status the standard answered, a weivel_status, into the layer's: WEIVEL_STATUS_OK to
 * WEIVEL_C_STATUS_OK, WEIVEL_STATUS_NONE to WEIVEL_C_STATUS_NONE, WEIVEL_STATUS_REFUSED to
 * WEIVEL_C_STATUS_REFUSED_BY_THE_STANDARD, WEIVEL_STATUS_MALFORMED to WEIVEL_C_STATUS_MISUSE, since the standard calls
 * a call malformed only for its caller's struct, WEIVEL_STATUS_OUT_OF_MEMORY to WEIVEL_C_STATUS_OUT_OF_MEMORY, and
 * any other to WEIVEL_C_STATUS_FAILED.
 */
WEIVEL_C_STANDARD_API weivel_c_status weivel_c_standard_status(uint32_t status);

/** A JSON manifest to register, and what the host's registration needs beside it. */
typedef struct weivel_c_standard_manifest_document {
    uint32_t size;
    /** The document, read only during the call; it need not be NUL-terminated. Required. */
    const char *json;
    size_t length;
    /** Handed to weivel_standard_load_manifest unchanged; NULL for every default. */
    const weivel_standard_validation_options *options;
    /**
     * The identity the plugin is registered with, NULL for none: whatever loads a plugin supplies it, never its
     * manifest. Read only during the call.
     */
    const char *identity;
} weivel_c_standard_manifest_document;

/**
 * Reads a JSON manifest with weivel_standard_load_manifest, which validates it first, and registers the manifest it
 * answers with `entry`, as weivel_c_host_register does.
 *
 * The standard's manifest is the standard's, freed only by weivel_standard_manifest_release; this releases it before
 * it returns, on every answer, since the host copies every text and array of a manifest it registers. So nothing read
 * here is left for the caller to release but what `validation` holds.
 *
 * @param document Required, with `size` covering `identity` and `json` set.
 * @param entry as weivel_c_host_register takes it. Required.
 * @param plugin receives the plugin's handle. Required; 0 on every answer but OK.
 * @param validation the caller's struct, as weivel_standard_load_manifest takes it, filled as it fills it and released
 * by the caller with weivel_standard_validation_release; NULL to be told the status alone.
 * @param refusal the core's refusal of the registration; may be NULL.
 * @return WEIVEL_C_STATUS_OK; WEIVEL_C_STATUS_REFUSED_BY_THE_STANDARD for a manifest that is not JSON or does not
 * validate, `validation` saying why, nothing registered; WEIVEL_C_STATUS_MISUSE for a NULL or too small argument, or a
 * `validation` the standard calls malformed; and every other answer of weivel_c_host_register.
 */
WEIVEL_C_STANDARD_API weivel_c_status weivel_c_standard_register_manifest(
    weivel_c_host *host, const weivel_c_standard_manifest_document *document, const weivel_c_entry *entry,
    weivel_handle *plugin, weivel_standard_validation *validation, weivel_c_refusal *refusal);

/**
 * Fills a manifest value with the manifest of a plugin weivel_standard_discover found, carrying the identity its
 * identity file gives, or none when it has no identity file, which the standard leaves to the host to give.
 *
 * Everything the manifest points at is the discovery's, valid until weivel_standard_discovery_release; the host's
 * registration copies it, so the discovery may be released as soon as every plugin of it is registered.
 *
 * @param manifest the caller's manifest with `size` set, covering at least `identity`; written only within its `size`,
 * which is kept.
 * @return WEIVEL_C_STATUS_OK; WEIVEL_C_STATUS_MISUSE for a NULL argument, a manifest too small, or a found plugin with
 * no manifest.
 */
WEIVEL_C_STANDARD_API weivel_c_status weivel_c_standard_discovered_manifest(
    const weivel_standard_discovered_plugin *found, weivel_manifest *manifest);

/**
 * Registers a plugin weivel_standard_discover found: its manifest and identity as
 * weivel_c_standard_discovered_manifest fills them, and the entry weivel_c_entry_load reads from the file its manifest
 * names, by the symbol the caller names. The standard's layout names the entry's file and no symbol, so the symbol is
 * always the caller's: this module has none of its own.
 *
 * An entry the standard did not find present, a regular file reached through no link, is never opened. The host the
 * plugin is registered with holds the entry's library from then on, as weivel_c_entry_load says; on any answer but OK
 * nothing stays loaded.
 *
 * @param entry_symbol the symbol of the weivel_c_entry_provider the entry's library exports, UTF-8. Required.
 * @param plugin receives the plugin's handle. Required; 0 on every answer but OK.
 * @param entry_failure as weivel_c_entry_load fills it; may be NULL.
 * @param refusal the core's refusal of the registration; may be NULL.
 * @return WEIVEL_C_STATUS_OK; WEIVEL_C_STATUS_NOT_A_LIBRARY, nothing opened, for a manifest that names no entry or an
 * entry the standard did not find present; every answer of weivel_c_entry_load and of weivel_c_host_register.
 */
WEIVEL_C_STANDARD_API weivel_c_status weivel_c_standard_register_discovered(
    weivel_c_host *host, const weivel_standard_discovered_plugin *found, const char *entry_symbol,
    weivel_handle *plugin, weivel_c_entry_load_failure *entry_failure, weivel_c_refusal *refusal);

/**
 * Offers the settings a host reads as a host service under the id the standard answers for config: `config` itself is
 * the object registered, so every plugin that resolves the service holds this very pointer and calls through it.
 *
 * @param config the caller's definition, whose `size` covers its first shape; the caller's until it unregisters it.
 * @param registration as weivel_c_host_register_service takes it.
 * @return WEIVEL_C_STATUS_MISUSE for a NULL `config` or one smaller than its first shape; every answer of
 * weivel_c_host_register_service.
 */
WEIVEL_C_STANDARD_API weivel_c_status weivel_c_standard_offer_config(weivel_c_host *host,
                                                                     weivel_standard_config *config,
                                                                     weivel_handle *registration,
                                                                     weivel_c_refusal *refusal);

/** Offers the secrets a host keeps under the id the standard answers for secrets, as weivel_c_standard_offer_config. */
WEIVEL_C_STANDARD_API weivel_c_status weivel_c_standard_offer_secrets(weivel_c_host *host,
                                                                      weivel_standard_secrets *secrets,
                                                                      weivel_handle *registration,
                                                                      weivel_c_refusal *refusal);

/**
 * Offers the directories a host stores files in under the id the standard answers for paths, as
 * weivel_c_standard_offer_config.
 */
WEIVEL_C_STANDARD_API weivel_c_status weivel_c_standard_offer_paths(weivel_c_host *host, weivel_standard_paths *paths,
                                                                    weivel_handle *registration,
                                                                    weivel_c_refusal *refusal);

#ifdef __cplusplus
}
#endif

#endif
