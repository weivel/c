The `context` module's tests, against the real core, with plugins registered and opened as a host opens them and a clock the test owns. Each holds a rule of [the context doc](../../../context.md).

<!-- generated: source-children -->
- [context_test.c](../../../../test/context/context_test.c) - what a plugin sees through its context, each rule of the contract's `context` section
- [endings_test.c](../../../../test/context/endings_test.c) - how a context's records end, and every reservation a record takes given back exactly once
- [threads_test.c](../../../../test/context/threads_test.c) - plugins on several threads wanting, getting and watching each other's services
- [world.h](../../../../test/context/world.h) - what the context tests share: a counting table, a host's worth of the layer on the test's clock, and recording callbacks
<!-- end generated -->
