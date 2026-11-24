Sources the `holds_no_default.cmake` check reads in place of a file of the module, each holding a default in a way that must still be caught; each test passes only when the check refuses it naming what it holds. `definition_with_a_value.txt` stands for a definition the build would give the module's targets. The sources are never compiled.

<!-- generated: source-children -->
- [environment_block.c](../../../../../test/standard/default_evasions/environment_block.c) - the environment block read directly
- [environment_read.c](../../../../../test/standard/default_evasions/environment_read.c) - an environment variable read with `getenv`
- [environment_read_through_a_macro.c](../../../../../test/standard/default_evasions/environment_read_through_a_macro.c) - an environment variable read through the platform's macro form
- [file_name.c](../../../../../test/standard/default_evasions/file_name.c) - a file name in a string literal
- [file_name_in_characters.c](../../../../../test/standard/default_evasions/file_name_in_characters.c) - a file name spelled as character literals
- [file_name_stringized.c](../../../../../test/standard/default_evasions/file_name_stringized.c) - a file name stringized from an identifier
- [file_opened_from_a_descriptor.c](../../../../../test/standard/default_evasions/file_opened_from_a_descriptor.c) - a file opened from a descriptor
- [file_opened_through_a_macro.c](../../../../../test/standard/default_evasions/file_opened_through_a_macro.c) - a file opened through the platform's macro form
- [file_probed.c](../../../../../test/standard/default_evasions/file_probed.c) - a file probed for being there
- [header_outside_the_module.c](../../../../../test/standard/default_evasions/header_outside_the_module.c) - a header named in quotes that leaves `src/standard`
- [service_id_copied.c](../../../../../test/standard/default_evasions/service_id_copied.c) - a service id macro of the standard's compiled in
<!-- end generated -->
