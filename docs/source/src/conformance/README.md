The `conformance` module, the layer suite's driver, built as its own library because it reads each vector with the standard's JSON reader and so links the standard. `driver.c` reads a vector and hands it to the file of its op, which drives its steps through the host's public surface in the world `world.c` builds; `observe.c` writes what the run showed in the shape of the vector's `then`, and `equality.c` compares the two as values. How each op is driven is [the conformance doc](../../../conformance.md)'s; what each vector asks is the kit's `docs/vectors/layer/`.

<!-- generated: source-children -->
- [actions.c](../../../../src/conformance/actions.c) - the entries a `lifecycle` vector's plugins are built with, which record and run their actions
- [consume.c](../../../../src/conformance/consume.c) - the `consume` op: plugins opened with an activate that stays pending, then the vector's steps
- [directory_posix.c](../../../../src/conformance/directory_posix.c) - walking a directory of vectors with the POSIX directory calls
- [directory_windows.c](../../../../src/conformance/directory_windows.c) - walking a directory of vectors with the Windows find calls
- [driver.c](../../../../src/conformance/driver.c) - witnessing one vector: reading it, running its op and comparing what the run showed with `then`
- [equality.c](../../../../src/conformance/equality.c) - comparing two JSON values as values, numbers by what they stand for
- [ids.c](../../../../src/conformance/ids.c) - listing every vector id under a directory at any depth
- [library.c](../../../../src/conformance/library.c) - the `library` and `description` ops: a declaration loaded through the host, holders, owners and their steps
- [lifecycle.c](../../../../src/conformance/lifecycle.c) - the `lifecycle` op's steps through the host's public surface, and what the driver reads after the last
- [lifecycle.h](../../../../src/conformance/lifecycle.h) - the `lifecycle` op's run: its entries, the trace, the pending wants and what the steps saw
- [observe.c](../../../../src/conformance/observe.c) - answering `then` in its own shape, refusal codes by their names
- [observe.h](../../../../src/conformance/observe.h) - answering a vector's `then` field by field, an unknown field a problem
- [ops.h](../../../../src/conformance/ops.h) - the ops a layer vector may have, one function each
- [text.c](../../../../src/conformance/text.c) - the growing JSON text a run is written into
- [text.h](../../../../src/conformance/text.h) - a text the driver grows as it writes JSON, read back by the standard's reader
- [time.c](../../../../src/conformance/time.c) - the driver's clock and scheduler: tasks run only when the driver moves time over them, and each timer move recorded
- [vectors.c](../../../../src/conformance/vectors.c) - reading a vector file with the standard's reader, refusing one that names itself another id
- [vectors.h](../../../../src/conformance/vectors.h) - reading the kit's layer vectors from where the caller says the kit is
- [want.c](../../../../src/conformance/want.c) - the `want` op: a consumer's want at time 0, time run to the horizon, the provider registered on the way
- [world.c](../../../../src/conformance/world.c) - the world every op runs in: the host on the real core, the counting sink and log, and the vector's plugins
- [world.h](../../../../src/conformance/world.h) - what every op runs in: one host on a clock and scheduler the driver owns, and the plugins a vector gives
<!-- end generated -->
