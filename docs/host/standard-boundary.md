# The host never reaches the standard

The standard is replaceable and a host may leave it out, so the host must work with none of the standard's binary
present: it may use the standard's published declaration types, and nothing else of it. The two modules that do link
the standard are the [`standard`](../standard.md) and [`conformance`](../conformance.md) modules, each a library of
its own.

The host includes the standard's `weivel/library.h` for its types and never links, loads or calls the standard's
binary, and three tests read that from three sides. One reads every source outside the `standard` and `conformance`
modules as the strings the compiler would make of it, adjacent literals joined, an identifier stringized onto them and
a run of character literals read as one string, and fails on a name of a function the standard exports (also without
its `weivel_`), a string spelling one, the stem `weivel_standard`, the start of that stem at a string's end, the end of
a function's name at a string's start, a name of weivel's around a format conversion, a `#pragma comment(lib, ...)`
naming the standard, or a token pasted onto a name of weivel's. One reads the link graph the build writes once the
whole project is configured, every directory at any depth and imported targets included, and fails when a target of
this repository reaches a link item that names the standard, read part by part with generator expressions, paths and
extensions taken off, but for those it names exactly: the `standard` and `conformance` modules' libraries and their
tests. One reads the imports of every host binary built, the shared library and each host test executable, and fails
when one imports the standard's library or the module's, or a function of either. A name assembled at run time from
pieces none of which these readings recognise, and the standard linked statically by a path no link item shows, are
held by review alone. The tests need CMake 3.21 or later.
