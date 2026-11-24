One public header per module of the layer, each the documentation of its calls beside their declarations: what each takes, answers and may be called from, the size every struct begins with, and who releases what. The conventions they share are [the conventions doc](../../../../conventions.md)'s, and what each module is for is its doc beside it in the descent tree. `core.h` decides `WEIVEL_C_API`, the export macro of the host's library, and `standard.h` decides `WEIVEL_C_STANDARD_API` for the `standard` module's, so a static, shared and consumer build of either differ in nothing else.

<!-- generated: source-children -->
- [conformance.h](../../../../../include/weivel/c/conformance.h) - the `conformance` module: witnessing a layer vector and listing a directory's vector ids
- [context.h](../../../../../include/weivel/c/context.h) - the `context` module: a plugin's view of the core, its services, wants, watches, capabilities, events, log and manifest
- [core.h](../../../../../include/weivel/c/core.h) - the `core` module: the loaded table, the wrapper, events, refusals, the error sink, the statuses and the export macro
- [handles.h](../../../../../include/weivel/c/handles.h) - the `handles` module: the table from the core's handles to the layer's objects and its reservations
- [host.h](../../../../../include/weivel/c/host.h) - the `host` module: creating a host, registering, activating, releasing, the ledger, host services, destroy and free
- [host/](host/) - the host's further headers: a plugin's entry, and C libraries as host services
- [standard.h](../../../../../include/weivel/c/standard.h) - the optional `standard` module: registering a JSON or discovered manifest, offering the standard's services, the status translation and the description check
- [timer.h](../../../../../include/weivel/c/timer.h) - the `timer` module: the caller's scheduler, the timer armed on it, and its observer
<!-- end generated -->
