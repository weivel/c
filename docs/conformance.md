# `conformance`: the layer suite's driver

How the C layer sits the conformance kit's layer vectors. The core never calls a listener or runs host code, so what
a layer does around it can only be witnessed through a layer; the kit's `docs/vectors/layer/` states what each vector
asks, and the core's `docs/contract/conformance.md` what the module must do in any language.

`include/weivel/c/conformance.h` is the driver for the layer suite, its own library `weivel_c_conformance`. It reads a
layer vector of the kit with the standard's JSON reader, the one place a JSON document enters C, so it links the
standard as the `standard` module does. `weivel_c_conformance_witness_vector` reads `<directory>/<id>.json`, refuses a
vector that names itself another id, builds the vector's plugins as manifest values and entries, registers them through
the host's public surface on the real core, writing no file, drives the vector's steps, and answers what the run
showed as JSON in the shape of the vector's own `then`, which the standard's reader reads back and compares with
`then` as a value. A `then` field, an op, a given key or a step kind the driver does not know is a problem, never a
pass. `weivel_c_conformance_vector_ids` lists every vector under a directory at any depth.

- The driver owns the clock and the scheduler the host is given: a task runs only when the driver moves time over its
  due time, `fired` records each run, and a vector with `schedulerRunsBeforeRegistering` false moves the clock without
  running a task. `timer` is every move the host's timer observer was told, at the clock's time of the move.
- A `consume` vector's plugins are opened through the host's activation with an activate that stays pending, so none
  is verified before a `verify` step asks. A listener that "throws" answers a status other than OK, which goes to the
  error sink. A refusal of the run is a refusal a context call answered; a `verify` step's refusal is the check the
  host acts on.
- A `lifecycle` vector's entries record and run their actions as the kit's `docs/vectors/layer/lifecycle.md` states;
  an awaited want answers pending and goes on from the want's callback. A step "throws" when the host answers a status
  that is no answer to a well-made call (anything but OK, REFUSED, NONE and DESTROYED), and a verify is `quiet` when
  it does not and nothing reached the log or the sink meanwhile.
- A `library` vector reads its declaration with the standard's reader and loads it, with the core's echo library the
  caller names in the options' `library_path`, through the host's public load call. A declaration of neither a service
  nor a contract, which the reader refuses, is built from its `importName`, `version` and `release` alone, so the
  host's own refusal of it is what is witnessed. Its holders are opened with an
  activate that stays pending, so a `quarantine` step finishes it failed. A holder lets go of a revoked holding with
  `weivel_c_context_release_holding`, handed the `handle` its revocation named, and of a kept bytes answer with the
  object's `release_bytes`. `sameObject` is read at each get of a definition, by opening the binary again while the
  host holds it open and closing it at once, so no handle of the driver's outlives the step and the counter moves.
  Its owners are opened before its holders, each holder consuming every declared and every provided id, and each
  hears its release events through a listener of its context. `reported` counts the sink's and the log's lines from
  the first step on, the core's own log lines included. A C host's release forgets the plugin in the same call, once
  the events the core's release queued have been heard, so the `get` steps a holder makes straight after its
  `releaseHolder` are made as late calls from its listener, as it hears its release revoke what it held, and the
  `forgetHolder` that follows holds the host to having forgotten it.
- A `description` vector is driven as a `library` vector, on the standard's described test library the caller names in
  the options' `described_path`, with the host's description check set to the `standard` module's. C binds nothing of
  a description, so it witnesses the vectors directly under `description/`, which load the library and get its
  services, and none under `description/binding/`, which the kit's language list exempts it from; a step that calls an
  operation through a binding is one the driver does not drive, so it could never pass. C ships no host program, so
  the kit's `host/` vectors, which only a language with one sits, are exempt too.

`test/conformance/` holds one test per layer vector of the pinned kit, named by its id and reading the kit's file at
run time, and a coverage test that lists the kit's vector directory and fails on a vector no test names, but for those
under a directory the kit's `checks/languages.txt` exempts C from with a `c layer-exempt` line, which no test may name,
and on an exemption no vector lies under. The kit's location comes from the build: `siblings/conformance`, or
`WEIVEL_C_KIT_DIRECTORY`. The described library the `description` vectors load is built in the test build from the
pinned standard's `test/described` sources with the standard's own `weivel_describe`, each description naming its
definition with `ANSWERED_BY` as the standard's own build of it does, as `echo` is built from the core's sources,
since the standard defines that target only as a project of its own.
