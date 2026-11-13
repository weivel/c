## What this is

The C language implementation of weivel, for a host written in C that runs plugins written in C. It is one of the
language implementations the core's `CONTRACT.md` describes, held to that contract and to the vectors in
`weivel/conformance`, and it decides nothing itself: compatibility, order, reach, waiting and revocation are the
core's answers.

## What it is not

It is not the core, and C plugins get no special treatment for sharing the core's language. It forces no path, file
name, variable or format on its caller: the core's binary is loaded from the path the caller gives, and reading a
manifest or discovering plugins on disk is the standard's, which a host may leave out. It links no core: one build of
the layer runs against whichever core binary its caller loads.

## Modules

`src/` holds the modules every language implementation of weivel holds, as the core's `MODULES` list names them:
`core`, `handles`, `timer`, `context`, `host`, `standard`, `conformance` and `benchmarks`. A module's public header is
`include/weivel/c/<module>.h`, `include/weivel/c.h` includes them all, and its tests are in `test/<module>/`. Every
public name begins with `weivel_c_`, every public struct begins with its `size`, and every call answers a
`weivel_c_status`.

## What is built

Only the loader of the `core` module. `weivel_c_core_load` opens the core's shared binary with the platform's loader
and fills one table, `weivel_c_core_table`, with every function the core exports; a path that is no library and a
library that lacks a function are each answered as a status, the second naming the function.
`weivel_c_core_unload` closes it. The other seven modules hold only a header saying what they are for.

## Building

C11 and CMake, with no other dependency. The static library is the target `weivel_c` and the shared one
`weivel_c_shared`. The layer is compiled against the core's `weivel/core.h` alone, found under
`siblings/core/include` unless `WEIVEL_C_CORE_INCLUDE_DIRECTORY` says otherwise.

`.weivel/repo.json` pins `core`, `standard` and `conformance` by commit under `siblings/`. CI checks them out there;
locally, link or clone each checkout into `siblings/`. The tests build the pinned core as a shared library and load
it:

```
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Nothing is published: a consumer builds the layer from a pinned commit.
