The C language implementation of weivel, for a host written in C that runs plugins written in C. It is one of the
language implementations the core's `docs/contract/` describes, held to that contract and to the vectors of the
conformance kit, and it decides nothing itself: compatibility, order, reach, waiting and revocation are the core's
answers, made once in the core so that a C plugin meets the very decisions a plugin in any other language meets. It is
not the core, and C plugins get no special treatment for sharing the core's language. It forces no path, file name,
variable or format on its caller: the core's binary is loaded from the path the caller gives, and reading a manifest
or discovering plugins on disk is the standard's, which a host may leave out. It links no core: one build of the
layer runs against whichever core binary its caller loads. Its modules stand on one another in one direction. The
`core` module loads the core's binary and owns the one wrapper every call into it goes through; the `handles` table
keeps the layer's object for each handle the core answers; the `timer` arms the core's next deadline on the caller's
scheduler; the `context` module routes each event the wrapper drains to the plugin it names and is all a plugin sees
of the core; and the `host` builds those four for one host and registers, activates and releases plugins and loads C
libraries as host services. Beside the host stand three libraries of their own: the optional `standard` module and the
`conformance` module, the only two that link the standard, and the `benchmarks` module, linked by its tests alone. How
to build the layer and run its tests is answered in [the source mirror's root doc](source/README.md); the layer is a
library and ships no program, so there is nothing of its own to run.

- [conventions.md](conventions.md) - what every module keeps: where its header and tests live, the `weivel_c_` names, statuses and size-prefixed structs
- [core.md](core.md) - `core`: loading the core's binary, and the one wrapper every call goes through with its lock, drain, refusals, dispatch, sink and deadline hooks
- [handles.md](handles.md) - `handles`: the table from the core's handles to the layer's objects, and what a plugin holds of another's
- [timer.md](timer.md) - `timer`: arming the core's deadline on the caller's scheduler, and the observer that hears each move
- [context.md](context.md) - `context`: what a plugin reaches through its context, and how its entries end and the context detaches
- [host/](host/) - `host`: a host's life, a plugin's entry in C, C libraries as host services, and how the host is kept from the standard
- [standard.md](standard.md) - `standard`, optional and its own library: what joins the standard's answers to the host, and why it holds no default
- [conformance.md](conformance.md) - `conformance`: the driver that sits the kit's layer vectors, op by op, and the test per vector
- [benchmarks.md](benchmarks.md) - `benchmarks`: what the layer measures and the gates that hold it to the kit's bounds
- [source/](source/) - the source mirror: one doc per source folder that holds code, and at its root how to build and test the layer
