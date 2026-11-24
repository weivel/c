The `host` module. `lifetime.c` makes, destroys and frees a host; `plugins.c` registers plugins and the host's own services and answers the ledger; `activation.c`, `entry.c` and `release.c` start and stop plugins; and `libraries.c`, `library_calls.c`, `library_ids.c` and the `references` files load C libraries as host services. The host's lock is a leaf, as `host.h` states: nothing is called while it is held. What a host does is [the host's docs](../../../host/README.md)'.

<!-- generated: source-children -->
- [activation.c](../../../../src/host/activation.c) - activation: the plan, starting each plugin as a step of a dispatch, verification, quarantine and outcomes
- [entry.c](../../../../src/host/entry.c) - completions and their finish, telling outcomes, and reading an entry from a shared library and closing it
- [host.h](../../../../src/host/host.h) - what the module's sources share: the host, a plugin's record, its states, and the host's lock
- [libraries.c](../../../../src/host/libraries.c) - loading a C library from a declaration, its definitions and descriptions, unloading, and the destroy's unloads
- [libraries.h](../../../../src/host/libraries.h) - a loaded library's record, its services and their objects, which live until the host is freed
- [library_calls.c](../../../../src/host/library_calls.c) - a call through a bytes service, releasing what it answered, and closing a library's binary
- [library_ids.c](../../../../src/host/library_ids.c) - refusing a declaration's repeated id and an id a registration under the host already holds
- [lifetime.c](../../../../src/host/lifetime.c) - creating a host, its destroy as a deferred step, and freeing it
- [manifests.c](../../../../src/host/manifests.c) - copying a manifest value whole, and telling when the core must judge the caller's own instead of the copy
- [plugins.c](../../../../src/host/plugins.c) - registering a plugin, its context, the ledger, and the host's own services under `WEIVEL_HOST`
- [references.h](../../../../src/host/references.h) - the references held on a loaded C library, counted on one word without a lock
- [references_posix.c](../../../../src/host/references_posix.c) - the references on C11 atomics
- [references_windows.c](../../../../src/host/references_windows.c) - the references on the Interlocked calls
- [release.c](../../../../src/host/release.c) - a release: the deactivate, the core's release, then ending entries, forgetting and detaching as one step
<!-- end generated -->
