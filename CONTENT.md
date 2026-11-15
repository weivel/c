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
`include/weivel/c/<module>.h`, with any further header of the module under `include/weivel/c/<module>/`,
`include/weivel/c.h` includes them all but the optional `standard` module's and the `conformance` module's, each a
library of its own, and its tests are in `test/<module>/`. Every public name begins with `weivel_c_` and every call
answers a `weivel_c_status`. Every public struct begins with its `size` but two kinds, each saying so in its header: a
callback that is only a function and the data it is called with (the error sink, the log, the listeners, watchers
and outcome listener, the want callback, the timer observer), and the queue place inside a step, each embedded by value or copied whole
so that the struct holding it is what grows.

## What is built

Every module: `core`, `handles`, `timer`, `context`, `host`, `standard`, `conformance` and `benchmarks`.

`weivel_c_core_load` opens the core's shared binary with the platform's loader and fills one table,
`weivel_c_core_table`, with every function the core exports; a path that is no library and a library that lacks a
function are each answered as a status, the second naming the function. `weivel_c_core_unload` closes it.

`weivel_c_core_create` makes a core through a loaded table and the one wrapper every call into it goes through, and
each call of the core has a `weivel_c_core_` function of the same name that goes through that wrapper:

- a call is made under one lock and its events are drained under the same hold, each copied into the layer's own
  `weivel_c_event` and released in the core before the next is drained;
- a refused call is answered with the refusal the core queued for it, as a `weivel_c_refusal` its caller releases, and
  with the status alone where the core queued none;
- events are dispatched to the listeners first in, first out, outside the lock, by one thread at a time; a call made
  while a dispatch runs, from a listener or from another thread, joins the end of the queue, and `weivel_c_core_defer`
  puts a step there;
- an event is heard even when memory ran out for its texts: it is then marked `incomplete`, every handle, code and
  number exact and a fixed sentence in place of each text, and only an event with no memory at all is dropped;
- what a listener, a step or a hook answers as a failure goes to the `weivel_c_error_sink` given at creation, which
  writes a line to standard error when none is given;
- nothing of the caller's is called under the lock: listeners, steps, the sink and the deadline hooks all run outside
  it and may call in;
- the wrapper records each `WEIVEL_EVENT_DEADLINE` as it drains it. `weivel_c_core_set_deadline_hooks` gives it a
  clock, read before a call only while a deadline is recorded so a due deadline reaches the core first, and a
  `follow` hook, told the latest deadline after the lock is let go, by one thread at a time; no event of a call that
  drained an announcement is dispatched, by any thread, before `follow` has returned with it, and a `follow` telling
  a deadline no call drained holds back nothing.

`weivel_c_handles_create` makes the table from the core's handles to the layer's own objects: an entry per plugin,
service registration, capability implementation, watch and want, made when the core answers the handle and ended
when what it stands for ends. A handle the table does not hold answers nothing. What a plugin holds of another's is
kept by the pair of handles and dropped by the `WEIVEL_EVENT_REVOKED` that names it, and each entry has a token that
reaches it without a search, which is what a plugin is opened with as its payload. The table has its own lock and
calls nothing, so any thread may use it. `weivel_c_handles_reserve` takes the room of an addition ahead of time, so
the entry for a handle is made, once the core has answered it, by a step that cannot fail, and the core never holds
an object whose handle the table lacks.

`weivel_c_timer_create` arms the core's deadline on the caller's `weivel_c_scheduler`: a clock, `now`, and
`schedule` and `cancel` for one task. The layer owns no thread and no timer. The task is scheduled from the latest
deadline drained and from nothing else, replaced when the deadline moves and cancelled when none is left; when it
runs it hands the core the clock's time, and when the scheduler says it cannot wait the whole way (`maximum_wait`)
it waits as long as it may and arms again. While a deadline is armed the wrapper reads the clock before each call
and advances the core first when the deadline has passed, so a registration at or after a want's deadline settles
nothing even when the task has not run. `weivel_c_timer_destroy` cancels the task, so nothing of the layer's fires
once its core is gone. A `weivel_c_timer_observer` given at creation hears each move the timer makes, the deadline it
arms or that it disarms, outside every lock, before any event of the call that drained the announcement is
dispatched; arming again for the deadline already armed is no move. A timer made without a scheduler arms nothing and
reads no clock, and `weivel_c_timer_now`, where the layer takes the time for a want that needs it, then answers the
caller's mistake.

`weivel_c_contexts_create` makes what the contexts of one host share: the core, the handles table, the timer and
the host's log, behind one listener that routes each event of the core to the plugin it names. The host builds a
`weivel_c_context` for each plugin it opens, from the plugin's handle and its manifest value, which the context
borrows, and opens the plugin with the context's token as its payload. Through its context a plugin:

- gets a service registered now, wants one, watches one, lists the ids it may reach and registers its own. A service
  is a `void *`: a get, a settled want and a watch hearing a registration answer the very pointer the provider
  registered, so calling it is a plain call in C that crosses into nothing;
- gives a callback for whatever comes later. A want settles exactly once through its callback, with the provider's
  pointer or the core's refusal, a want that could be settled or had to be rejected at once included. It waits
  without bound unless its options ask otherwise, and a timeout, a budget and a poll interval reach the core
  unchanged with the host's time; on a host without a clock such a want is the caller's mistake and the core is not
  asked;
- may give an answerer when it registers a service, which is put each question a constrained want asks and whose
  answer is reported to the core. A registration without one answers at once: nothing live, and that it serves;
- provides an implementation of a capability, lists the verified ones and watches them join and leave;
- hears what the core reports about it: which object it holds was revoked, a refusal of reach, a want that settled
  degraded;
- logs to the host's log, and reads its manifest value.

A callback that answers a failure goes to the error sink and silences nothing, and no callback runs under a lock of
the layer's, so each may call back into its context. A context decides nothing and keeps no answer of the core's.
Once a plugin is released or quarantined its callbacks still hear what that call queued; its entries end when the
host calls `weivel_c_context_end_entries`, from a step it defers behind those events, and a call the plugin still
makes is answered as the core answers a late call. Once the core has forgotten the plugin, the host detaches its
context with `weivel_c_context_detach`, in the same hold of the layer lock as the forget: the plugin's handle leaves
the table, so the core may issue it again, and a call through the context answers `WEIVEL_C_STATUS_DETACHED` without
reaching the core, decided under that lock as the call is made, since the handle may by then name another plugin. An
event is routed to a context only when it was drained after that context was made, so an event naming an earlier
holder of the same handle reaches nobody.

### A plugin's entry in C

A plugin written in C hands its host one `weivel_c_entry` (`include/weivel/c/host/entry.h`): a size-prefixed struct
holding `activate`, an optional `deactivate` and the plugin's own `data`. Each is a `weivel_c_entry_function`, called
with the plugin's context and a `weivel_c_completion`, and answers at once `WEIVEL_C_ENTRY_DONE`,
`WEIVEL_C_ENTRY_FAILED` with an English reason, or `WEIVEL_C_ENTRY_PENDING`. A pending one finishes later by calling
`weivel_c_completion_finish` exactly once, from any thread. A finish that comes twice, after the plugin was released,
or after the host was destroyed is answered as a value, and a completion stays valid until the host is freed.

`weivel_c_entry_load` reads an entry from a shared library at a path its caller gives, through a
`weivel_c_entry_provider` the library exports under a symbol name its caller gives; the layer has no default name for
either. The host closes that library only when it is freed: code of the plugin's may still run after its release or
the destroy has finished, a finish it makes from inside the library on its own thread among it, and only the free
comes after every call into the layer has returned.

### The host

`weivel_c_host_create` makes a host from a loaded core table or the path of the core's binary, the core's options,
the caller's scheduler, an error sink and a log, and builds the wrapper's core, the handles table, the timer and the
contexts. It never links or loads the standard: `weivel_c_host_register` takes a manifest value, which it copies
whole, so its caller may free its own at once, and an entry. A manifest, or a list in it, below its first shape or
declaring two strides is handed to the core as the caller built it, so the host answers it exactly as the core does.

- `weivel_c_host_ledger` and `weivel_c_host_ledger_row` answer the core's ledger, every plugin's row as the core
  keeps it, and the `timer_observer` of the host's options hears each move of its timer. The host hands its caller
  nothing of the wrapper's dispatch, clock or deadline hooks: its core and its timer are its own, so nothing but the
  layer drains the core or moves the timer.
- A host given no scheduler arms none, rather than a timer of the layer's: the layer owns no thread and no clock. A
  want that needs the time, one with a timeout or with constraints, is then the caller's mistake, answered
  `WEIVEL_C_STATUS_MISUSE` with nothing asked of the core, and the observer hears nothing.
- `weivel_c_host_register_service` registers a service of the host's own under `WEIVEL_HOST`, offered to every plan
  as provided by the host and answered to a plugin as the very pointer given.
- `weivel_c_host_activate` asks the core for the plan with the edge source its caller chooses, answers the plan at
  once, and starts each plugin of the plan's order in that order as one step of a dispatch: it opens the plugin with
  its context's token as payload and calls its activate, never waiting for one before starting the next. Where no
  dispatch runs, the activating thread starts the plan as the dispatcher, so what an activate's own calls queue is
  heard once every activate of the plan has been called; where another thread dispatches, the plan is started as a
  call made while that dispatch runs, and those events join the end of its queue. Either way an activate that
  settles before it returns is verified, and quarantined if the core refuses, before the next plugin is started. An
  activate may therefore run as the dispatcher, so it must not block on an event another thread causes: one that
  must wait for such an event answers `WEIVEL_C_ENTRY_PENDING` and finishes later. Each started plugin's outcome
  reaches the caller's listener once, from the dispatch: `active`, `quarantined` (its activate failed, the reason and
  a fix in its ledger row, or the core refused its verification, quarantined before any event of the verification is
  dispatched and before the next plugin is started), `refused` (the core refused the open),
  `released` (released while its activate was in flight, told as that release begins, or kept from its activate
  because the host's destroy had begun) or `failed` (an open that failed for no refusal, told to the sink, the plugin
  released and forgotten at once when the core held it open). Every completion is made before the first open, so an
  activation short of memory starts nothing, and the plugins are started on the calling thread exactly once whatever
  other thread dispatches meanwhile. An outcome carries the refusal that decided it: a `refused` one the core's
  refusal of the open, a `quarantined` one the core's refusal of the verification when that refusal quarantined it,
  and every other none, a quarantine for a reason of the host's among them. A verification the caller asks for, or
  one an activate finishing later sets off, is the same one step with its quarantine where no dispatch runs.
- `weivel_c_host_release` calls the deactivate while the plugin is whole, releases it in the core, lets that
  release's events reach the plugin, then ends its entries, forgets it and detaches its context, those last three as
  one step queued in the release's own hold of the layer lock behind its events, the detach made in the forget's own
  hold. A quarantine queues the ending of its plugin's entries the same way. Where no dispatch runs, the release has
  finished when it returns. A deactivate that answers pending holds the core's release and everything after it until
  it finishes, however long.
- `weivel_c_host_destroy` releases every plugin the host opened and has not released, the last opened first, one in
  flight and one quarantined included; lets every event and step those releases left run; unloads every C library it
  loaded, telling the sink of one whose binary does not close and unloading the rest; then disarms the timer and
  destroys the core. It waits for every open, settling and activation begun while the host ran. It too is a deferred
  step, and finishes where no dispatch runs before it returns, unless something it waits on is still pending. From
  then on every call on the host answers `WEIVEL_C_STATUS_DESTROYED`, quietly; `weivel_c_host_free` follows once it
  has finished.

No entry function, listener, log line or sink report runs under a lock of the layer's: the host's own lock guards its
tables and is let go before it calls anything.

### C libraries as host services

`weivel_c_host_load_library` (`include/weivel/c/host/library.h`) loads a C library from a declaration value, the
standard's published `weivel_standard_library`, whether the standard read it or the caller built it, and a path the
caller gives, since a declaration names none. It binds the declared release symbol and each declared function of the
bytes kind, and registers each declared service under `WEIVEL_HOST` as one `weivel_c_library_service`: the service
id, the declared function names in order, and a `call` that takes the object, a function's index, the input and an
answer. A consumer that resolves the service is answered that very object.

- A call crosses no core call and takes no lock: the input is the caller's own pointer and length, handed to the
  library for the call, and the output is answered in place, as a `weivel_c_bytes` holding the library's own
  pointer. `weivel_c_bytes_release`, also reachable as the object's `release_bytes` for a plugin that links nothing of
  the layer's, hands it to the library's release symbol once and empties it; releasing the emptied struct again does
  nothing.
- A status other than 0 answers `WEIVEL_C_STATUS_FAILED` with the status and the body, and `body_is_utf8` says
  whether the body is well-formed UTF-8, so it reads as the text saying why; an empty body counts as UTF-8.
- A declaration the caller built is refused as the caller's mistake, `WEIVEL_C_STATUS_MISUSE` told to nobody else,
  when it has a function of another kind (named), no service or a service of no function, an id it lists twice, or an
  id a registration under the host already holds, a plugin's, another live library's or the host's own (the id
  named): registering under `WEIVEL_HOST` replaces a registration, so loading it would silently take that service
  over. The id is looked for in the layer's own records of every registration, never by a get, so a refused load
  leaves the host holding nothing it would later hear revoked. The refusal holds against every registration made
  before the load call; a plugin registering the same id on another thread while the load runs can still be
  replaced, and closing that window needs a register-if-absent call for `WEIVEL_HOST` in the core, which it does not
  have. The host's own registrations are held in the layer both ways, on whichever thread each runs:
  `weivel_c_host_register_service` claims its id on the host's list under the host's lock before it asks the core,
  refusing, `WEIVEL_C_STATUS_MISUSE`, an id a live library declares, and a load kept while that claim stands is
  refused naming the id. A library that is none, or lacks its release symbol or a function's, is answered as a value
  naming what is missing.
- The library's record is kept on the host's list before any service is registered, so a load that does not finish
  once one is (the destroy begun meanwhile, or a later service's registration refused) is undone as an unload is: an
  object a consumer was told of meanwhile answers `WEIVEL_C_STATUS_UNLOADED`, nothing stays registered, and the binary
  closes once nothing holds it.
- `weivel_c_host_unload_library` unregisters every service of the library, and every later call through an object a
  consumer still holds answers `WEIVEL_C_STATUS_UNLOADED`. Its binary closes only once no call runs in it and no bytes
  it answered are held, on whichever of them ends last, so nothing ever calls into a closed library and bytes still
  held stay readable. A call and each held body count themselves on one atomic word per library, which is also how
  an unload is seen; the host's own lock guards only its list of libraries.
- The destroy unloads every library, the last loaded first, after every plugin release and its events and steps,
  before the core is destroyed, and `weivel_c_host_free` refuses while bytes a library answered are still held. A
  close refused once the host is destroyed, when the last body held across the destroy is released, is told to
  nobody.

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

### The `standard` module, optional and its own library

`include/weivel/c/standard.h` is C's access to the standard's defaults, each feeding the host. It is optional and a
library of its own, `weivel_c_standard` (`weivel_c_standard_shared` for the shared build), the one target that links
the standard; the host never links, loads or requires it, and every host test links without it. The standard is a C
library, so C reaches it by calling its functions: where nothing needs translating the caller calls the standard
directly, `weivel_standard_discover` to lay out and discover plugins on disk, `weivel_standard_read_library`, whose
declaration is exactly what `weivel_c_host_load_library` takes with the path the caller gives, and the identity
parsing and ordering. The module adds what joins the standard's answers to the host:

- `weivel_c_standard_register_manifest` validates and reads a JSON manifest with the standard and registers it with
  an entry and the identity the caller supplies. The standard's manifest is the standard's, freed by its own release;
  the module releases it before returning, since the host copies what it registers. A document the standard declines
  answers `WEIVEL_C_STATUS_REFUSED_BY_THE_STANDARD`, the validation saying why, with nothing registered.
- `weivel_c_standard_discovered_manifest` fills a manifest value with a discovered plugin's manifest and the identity
  its identity file gives; `weivel_c_standard_register_discovered` registers it with the entry read from the file its
  manifest names. The layout names a file and no symbol, so the entry's symbol is always the caller's: the module has
  none of its own. An entry the standard did not find present, or a manifest naming none, opens nothing.
- `weivel_c_standard_offer_config`, `_offer_secrets` and `_offer_paths` register the caller's own definition of each
  service as a host service under the id the standard's binary answers, so a plugin resolving it through its context
  holds that very object.
- `weivel_c_standard_status` translates a status of the standard's into the layer's.

The module holds no default of its own: a test reads its sources and header and fails on any string, a literal, an
identifier stringized or a character literal; on a service id or placeholder macro of the standard's that would
compile one of its values into this binary; on a name that reads the environment or opens a file, the platform's
macro forms and the environment block among them; on a header named in quotes that leaves `src/standard`; and on a
definition the build gives the module's targets that carries a value.

### The `conformance` module: the layer suite's driver

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
- A `lifecycle` vector's entries record and run their actions as LAYER.md states; an awaited want answers pending and
  goes on from the want's callback. A step "throws" when the host answers a status that is no answer to a well-made
  call (anything but OK, REFUSED, NONE and DESTROYED), and a verify is `quiet` when it does not and nothing reached the
  log or the sink meanwhile.

`test/conformance/` holds one test per layer vector of the pinned kit, named by its id and reading the kit's file at
run time, and a coverage test that lists the kit's vector directory and fails on a vector no test names. The kit's
location comes from the build: `siblings/conformance`, or `WEIVEL_C_KIT_DIRECTORY`.

### The `benchmarks` module: the layer's half of the platform's benchmarks

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
  with it.

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
that runner's noise bounds in the same file, failing a run too noisy to judge; and a measuring mode that prints every
measurement the bounds are set from. Every bound is the kit's; the layer sets none, and the commit that adds `c.txt`
to the kit lists the samples.

## Building

C11 and CMake, with no other dependency. The static library is the target `weivel_c` and the shared one
`weivel_c_shared`. The layer is compiled against the core's `weivel/core.h`, found under
`siblings/core/include` unless `WEIVEL_C_CORE_INCLUDE_DIRECTORY` says otherwise, and the standard's
`weivel/library.h` for its declaration types alone, found under `siblings/standard/include` unless
`WEIVEL_C_STANDARD_INCLUDE_DIRECTORY` says otherwise; it links neither. When the standard's checkout is under
`siblings/standard`, or `WEIVEL_C_STANDARD_SOURCE_DIRECTORY` names one, the optional `weivel_c_standard` and
`weivel_c_standard_shared` are built from it as well, linking the standard's `weivel_standard` and
`weivel_standard_shared`, and `weivel_c_conformance` linking `weivel_standard`; the tests need them.

`.weivel/repo.json` pins `core`, `standard` and `conformance` by commit under `siblings/`. CI checks them out there;
locally, link or clone each checkout into `siblings/`. The tests build the pinned core as a shared library and load
it:

```
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Nothing is published: a consumer builds the layer from a pinned commit.
