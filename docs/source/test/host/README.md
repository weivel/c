The `host` module's tests against the real core, the shared libraries they load as plugins and host services, and the three checks that the host never reaches the standard ([why and how](../../../host/standard-boundary.md)): `calls_nothing_of_the_standard.cmake` over sources and the link graph `link_graph.cmake` writes, with the files of `standard_evasions/` as what it must catch, and `imports_nothing_of_the_standard.cmake` over the built binaries. The threaded tests hold a thread still at a gate, and the tests that need a close or a registration to fail link the testing library.

<!-- generated: source-children -->
- [calls_nothing_of_the_standard.cmake](../../../../test/host/calls_nothing_of_the_standard.cmake) - fails when a source or a link of the host could reach the standard's binary
- [check_a_link_graph_fixture.cmake](../../../../test/host/check_a_link_graph_fixture.cmake) - configures the fixture project in one scenario and runs the check on the graph it writes
- [definition_test.c](../../../../test/host/definition_test.c) - definition services and the registrations that await their releases
- [description_test.c](../../../../test/host/description_test.c) - descriptions a C library declares: checked once at the load and bound to nothing
- [entry_closing_test.c](../../../../test/host/entry_closing_test.c) - the library an entry was read from closes at the free and never before
- [entry_library.c](../../../../test/host/entry_library.c) - a plugin written in C built as a shared library, and a symbol that provides no entry
- [entry_library.h](../../../../test/host/entry_library.h) - what the test plugin library keeps, reached through the entry's `data`
- [entry_test.c](../../../../test/host/entry_test.c) - an entry read from a shared library by path and symbol, registered, activated and released
- [gates.h](../../../../test/host/gates.h) - a flag one thread raises and another waits on
- [host_test.c](../../../../test/host/host_test.c) - registration, the host's own services, and activation with each of its five outcomes
- [imports_nothing_of_the_standard.cmake](../../../../test/host/imports_nothing_of_the_standard.cmake) - fails when a host binary imports the standard's library or the module's, or a function of either
- [libraries.h](../../../../test/host/libraries.h) - the declarations of the test service library and the echo library, and the testing build's close counters
- [library_loading_test.c](../../../../test/host/library_loading_test.c) - a library load that does not finish, and the declarations a load refuses
- [library_test.c](../../../../test/host/library_test.c) - C libraries as host services: loading, calls, answers, releases, unloading and refused declarations
- [library_threads_test.c](../../../../test/host/library_threads_test.c) - calls through a library's service from several threads while another unloads it
- [lifecycle_test.c](../../../../test/host/lifecycle_test.c) - release and destroy step by step, at the top level and deferred inside a dispatch
- [link_graph.cmake](../../../../test/host/link_graph.cmake) - writes the link graph of the whole project once it is configured
- [plugins.h](../../../../test/host/plugins.h) - the entry functions the host tests build their plugins from, each recording what it does
- [races_test.c](../../../../test/host/races_test.c) - calls on the host raced at the one moment that matters, each thread held at a gate
- [references_test.c](../../../../test/host/references_test.c) - the count of references on a loaded library: taken, full, closed, and the last drop
- [seams_test.c](../../../../test/host/seams_test.c) - the host held inside an allocation, or an allocation failed, at the step the test names
- [service_library.c](../../../../test/host/service_library.c) - a C library of the bytes kind that tells the test what crossed
- [service_library.h](../../../../test/host/service_library.h) - what the test service library and its tests share
- [standard_evasions/](standard_evasions/) - sources and a project that reach the standard, each one the check must catch
- [threads_test.c](../../../../test/host/threads_test.c) - activates finishing later from several threads while another releases their plugins
- [world.h](../../../../test/host/world.h) - what the host tests share: a host on a counting table and the test's clock, a recording sink, log, trace and outcome listener
<!-- end generated -->
