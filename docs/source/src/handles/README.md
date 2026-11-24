The `handles` module: the table from the core's handles to the layer's own objects, with its own lock and calling nothing. What it keeps and why is [the handles doc](../../../handles.md)'s.

<!-- generated: source-children -->
- [table.c](../../../../src/handles/table.c) - the table: entries per kind of handle, tokens, reservations, holdings by pair of handles, and their ending
- [table.h](../../../../src/handles/table.h) - what the layer's other sources and its tests reach of the table beyond its public calls
<!-- end generated -->
