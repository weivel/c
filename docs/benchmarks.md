# `benchmarks`: the layer's half of the platform's benchmarks

What the C layer measures and the gates that hold it to bounds. A call through the layer must cost next to nothing
beyond the same call made without it, so the layer's overhead is measured on every runner and judged against bounds
the kit owns, never the layer, which could otherwise loosen its own; the kit's `docs/benchmarks.md` states the rows
and the budget files, and the core's `docs/contract/benchmarks.md` what the module must do in any language.

`src/benchmarks/` is a library of its own, `weivel_c_benchmarks`, linked by its tests alone; it has no public header.
It times a call in batches, the paths compared sampled in turn one batch each per round, and builds what it measures
through the layer's public surface on a table its caller loaded:

- the control call through the wrapper, the core's `weivel_get_service` for a plugin made as
  `weivel_c_core_get_service`, the very call the kit's `control_call.c` times, beside the same call made through the
  bare table;
- a C library's function called through the object the host registers for it, beside the same function called
  directly through the platform's loader, each releasing what it answered;
- a same-language call: a consumer plugin resolves another plugin's service through its context and calls the object
  it was handed, beside the same call made twice on the provider's own object, so the run's own noise is measured
  with it;
- the library call's definition row: an operation of the echo library's definition called through the definition the
  host's own context resolved, beside the same operation called twice through the pointer the definition's symbol
  answered, each handed the definition itself and reading the operation's function pointer out of the struct on every
  call. The two are one pointer, so the row is judged by its platform's noise bounds, as the same-language call is,
  and fails when the host resolved anything else. It is sampled in more and shorter rounds than the same-language
  call, as the kit's `docs/benchmarks.md` lets a language: 2,000,000 warm-up calls, then 3,001 rounds of 100,000
  calls each, where the same-language call takes 301 rounds of 1,000,000, since a call into another binary spreads
  more within a long batch.

`test/benchmarks/` holds the gates. In every build: the identity gate, a consumer handed the provider's very pointer
by `weivel_c_context_get_service` and a thousand calls through it crossing into the core not once on a table that
counts every crossing a resolve or a call of the layer's makes, refused for a resolver that copies the object; the
control call refused against a budget of 1 ns; and both ratio gates and the same-language gate failing on a platform
the budget gives no line, before anything is measured, never passing unbounded. In a Release build, labelled
`release-benchmark`, which CI's `benchmarks` job builds and runs on all three runners, configured with
`WEIVEL_C_BENCHMARKS_REQUIRED` so that a gate which would skip outside a Release build fails there instead: the
control call through the wrapper against the core's own budget, `benchmarks/budget.txt` of the pinned kit; the two
ratio gates against `benchmarks/c.txt` of the pinned kit, each round's measured path over its reference, the
per-round median held to the highest ratio measured on that runner times the margin; the same-language call against
that runner's noise bounds in the same file, failing a run too noisy to judge, and the library call's definition row
against the same bounds; and a measuring mode that prints every measurement the bounds are set from. Every bound is
the kit's; the layer sets none, and the commit that adds `c.txt` to the kit lists the samples.
