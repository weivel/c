The layer's tests, one folder per module, registered by this folder's CMake when the layer is the top-level project; configuring refuses to start unless the core is checked out at `siblings/core` and the standard module is built. Each C test is a program of its own with a 60 second timeout (a benchmark gate has 300), most run against the real core's shared library, which the tests build from the pinned checkout. The tests of the `core`, `handles`, `timer`, `context` and `standard` modules, and the host's `host`, `lifecycle`, `threads`, `races` and `entry` tests, run twice, against the static library and against the shared one (`*_shared`), which proves the exports. The rest run once: the conformance suite and its coverage test, the benchmark gates, and the host tests that link the testing library to reach its seams: an allocation, a close or a registration failed on demand, or a count set by hand. The checks over the sources, the link graph and the built binaries are CMake scripts that read them and run nothing of the layer. Every C test reports through `check.h`, counting failures rather than stopping at the first. How to run them is in [the source mirror's root doc](../README.md).

<!-- generated: source-children -->
- [CMakeLists.txt](../../../test/CMakeLists.txt) - every test and the library it links, the test libraries, the checks over sources, link graph and binaries, and the benchmark gates
- [benchmarks/](benchmarks/) - the benchmark program and its gates
- [check.h](../../../test/check.h) - the `CHECK` macro every C test reports through
- [conformance/](conformance/) - the layer suite: one test per vector of the pinned kit, and its coverage check
- [context/](context/) - the `context` module's tests
- [core/](core/) - the `core` module's tests and the checks that only the wrapper calls the core
- [handles/](handles/) - the `handles` module's tests
- [host/](host/) - the `host` module's tests, the test libraries it loads, and the checks that it never reaches the standard
- [read_tokens.cmake](../../../test/read_tokens.cmake) - reading a C source as tokens and as the strings the compiler would make of it, for the checks over sources
- [standard/](standard/) - the `standard` module's tests and the check that it holds no default
- [threads.h](../../../test/threads.h) - a thread and a mutex for the tests, on the platform's own
- [timer/](timer/) - the `timer` module's tests
<!-- end generated -->
