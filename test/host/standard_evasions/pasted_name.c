#define STANDARD_FUNCTION(name) weivel_standard_##name

void call_through_a_paste(void) {
    STANDARD_FUNCTION(library_release)(0);
}
