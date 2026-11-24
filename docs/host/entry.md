# A plugin's entry in C

The one thing a plugin written in C hands its host. The entry in the abstract, which every language offers in its own
form, is the core's `docs/contract/host.md`.

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
