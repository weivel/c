# `core`

How the C layer loads the core and makes every call into it. The rules the module keeps in any language are the
core's `docs/contract/core.md`. The layer names no function of the core but here, which is why one build of it runs
against whichever core binary its caller loads.

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
