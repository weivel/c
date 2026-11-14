#include <weivel/c/standard.h>

#include <stddef.h>
#include <string.h>

#define END_OF(type, field) (offsetof(type, field) + sizeof(((type *)0)->field))
#define FIRST_DOCUMENT_SIZE END_OF(weivel_c_standard_manifest_document, identity)
#define IDENTIFIED_MANIFEST_SIZE END_OF(weivel_manifest, identity)

/**
 * Copies the standard's view of a manifest into the caller's, within the caller's `size`, which is kept.
 *
 * @implNote The view carries the size of the core header the standard was built with, which may be older or newer
 * than the caller's, so only the bytes both cover are copied and the rest of the caller's struct is zero.
 */
static void copy_view(const weivel_manifest *view, weivel_manifest *manifest) {
    uint32_t size = manifest->size;
    size_t covered = view->size < size ? view->size : size;
    memset(manifest, 0, size);
    memcpy(manifest, view, covered);
    manifest->size = size;
}

static weivel_c_status register_read(weivel_c_host *host, const weivel_standard_manifest *read,
                                     const char *identity, const weivel_c_entry *entry, weivel_handle *plugin,
                                     weivel_c_refusal *refusal) {
    weivel_manifest manifest = WEIVEL_INIT(weivel_manifest);
    copy_view(weivel_standard_manifest_view(read), &manifest);
    if (identity != NULL) {
        manifest.identity = identity;
    }
    return weivel_c_host_register(host, &manifest, entry, plugin, refusal);
}

static int document_is_usable(const weivel_c_standard_manifest_document *document) {
    return document != NULL && document->size >= FIRST_DOCUMENT_SIZE && document->json != NULL;
}

weivel_c_status weivel_c_standard_register_manifest(weivel_c_host *host,
                                                    const weivel_c_standard_manifest_document *document,
                                                    const weivel_c_entry *entry, weivel_handle *plugin,
                                                    weivel_standard_validation *validation,
                                                    weivel_c_refusal *refusal) {
    weivel_standard_validation own = WEIVEL_INIT(weivel_standard_validation);
    weivel_standard_validation *answered = validation == NULL ? &own : validation;
    weivel_standard_manifest *read = NULL;
    weivel_c_status status;
    if (plugin == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    *plugin = 0;
    if (!document_is_usable(document)) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    status = weivel_c_standard_status(weivel_standard_load_manifest(document->json, document->length,
                                                                    document->options, &read, answered));
    weivel_standard_validation_release(&own);
    if (status != WEIVEL_C_STATUS_OK) {
        return status;
    }
    status = register_read(host, read, document->identity, entry, plugin, refusal);
    weivel_standard_manifest_release(read);
    return status;
}

weivel_c_status weivel_c_standard_discovered_manifest(const weivel_standard_discovered_plugin *found,
                                                      weivel_manifest *manifest) {
    const weivel_manifest *view;
    if (found == NULL || manifest == NULL || manifest->size < IDENTIFIED_MANIFEST_SIZE) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    view = weivel_standard_manifest_view(found->manifest);
    if (view == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    copy_view(view, manifest);
    manifest->identity = found->identity_id;
    return WEIVEL_C_STATUS_OK;
}

static int entry_is_present(const weivel_standard_discovered_plugin *found) {
    return found->entry_path != NULL && found->entry_present;
}

weivel_c_status weivel_c_standard_register_discovered(weivel_c_host *host,
                                                      const weivel_standard_discovered_plugin *found,
                                                      const char *entry_symbol, weivel_handle *plugin,
                                                      weivel_c_entry_load_failure *entry_failure,
                                                      weivel_c_refusal *refusal) {
    weivel_manifest manifest = WEIVEL_INIT(weivel_manifest);
    weivel_c_entry entry = WEIVEL_INIT(weivel_c_entry);
    weivel_c_status status;
    if (plugin == NULL) {
        return WEIVEL_C_STATUS_MISUSE;
    }
    *plugin = 0;
    status = weivel_c_standard_discovered_manifest(found, &manifest);
    if (status != WEIVEL_C_STATUS_OK) {
        return status;
    }
    if (!entry_is_present(found)) {
        return WEIVEL_C_STATUS_NOT_A_LIBRARY;
    }
    status = weivel_c_entry_load(found->entry_path, entry_symbol, &entry, entry_failure);
    if (status != WEIVEL_C_STATUS_OK) {
        return status;
    }
    status = weivel_c_host_register(host, &manifest, &entry, plugin, refusal);
    if (status != WEIVEL_C_STATUS_OK) {
        weivel_c_entry_unload(&entry);
    }
    return status;
}
