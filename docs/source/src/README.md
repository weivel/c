The implementation, one folder per module, the modules [the conventions](../../conventions.md) name. A header here is internal: the shared library exports nothing it declares, and a consumer reaches the layer through the public headers alone. Platform-specific code is in files named `_posix.c` and `_windows.c`, of which the build compiles one.

<!-- generated: source-children -->
- [benchmarks/](benchmarks/) - the `benchmarks` library: timing, the budgets, and the worlds each measured call runs in
- [conformance/](conformance/) - the `conformance` library: the layer suite's driver, one file per op
- [context/](context/) - the `context` module: contexts, the records of what a plugin made or holds, and the one listener that routes events
- [core/](core/) - the `core` module: the loader, the wrapper, the core's calls, and event and refusal copies
- [handles/](handles/) - the `handles` module: the handle table
- [host/](host/) - the `host` module: plugins, entries, activation, release, the host's life, and C libraries as host services
- [standard/](standard/) - the optional `standard` module: what joins the standard's answers to the host
- [timer/](timer/) - the `timer` module: the timer armed on the caller's scheduler
<!-- end generated -->
