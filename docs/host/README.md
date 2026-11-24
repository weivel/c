The `host` module: what a C program uses to run plugins written in C. A host builds, for itself alone, the wrapper's
core, the handles table, the timer and the contexts; takes each plugin as a manifest value and an entry; starts and
stops plugins in the order the core's answers give; offers services of its own and C libraries as host services; and
never links, loads or calls the standard, so a host that wants none of the standard's defaults carries none of its
code. Every decision stays the core's. The rules the module keeps in any language are the core's
`docs/contract/host.md`; the docs here say how C keeps them.

- [lifecycle.md](lifecycle.md) - making a host, registering plugins, the ledger, host services, activation and its five outcomes, release and destroy
- [entry.md](entry.md) - a plugin's entry in C: activate, deactivate, a completion that finishes later, and reading an entry from a shared library
- [libraries.md](libraries.md) - C libraries as host services: bytes functions and definitions, descriptions, refused declarations, unloading and when a binary closes
- [standard-boundary.md](standard-boundary.md) - how three tests hold the host to never linking, loading or calling the standard's binary
