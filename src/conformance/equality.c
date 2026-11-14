#include "text.h"

#include <string.h>

static int numbers_equal(const weivel_json_value *left, const weivel_json_value *right) {
    int64_t left_whole = 0;
    int64_t right_whole = 0;
    double left_number = 0;
    double right_number = 0;
    if (weivel_json_as_int64(left, &left_whole) && weivel_json_as_int64(right, &right_whole)) {
        return left_whole == right_whole;
    }
    weivel_json_as_double(left, &left_number);
    weivel_json_as_double(right, &right_number);
    return left_number == right_number;
}

static int strings_equal(const weivel_json_value *left, const weivel_json_value *right) {
    size_t length = weivel_json_string_length(left);
    return length == weivel_json_string_length(right) &&
           memcmp(weivel_json_as_string(left), weivel_json_as_string(right), length) == 0;
}

static int arrays_equal(const weivel_json_value *left, const weivel_json_value *right) {
    size_t index;
    if (weivel_json_array_length(left) != weivel_json_array_length(right)) {
        return 0;
    }
    for (index = 0; index < weivel_json_array_length(left); index++) {
        if (!conformance_json_equal(weivel_json_array_at(left, index), weivel_json_array_at(right, index))) {
            return 0;
        }
    }
    return 1;
}

static int objects_equal(const weivel_json_value *left, const weivel_json_value *right) {
    size_t index;
    if (weivel_json_object_size(left) != weivel_json_object_size(right)) {
        return 0;
    }
    for (index = 0; index < weivel_json_object_size(left); index++) {
        const weivel_json_value *other = weivel_json_object_get(right, weivel_json_object_key_at(left, index));
        if (other == NULL || !conformance_json_equal(weivel_json_object_value_at(left, index), other)) {
            return 0;
        }
    }
    return 1;
}

int conformance_json_equal(const weivel_json_value *left, const weivel_json_value *right) {
    int left_truth = 0;
    int right_truth = 0;
    weivel_json_kind kind = weivel_json_kind_of(left);
    if (kind != weivel_json_kind_of(right)) {
        return 0;
    }
    switch (kind) {
    case WEIVEL_JSON_BOOL:
        weivel_json_as_bool(left, &left_truth);
        weivel_json_as_bool(right, &right_truth);
        return left_truth == right_truth;
    case WEIVEL_JSON_NUMBER:
        return numbers_equal(left, right);
    case WEIVEL_JSON_STRING:
        return strings_equal(left, right);
    case WEIVEL_JSON_ARRAY:
        return arrays_equal(left, right);
    case WEIVEL_JSON_OBJECT:
        return objects_equal(left, right);
    default:
        return 1;
    }
}
