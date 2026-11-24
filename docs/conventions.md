# Conventions

What every module of the layer keeps, whichever module it is: where its code, header and tests live, how its names
begin, what its calls answer and how its structs grow. The rules every language implementation keeps across modules
are the core's `docs/contract/cross-cutting.md`; these are C's form of them.

`src/` holds the modules every language implementation of weivel holds, as the core's `MODULES` list names them:
`core`, `handles`, `timer`, `context`, `host`, `standard`, `conformance` and `benchmarks`. A module's public header is
`include/weivel/c/<module>.h`, with any further header of the module under `include/weivel/c/<module>/`,
`include/weivel/c.h` includes them all but the optional `standard` module's and the `conformance` module's, each a
library of its own, and its tests are in `test/<module>/`. Every public name begins with `weivel_c_` and every call
answers a `weivel_c_status`. Every public struct begins with its `size` but two kinds, each saying so in its header: a
callback that is only a function and the data it is called with (the error sink, the log, the listeners, watchers
and outcome listener, the want callback, the timer observer), and the queue place inside a step, each embedded by
value or copied whole so that the struct holding it is what grows.
