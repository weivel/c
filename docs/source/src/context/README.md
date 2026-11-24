The `context` module: what a plugin sees of the core. `contexts.c` makes what the contexts of one host share and each plugin's context, `services.c` and `capabilities.c` make a plugin's calls through the wrapper, `records.c` keeps what a plugin made or was handed until it ends, and `dispatch.c` is the one listener that routes each event to the record or context it names. The locks are taken in the one order `contexts.h` states, and nothing of a plugin's is called under any of them. What a context does is [the context doc](../../../context.md)'s.

<!-- generated: source-children -->
- [capabilities.c](../../../../src/context/capabilities.c) - providing a capability implementation, listing the verified ones and watching them
- [capacity.c](../../../../src/context/capacity.c) - putting a constrained want's question to a registration's answerer and reporting the answer to the core with the host's time
- [contexts.c](../../../../src/context/contexts.c) - creating and freeing the contexts and each context, its token, listeners, log, manifest, end of entries and detach
- [contexts.h](../../../../src/context/contexts.h) - what the module's sources share: the contexts, a context, its records, and the order the locks are taken in
- [dispatch.c](../../../../src/context/dispatch.c) - the contexts' one listener: each event of the core routed through the handles table to the record or context it names
- [records.c](../../../../src/context/records.c) - the records of what a plugin made or holds: prepared before the call, kept once answered, ended exactly once
- [services.c](../../../../src/context/services.c) - a plugin's service calls: get, want and withdraw, watch and stop, the ids it may reach, register, release a holding, unregister
<!-- end generated -->
