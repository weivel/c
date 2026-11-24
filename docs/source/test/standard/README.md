The `standard` module's tests, linking the module and the standard, against the real core, and the check `holds_no_default.cmake`, which reads the module's sources, its header and the definitions its targets are built with, with the files of `default_evasions/` as what it must catch. The tests lay out what they need under a directory each makes and removes. What the module adds is [the standard doc](../../../standard.md)'s.

<!-- generated: source-children -->
- [default_evasions/](default_evasions/) - sources that hold a default in the module, each one the check must catch
- [description_test.c](../../../../test/standard/description_test.c) - the standard's description check as the host's, on the standard's described library
- [discovery_test.c](../../../../test/standard/discovery_test.c) - plugins laid out on disk, discovered by the standard and registered through the module
- [disk.h](../../../../test/standard/disk.h) - files a test lays out under a directory it makes, and removes by name
- [holds_no_default.cmake](../../../../test/standard/holds_no_default.cmake) - fails when the module holds a default of its own: a string, a service id, an environment or file read, a header outside it, a valued definition
- [library_test.c](../../../../test/standard/library_test.c) - a declaration the standard read is what the host loads, released by the caller right after
- [manifest_test.c](../../../../test/standard/manifest_test.c) - a JSON manifest read by the standard registered through the module, and one the standard declines
- [services_test.c](../../../../test/standard/services_test.c) - the caller's config, secrets and paths offered under the standard's ids and resolved as the very objects
<!-- end generated -->
