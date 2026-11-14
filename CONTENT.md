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

The `core`, `handles`, `timer` and `context` modules. The other four modules hold only a header saying what they
are for.

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
  `follow` hook, told the latest deadline after the lock is let go and before the dispatch, by one thread at a time.

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
once its core is gone. A timer made without a scheduler arms nothing and reads no clock, and `weivel_c_timer_now`,
where the layer takes the time for a want that needs it, then answers the caller's mistake.

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
makes is answered as the core answers a late call.

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
