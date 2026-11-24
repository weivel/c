The layer suite: one test per layer vector of the pinned kit, named by its id, in one table the runner runs and the coverage test reads. Each reads its vector from the kit at run time, so the suite follows the kit commit `repo.json` pins. How the suite and its coverage check work is [the conformance doc](../../../conformance.md)'s.

<!-- generated: source-children -->
- [coverage_test.c](../../../../test/conformance/coverage_test.c) - fails on a vector of the kit no test names, and on a test or exemption the kit's language list does not allow
- [layer_vectors.c](../../../../test/conformance/layer_vectors.c) - the table of tests, one per vector, each witnessing it and holding the run to `then`
- [layer_vectors.h](../../../../test/conformance/layer_vectors.h) - the suite's table, shared by the runner and the coverage test
- [layer_vectors_test.c](../../../../test/conformance/layer_vectors_test.c) - runs every test of the table against the real core, the echo library and the described library
<!-- end generated -->
