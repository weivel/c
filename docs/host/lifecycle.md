# A host's life

How a host is made, registers plugins, starts and stops them, and is destroyed. What a plugin hands the host to be
started is [its entry](entry.md); what the host does with a C library is [libraries.md](libraries.md).

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
