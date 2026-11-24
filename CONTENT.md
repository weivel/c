## What this is

The C language implementation of weivel, for a host written in C that runs plugins written in C. It loads the core's
binary from the path its caller gives and carries out the core's answers, held to the core's contract and to the
conformance kit's vectors.

## What it is not

It is not the core and decides nothing itself: compatibility, order, reach, waiting and revocation are the core's
answers. It forces no path, file name, variable or format on its caller, and reading manifests or discovering plugins
on disk is the standard's, which a host may leave out.

## Where to read on

Start at [docs/README.md](docs/README.md), the layer's architecture and the full statement of what it is and is not,
and descend: one doc per module, and one doc per source folder, where building and testing the layer is answered.
