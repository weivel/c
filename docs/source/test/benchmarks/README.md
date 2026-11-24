The benchmarks: one program with a mode per gate, and a CMake script that runs one mode and holds it to its exit and output. The gates run in every build and in Release, and a timing verdict from an unoptimised build means nothing, so a Release-only gate skips elsewhere unless `WEIVEL_C_BENCHMARKS_REQUIRED` makes it fail. The `fixtures` folder holds budget files that prove a gate refuses what it must. Which gate holds what is [the benchmarks doc](../../../benchmarks.md)'s.

<!-- generated: source-children -->
- [benchmark.c](../../../../test/benchmarks/benchmark.c) - the benchmark program: the identity, control, library, same-language and definition modes, and the measuring mode
- [run.cmake](../../../../test/benchmarks/run.cmake) - runs one mode and holds it to its exit and output, skipping a Release-only gate elsewhere
- [tables.h](../../../../test/benchmarks/tables.h) - the real table, a copy that counts every crossing into the core, and the bare control call
<!-- end generated -->
