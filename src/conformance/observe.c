#include "observe.h"

#include <stdlib.h>
#include <string.h>

static const char *const CODE_NAMES[] = {
    [WEIVEL_REFUSAL_NONE] = "none",
    [WEIVEL_REFUSAL_MALFORMED_CALL] = "malformed_call",
    [WEIVEL_REFUSAL_STALE_HANDLE] = "stale_handle",
    [WEIVEL_REFUSAL_MALFORMED_MANIFEST] = "malformed_manifest",
    [WEIVEL_REFUSAL_INCOMPATIBLE] = "incompatible",
    [WEIVEL_REFUSAL_NAME_TAKEN] = "name_taken",
    [WEIVEL_REFUSAL_UNMET_NEED] = "unmet_need",
    [WEIVEL_REFUSAL_REPEATED_PLUGIN] = "repeated_plugin",
    [WEIVEL_REFUSAL_PLUGIN_STATE] = "plugin_state",
    [WEIVEL_REFUSAL_IDENTITY_TAKEN] = "identity_taken",
    [WEIVEL_REFUSAL_OUTSIDE_NAMESPACE] = "outside_namespace",
    [WEIVEL_REFUSAL_SERVICE_TAKEN] = "service_taken",
    [WEIVEL_REFUSAL_UNDECLARED] = "undeclared",
    [WEIVEL_REFUSAL_WRONG_PROVIDER] = "wrong_provider",
    [WEIVEL_REFUSAL_STOPPED] = "stopped",
    [WEIVEL_REFUSAL_TIMED_OUT] = "timed_out",
    [WEIVEL_REFUSAL_UNMET_CONSTRAINT] = "unmet_constraint",
    [WEIVEL_REFUSAL_NOT_SERVED] = "not_served",
    [WEIVEL_REFUSAL_ACTIVATION_MISMATCH] = "activation_mismatch",
    [WEIVEL_REFUSAL_PROVIDED_TWICE] = "provided_twice"
};

const char *conformance_code_name(uint32_t code) {
    return code < sizeof CODE_NAMES / sizeof CODE_NAMES[0] ? CODE_NAMES[code] : NULL;
}

void conformance_write_refusal(conformance_text *out, uint32_t code, const char *subject, uint32_t has_fix) {
    const char *name = conformance_code_name(code);
    conformance_text_append(out, "{\"code\":");
    if (name != NULL) {
        conformance_text_append_quoted(out, name);
    } else {
        conformance_text_append_integer(out, code);
    }
    conformance_text_append(out, ",\"subject\":");
    if (subject != NULL) {
        conformance_text_append_quoted(out, subject);
    } else {
        conformance_text_append(out, "null");
    }
    conformance_text_append(out, has_fix ? ",\"hasFix\":true}" : ",\"hasFix\":false}");
}

static int compare_names(const void *left, const void *right) {
    return strcmp(*(const char *const *)left, *(const char *const *)right);
}

void conformance_write_names(conformance_text *out, const char **names, uint32_t count, int sorted) {
    uint32_t index;
    if (sorted && count > 1) {
        qsort((void *)names, count, sizeof *names, compare_names);
    }
    conformance_text_append(out, "[");
    for (index = 0; index < count; index++) {
        conformance_text_append(out, index == 0 ? "" : ",");
        conformance_text_append_quoted(out, names[index]);
    }
    conformance_text_append(out, "]");
}

static int is_equals_matcher(const weivel_json_value *expectation) {
    return weivel_json_kind_of(expectation) == WEIVEL_JSON_OBJECT && weivel_json_object_size(expectation) == 1 &&
           weivel_json_object_get(expectation, "equals") != NULL;
}

static const conformance_field *field_named(const conformance_field *fields, uint32_t count, const char *name) {
    uint32_t index;
    for (index = 0; index < count; index++) {
        if (strcmp(fields[index].name, name) == 0) {
            return &fields[index];
        }
    }
    return NULL;
}

int conformance_observe(conformance_world *world, const weivel_json_value *then, const conformance_field *fields,
                        uint32_t count, void *run, conformance_text *out) {
    size_t index;
    conformance_text_append(out, "{");
    for (index = 0; index < weivel_json_object_size(then); index++) {
        const char *name = weivel_json_object_key_at(then, index);
        const weivel_json_value *expectation = weivel_json_object_value_at(then, index);
        const conformance_field *field = field_named(fields, count, name);
        int wrapped = is_equals_matcher(expectation);
        if (field == NULL) {
            return conformance_world_fail(world, "the vector states a then field this driver does not witness", name);
        }
        conformance_text_append(out, index == 0 ? "" : ",");
        conformance_text_append_quoted(out, name);
        conformance_text_append(out, wrapped ? ":{\"equals\":" : ":");
        field->observe(run, expectation, out);
        conformance_text_append(out, wrapped ? "}" : "");
    }
    conformance_text_append(out, "}");
    return 1;
}
