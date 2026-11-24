# `context`

What a plugin written in C sees of the core, and the only way it reaches any of it. The rules a context keeps in any
language are the core's `docs/contract/context.md`.

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
- may register a service awaiting its releases, `weivel_c_context_register_service_awaiting_releases`, for an object
  that stays usable only until each holder lets go: once it is unregistered and every holding of it has ended, its
  listeners hear `WEIVEL_C_PLUGIN_EVENT_HOLDINGS_RELEASED` naming the registration. A holder of such a registration,
  told of its revocation by a `WEIVEL_C_PLUGIN_EVENT_REVOKED` whose `handle` names it, says it no longer uses the
  object with `weivel_c_context_release_holding`, however it came to hold it, a watch hearing it registered
  included;
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
