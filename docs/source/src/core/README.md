The `core` module: the only place the layer names a function of the core. `load.c` fills the table from the core's binary, `calls.c` holds one function per call of the core, and `wrapper.c` is the one wrapper they all go through, so one build of the layer runs against whichever core its caller loads; two tests, `only_the_wrapper_calls.cmake` and `table_covers_the_core.cmake` ([the core tests](../../test/core/README.md)), hold it to that. What the wrapper guarantees is [the core doc](../../../core.md)'s.

<!-- generated: source-children -->
- [binary.h](../../../../src/core/binary.h) - opening a shared library, finding a function in it and closing it, with the platform's loader
- [binary_posix.c](../../../../src/core/binary_posix.c) - the binary calls on `dlopen`, `dlsym` and `dlclose`
- [binary_windows.c](../../../../src/core/binary_windows.c) - the binary calls on `LoadLibraryW` with a UTF-8 path, `GetProcAddress` and `FreeLibrary`
- [calls.c](../../../../src/core/calls.c) - every call of the core as one function naming one field of the table, handed to the wrapper
- [calls.h](../../../../src/core/calls.h) - the calls that answer a handle, each with the step that records the handle before any event naming it is heard
- [events.c](../../../../src/core/events.c) - copying an event and a refusal into the layer's own, a fixed sentence where memory for a text ran out
- [events.h](../../../../src/core/events.h) - a drained event waiting to be dispatched, its queue place and its deadline fence
- [functions.h](../../../../src/core/functions.h) - every function the core exports, in the order of the table's fields
- [load.c](../../../../src/core/load.c) - loading the core's binary into the table, naming the function a library lacks
- [load.h](../../../../src/core/load.h) - whether a table is large enough to hold every function of its first version
- [mutex.h](../../../../src/core/mutex.h) - the layer's non-recursive lock
- [mutex_posix.c](../../../../src/core/mutex_posix.c) - the lock on a pthread mutex
- [mutex_windows.c](../../../../src/core/mutex_windows.c) - the lock on an SRW lock
- [wrapper.c](../../../../src/core/wrapper.c) - the wrapper: the lock, the drain, refusals, the dispatch queue and deferred steps, the sink and the deadline hooks
- [wrapper.h](../../../../src/core/wrapper.h) - what the layer's other sources reach of the wrapper
<!-- end generated -->
