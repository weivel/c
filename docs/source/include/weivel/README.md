The headers a C host and a C plugin include. `c.h` is the whole public surface of the host's library in one include; the `standard` and `conformance` modules' headers are left out of it because each is a library of its own that links the standard, so only what links one includes its header.

<!-- generated: source-children -->
- [c.h](../../../../include/weivel/c.h) - includes every module header of the host's library, and not the `standard` or `conformance` header
- [c/](c/) - one header per module, and the host's further headers
<!-- end generated -->
