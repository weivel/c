# `handles`

How the C layer keeps its own objects for the core's handles. The core holds no object, only handles, so whatever the
core hands the layer a handle for is kept here; the rules the table keeps in any language are the core's
`docs/contract/handles.md`.

`weivel_c_handles_create` makes the table from the core's handles to the layer's own objects: an entry per plugin,
service registration, capability implementation, watch and want, made when the core answers the handle and ended
when what it stands for ends. A handle the table does not hold answers nothing. What a plugin holds of another's is
kept by the pair of handles and dropped by the `WEIVEL_EVENT_REVOKED` that names it, and each entry has a token that
reaches it without a search, which is what a plugin is opened with as its payload. The table has its own lock and
calls nothing, so any thread may use it. `weivel_c_handles_reserve` takes the room of an addition ahead of time, so
the entry for a handle is made, once the core has answered it, by a step that cannot fail, and the core never holds
an object whose handle the table lacks.
