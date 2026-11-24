The `core` module's tests, against the real core's binary, and two checks that read the sources: `only_the_wrapper_calls.cmake` fails when a source of the layer could reach a function of the core anywhere but where the wrapper does, with the files of `evasions/` as the ways it must still catch, and `table_covers_the_core.cmake` fails when the pinned core's header declares a function the table lacks. What the wrapper guarantees is [the core doc](../../../core.md)'s.

<!-- generated: source-children -->
- [calls_test.c](../../../../test/core/calls_test.c) - every call of the core made once through the wrapper, and what each refuses
- [evasions/](evasions/) - sources that reach the core around the wrapper, each one the check must catch
- [fixture.h](../../../../test/core/fixture.h) - what the wrapper's tests share: a counting table that can bend a call, a recording listener and sink, plugins in a line
- [load_test.c](../../../../test/core/load_test.c) - the loader against the real core, a library that lacks a function and a file that is no library
- [only_the_wrapper_calls.cmake](../../../../test/core/only_the_wrapper_calls.cmake) - fails when a source of the layer reaches a function of the core anywhere but where the wrapper does
- [partial_core.c](../../../../test/core/partial_core.c) - a shared library exporting only the core's first two functions, so a load of it fails naming the third
- [sink_test.c](../../../../test/core/sink_test.c) - what reaches standard error: the default sink's line and a failing sink's last resort
- [table_covers_the_core.cmake](../../../../test/core/table_covers_the_core.cmake) - fails when the core's header and the layer's list of functions differ
- [threads_test.c](../../../../test/core/threads_test.c) - calls from several threads: each event dispatched once and in order, each step run once, each refusal its own
- [wrapper_test.c](../../../../test/core/wrapper_test.c) - the wrapper's rules: drain and dispatch order, refusals, deferred steps, events short of memory, the sink and the deadline hooks
<!-- end generated -->
