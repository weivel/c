Sources the `calls_nothing_of_the_standard.cmake` check reads in place of a file of the host, each reaching the standard in a way that must still be caught, and the fixture project that writes link graphs the check must refuse; each test passes only when the check refuses naming the reach. The `.txt` files here are link graph lines that stand for a link the build never makes. The sources are never compiled.

<!-- generated: source-children -->
- [json_reader.c](../../../../../test/host/standard_evasions/json_reader.c) - a call of the standard's JSON reader
- [library_named_in_a_string.c](../../../../../test/host/standard_evasions/library_named_in_a_string.c) - the standard's library file named in a string
- [name_assembled_at_run_time.c](../../../../../test/host/standard_evasions/name_assembled_at_run_time.c) - a name of the standard's assembled at run time from a stem and a format
- [name_called.c](../../../../../test/host/standard_evasions/name_called.c) - a function of the standard's called by name
- [name_end_in_another_file.c](../../../../../test/host/standard_evasions/name_end_in_another_file.c) - the end of a function's name at a string's start
- [name_in_characters.c](../../../../../test/host/standard_evasions/name_in_characters.c) - a function's name spelled as character literals
- [name_printed_through_a_format.c](../../../../../test/host/standard_evasions/name_printed_through_a_format.c) - a name of weivel's around a format conversion
- [name_split_across_literals.c](../../../../../test/host/standard_evasions/name_split_across_literals.c) - a function's name split across adjacent literals
- [name_start_in_one_file.c](../../../../../test/host/standard_evasions/name_start_in_one_file.c) - the start of the standard's stem at a string's end
- [name_stringized.c](../../../../../test/host/standard_evasions/name_stringized.c) - a function's name stringized without its `weivel_`
- [pasted_name.c](../../../../../test/host/standard_evasions/pasted_name.c) - a token pasted onto a name of weivel's
- [pragma_link.c](../../../../../test/host/standard_evasions/pragma_link.c) - a `#pragma comment(lib, ...)` naming the standard
- [project/](project/) - the fixture project whose link graph the check reads
<!-- end generated -->
