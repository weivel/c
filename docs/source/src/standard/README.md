The optional `standard` module, built as its own library, one of the two libraries of this repository that link the standard, beside the `conformance` module's; the host's never does. It holds no default of its own, which `holds_no_default.cmake` ([the standard tests](../../test/standard/README.md)) reads from its sources. What it adds is [the standard doc](../../../standard.md)'s.

<!-- generated: source-children -->
- [descriptions.c](../../../../src/standard/descriptions.c) - the standard's description check as a host's
- [manifests.c](../../../../src/standard/manifests.c) - registering a JSON manifest, and filling and registering a discovered plugin's manifest
- [services.c](../../../../src/standard/services.c) - offering the caller's config, secrets and paths definitions under the ids the standard answers
- [status.c](../../../../src/standard/status.c) - translating a status of the standard's into the layer's
<!-- end generated -->
