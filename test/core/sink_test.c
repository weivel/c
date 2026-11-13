/**
 * @file
 * What reaches standard error: the default sink's one line for a failure, and a failing sink's last resort. Standard
 * error is pointed at a file the caller names, so the verdict is printed to standard output.
 *
 * @implNote MSVC calls the standard `freopen` and `fopen` unsafe unless told not to before any header is read.
 */

#define _CRT_SECURE_NO_WARNINGS

#include "fixture.h"

#define LINE_SIZE 512

static weivel_c_status fail_always(hearing *self, weivel_c_core *core, const weivel_c_event *event) {
    (void)self;
    (void)core;
    (void)event;
    return WEIVEL_C_STATUS_FAILED;
}

/** Makes one listener fail for one event on a core whose failures go to `sink`, the default sink when NULL. */
static void fail_one_listener(telling *sink) {
    hearing listener = {0};
    weivel_handle handle = 0;
    weivel_c_core *core = make_core(NULL, &listener, sink);
    listener.react = fail_always;
    weivel_c_core_watch_service(core, WEIVEL_HOST, "c", &handle, NULL);
    weivel_c_core_register_service(core, WEIVEL_HOST, "c", &handle, NULL);
    weivel_c_core_destroy(core);
    weivel_c_core_free(core);
}

static int read_lines(const char *path, char lines[4][LINE_SIZE]) {
    FILE *file = fopen(path, "r");
    int count = 0;
    if (file == NULL) {
        return -1;
    }
    while (count < 4 && fgets(lines[count], LINE_SIZE, file) != NULL) {
        count++;
    }
    fclose(file);
    return count;
}

static int expect_line(const char *line, const char *beginning, const char *within) {
    if (strncmp(line, beginning, strlen(beginning)) == 0 && strstr(line, within) != NULL) {
        return 1;
    }
    printf("expected a line beginning \"%s\" and holding \"%s\", got: %s", beginning, within, line);
    return 0;
}

int main(int argument_count, char **arguments) {
    static char lines[4][LINE_SIZE];
    telling failing_sink = {0};
    int count;
    int passed;
    if (argument_count != 3 || !load_tables(arguments[1])) {
        printf("usage: sink_test <core-library> <file-to-point-standard-error-at>\n");
        return 2;
    }
    if (freopen(arguments[2], "w", stderr) == NULL) {
        printf("standard error could not be pointed at %s\n", arguments[2]);
        return 2;
    }
    fail_one_listener(NULL);
    failing_sink.answer = WEIVEL_C_STATUS_FAILED;
    fail_one_listener(&failing_sink);
    fflush(stderr);
    count = read_lines(arguments[2], lines);
    passed = count == 2 && failing_sink.count == 1 &&
             expect_line(lines[0], "weivel: a listener answered a failure", "failure kind 1, status 10") &&
             expect_line(lines[1], "weivel: the error sink answered status 10", "a listener answered a failure");
    if (!passed) {
        printf("core sink: %d lines reached standard error, the failing sink was told %u times\n", count,
               failing_sink.count);
        return 1;
    }
    printf("core sink: all checks passed\n");
    return 0;
}
