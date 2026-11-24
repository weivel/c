The `benchmarks` module, built as its own library and linked by its tests alone; it has no public header and exports nothing. Each measured call is built in a world of its own through the layer's public surface on a table its caller loaded, and timed by `measure.c` in batches sampled in turn. What it measures, how it samples and the bounds it is judged by are [the benchmarks doc](../../../benchmarks.md)'s.

<!-- generated: source-children -->
- [budget.c](../../../../src/benchmarks/budget.c) - reading the kit's budget files and judging a run against a ratio bound or its platform's noise bounds
- [budget.h](../../../../src/benchmarks/budget.h) - the bounds the module gates against, every one the kit's, and the calls that read and apply them
- [control.c](../../../../src/benchmarks/control.c) - the control call's world: the core's `weivel_get_service` made through the wrapper
- [definition.c](../../../../src/benchmarks/definition.c) - the library call's definition row: an operation called through the definition the host's own context resolved
- [library.c](../../../../src/benchmarks/library.c) - the library call's world: the echo library's function through the host's service object and called directly
- [measure.c](../../../../src/benchmarks/measure.c) - the clock and the sampling: warm-up, batches in turn one per path a round, and the summaries
- [measure.h](../../../../src/benchmarks/measure.h) - how a call is timed and two paths compared, round by round
- [pairs.c](../../../../src/benchmarks/pairs.c) - measuring a pair of paths and the same-language triple
- [pairs.h](../../../../src/benchmarks/pairs.h) - the comparisons the gates judge: a measured path against its reference, and the same-language call against itself twice
- [same_language.c](../../../../src/benchmarks/same_language.c) - the same-language world: a consumer plugin calling the object another plugin's service resolved to
- [worlds.h](../../../../src/benchmarks/worlds.h) - what the module measures, each world built through the layer's public surface on the caller's table
<!-- end generated -->
