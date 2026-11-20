if(NOT EXISTS ${PROJECT_SOURCE_DIR}/siblings/core/CMakeLists.txt)
    message(FATAL_ERROR "siblings/core is missing: the tests load the real core, so link or clone the core checkout "
        "there (for example a directory junction or symlink named siblings/core pointing at the core repository)")
endif()

if(NOT TARGET weivel_core)
    add_subdirectory(${PROJECT_SOURCE_DIR}/siblings/core ${PROJECT_BINARY_DIR}/core EXCLUDE_FROM_ALL)
endif()
if(NOT TARGET weivel_c_standard)
    message(FATAL_ERROR "the standard module is not built, so its tests cannot run: link or clone the standard "
        "checkout at siblings/standard")
endif()

add_library(partial_core SHARED core/partial_core.c)
weivel_c_strict_warnings(partial_core)
set_target_properties(partial_core PROPERTIES
    C_VISIBILITY_PRESET hidden
    ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/shared)

add_library(entry_library SHARED host/entry_library.c)
target_include_directories(entry_library PRIVATE ${PROJECT_SOURCE_DIR}/include ${WEIVEL_C_CORE_INCLUDE_DIRECTORY})
weivel_c_strict_warnings(entry_library)
set_target_properties(entry_library PROPERTIES
    C_VISIBILITY_PRESET hidden
    ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/shared)

add_library(service_library SHARED host/service_library.c)
weivel_c_strict_warnings(service_library)
set_target_properties(service_library PROPERTIES
    C_VISIBILITY_PRESET hidden
    ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/shared)

add_library(echo_library SHARED ${PROJECT_SOURCE_DIR}/siblings/core/test/echo/echo.c)
target_compile_definitions(echo_library PRIVATE ECHO_BUILDING)
weivel_c_strict_warnings(echo_library)
set_target_properties(echo_library PROPERTIES
    OUTPUT_NAME echo
    C_VISIBILITY_PRESET hidden
    ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/shared)

# The standard's described test library, built from the pinned standard's sources as echo is from the core's: the
# standard defines it only as a top-level project.
set(described_sources ${WEIVEL_C_STANDARD_SOURCE_DIRECTORY}/test/described)
add_library(described_library SHARED ${described_sources}/described.c ${described_sources}/broken.c)
target_compile_definitions(described_library PRIVATE DESCRIBED_BUILDING)
weivel_c_strict_warnings(described_library)
set_target_properties(described_library PROPERTIES
    OUTPUT_NAME weivel_standard_described
    C_VISIBILITY_PRESET hidden
    ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/shared)
weivel_describe(described_library
    HEADERS ${WEIVEL_C_STANDARD_SOURCE_DIRECTORY}/include/weivel/services.h ${described_sources}/described.h
    PREFIX described_generated_
    EXPORT DESCRIBED_API
    SERVICE described_mirror SYMBOL described_mirror_description
    SERVICE described_walker SYMBOL described_walker_description)

include(host/link_graph.cmake)
weivel_c_write_link_graph_at_the_end(${CMAKE_CURRENT_BINARY_DIR}/links.txt)

# Adds a test executable. One that links the host's library is kept on the list of host binaries whose imports
# imports_nothing_of_the_standard.cmake reads.
function(weivel_c_test name source library)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE ${library})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    weivel_c_strict_warnings(${name})
    add_test(NAME ${name} COMMAND ${name} ${ARGN})
    set_tests_properties(${name} PROPERTIES TIMEOUT 60)
    if(library MATCHES "^weivel_c(_shared|_testing)?$")
        set_property(GLOBAL APPEND PROPERTY WEIVEL_C_HOST_BINARIES "$<TARGET_FILE:${name}>")
    endif()
endfunction()

foreach(variant "" _shared)
    weivel_c_test(core_load_test${variant} core/load_test.c weivel_c${variant}
        $<TARGET_FILE:weivel_core_shared> $<TARGET_FILE:partial_core> ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
        ${CMAKE_CURRENT_BINARY_DIR}/core_load_test${variant})
    add_dependencies(core_load_test${variant} weivel_core_shared partial_core)
    foreach(suite wrapper calls threads)
        weivel_c_test(core_${suite}_test${variant} core/${suite}_test.c weivel_c${variant}
            $<TARGET_FILE:weivel_core_shared>)
        add_dependencies(core_${suite}_test${variant} weivel_core_shared)
    endforeach()
    weivel_c_test(handles_table_test${variant} handles/table_test.c weivel_c${variant}
        $<TARGET_FILE:weivel_core_shared>)
    add_dependencies(handles_table_test${variant} weivel_core_shared)
    weivel_c_test(timer_timer_test${variant} timer/timer_test.c weivel_c${variant}
        $<TARGET_FILE:weivel_core_shared>)
    add_dependencies(timer_timer_test${variant} weivel_core_shared)
    foreach(suite context threads endings)
        weivel_c_test(context_${suite}_test${variant} context/${suite}_test.c weivel_c${variant}
            $<TARGET_FILE:weivel_core_shared>)
        add_dependencies(context_${suite}_test${variant} weivel_core_shared)
    endforeach()
    foreach(suite host lifecycle threads races)
        weivel_c_test(host_${suite}_test${variant} host/${suite}_test.c weivel_c${variant}
            $<TARGET_FILE:weivel_core_shared>)
        add_dependencies(host_${suite}_test${variant} weivel_core_shared)
    endforeach()
    weivel_c_test(host_entry_test${variant} host/entry_test.c weivel_c${variant}
        $<TARGET_FILE:weivel_core_shared> $<TARGET_FILE:entry_library> ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt)
    add_dependencies(host_entry_test${variant} weivel_core_shared entry_library)
    weivel_c_test(core_sink_test${variant} core/sink_test.c weivel_c${variant}
        $<TARGET_FILE:weivel_core_shared> ${CMAKE_CURRENT_BINARY_DIR}/core_sink_test${variant}.standard_error)
    add_dependencies(core_sink_test${variant} weivel_core_shared)
endforeach()

foreach(variant "" _shared)
    foreach(suite manifest services)
        weivel_c_test(standard_${suite}_test${variant} standard/${suite}_test.c weivel_c_standard${variant}
            $<TARGET_FILE:weivel_core_shared>)
        add_dependencies(standard_${suite}_test${variant} weivel_core_shared)
    endforeach()
    weivel_c_test(standard_library_test${variant} standard/library_test.c weivel_c_standard${variant}
        $<TARGET_FILE:weivel_core_shared> $<TARGET_FILE:service_library>)
    add_dependencies(standard_library_test${variant} weivel_core_shared service_library)
    weivel_c_test(standard_description_test${variant} standard/description_test.c weivel_c_standard${variant}
        $<TARGET_FILE:weivel_core_shared> $<TARGET_FILE:described_library>)
    add_dependencies(standard_description_test${variant} weivel_core_shared described_library)
    weivel_c_test(standard_discovery_test${variant} standard/discovery_test.c weivel_c_standard${variant}
        $<TARGET_FILE:weivel_core_shared> $<TARGET_FILE:entry_library>
        ${CMAKE_CURRENT_BINARY_DIR}/standard_discovery_test${variant}.plugins)
    add_dependencies(standard_discovery_test${variant} weivel_core_shared entry_library)
endforeach()

set(WEIVEL_C_KIT_DIRECTORY ${PROJECT_SOURCE_DIR}/siblings/conformance
    CACHE PATH "The conformance kit checkout, at the commit repo.json pins, whose layer vectors the suite reads")
if(NOT EXISTS ${WEIVEL_C_KIT_DIRECTORY}/vectors/layer/LAYER.md)
    message(FATAL_ERROR "no vectors/layer under ${WEIVEL_C_KIT_DIRECTORY}: link or clone the conformance kit checkout "
        "at siblings/conformance, or set WEIVEL_C_KIT_DIRECTORY to it")
endif()
weivel_c_test(conformance_layer_vectors_test "conformance/layer_vectors_test.c;conformance/layer_vectors.c"
    weivel_c_conformance $<TARGET_FILE:weivel_core_shared> ${WEIVEL_C_KIT_DIRECTORY}/vectors/layer
    $<TARGET_FILE:echo_library> $<TARGET_FILE:described_library>)
add_dependencies(conformance_layer_vectors_test weivel_core_shared echo_library described_library)
set(conformance_probe ${CMAKE_CURRENT_BINARY_DIR}/conformance_probe)
file(MAKE_DIRECTORY ${conformance_probe}/nested/deeper)
weivel_c_test(conformance_coverage_test "conformance/coverage_test.c;conformance/layer_vectors.c"
    weivel_c_conformance ${WEIVEL_C_KIT_DIRECTORY}/vectors/layer ${conformance_probe}
    ${WEIVEL_C_KIT_DIRECTORY}/checks/languages.txt)

# Writes each definition the build gives the standard module's targets, one `<target> <definition>` a line, for the
# check that the module compiles no value of its own in.
function(weivel_c_write_the_modules_definitions file)
    set(lines "")
    foreach(target weivel_c_standard weivel_c_standard_shared)
        get_target_property(definitions ${target} COMPILE_DEFINITIONS)
        get_target_property(options ${target} COMPILE_OPTIONS)
        foreach(option IN LISTS options)
            if(option MATCHES "^[-/]D(.+)$")
                list(APPEND definitions "${CMAKE_MATCH_1}")
            endif()
        endforeach()
        foreach(definition IN LISTS definitions)
            if(definition AND NOT definition MATCHES "-NOTFOUND$")
                string(APPEND lines "${target} ${definition}\n")
            endif()
        endforeach()
    endforeach()
    file(WRITE ${file} "${lines}")
endfunction()

set(module_definitions ${CMAKE_CURRENT_BINARY_DIR}/standard_definitions.txt)
weivel_c_call_at_the_end(weivel_c_write_the_modules_definitions ${module_definitions})

function(weivel_c_default_check name expected)
    add_test(NAME ${name}
        COMMAND ${CMAKE_COMMAND} -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
            -DSTANDARD_INCLUDE=${WEIVEL_C_STANDARD_INCLUDE_DIRECTORY} -DDEFINITIONS=${module_definitions}
            ${ARGN} -P ${CMAKE_CURRENT_SOURCE_DIR}/standard/holds_no_default.cmake)
    if(NOT expected STREQUAL "")
        set_tests_properties(${name} PROPERTIES
            PASS_REGULAR_EXPRESSION "${expected}"
            FAIL_REGULAR_EXPRESSION "holds no default of its own")
    endif()
endfunction()

function(weivel_c_default_evasion name expected)
    weivel_c_default_check(standard_holds_no_default_refuses_${name} "${expected}"
        -DEVASION=${CMAKE_CURRENT_SOURCE_DIR}/standard/default_evasions/${name}.c)
endfunction()

weivel_c_default_check(standard_holds_no_default "")
weivel_c_default_evasion(file_name "file_name.c holds a string literal")
weivel_c_default_evasion(service_id_copied "names WEIVEL_STANDARD_SERVICE_CONFIG")
weivel_c_default_evasion(environment_read "names getenv")
weivel_c_default_evasion(file_name_in_characters "file_name_in_characters.c holds a string literal")
weivel_c_default_evasion(file_name_stringized "file_name_stringized.c holds a string literal")
weivel_c_default_evasion(environment_read_through_a_macro "names GetEnvironmentVariable,")
weivel_c_default_evasion(file_opened_through_a_macro "names CreateFile,")
weivel_c_default_evasion(environment_block "names environ,")
weivel_c_default_evasion(file_opened_from_a_descriptor "names fdopen,")
weivel_c_default_evasion(file_probed "names _access,")
weivel_c_default_evasion(header_outside_the_module "includes \"../host/defaults.h\", which leaves src/standard")
weivel_c_default_check(standard_holds_no_default_refuses_definition_with_a_value
    "weivel_c_standard defines PLUGINS_FILE=\"plugins.json\", compiling a value into the module"
    -DDEFINITIONS_EVASION=${CMAKE_CURRENT_SOURCE_DIR}/standard/default_evasions/definition_with_a_value.txt)

weivel_c_test(host_library_test host/library_test.c weivel_c_testing
    $<TARGET_FILE:weivel_core_shared> $<TARGET_FILE:service_library> $<TARGET_FILE:echo_library>
    ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt)
add_dependencies(host_library_test weivel_core_shared service_library echo_library)
weivel_c_test(host_library_loading_test host/library_loading_test.c weivel_c_testing
    $<TARGET_FILE:weivel_core_shared> $<TARGET_FILE:service_library> $<TARGET_FILE:echo_library>)
add_dependencies(host_library_loading_test weivel_core_shared service_library echo_library)
weivel_c_test(host_definition_test host/definition_test.c weivel_c_testing
    $<TARGET_FILE:weivel_core_shared> $<TARGET_FILE:echo_library>)
add_dependencies(host_definition_test weivel_core_shared echo_library)
weivel_c_test(host_description_test host/description_test.c weivel_c_testing
    $<TARGET_FILE:weivel_core_shared> $<TARGET_FILE:described_library> $<TARGET_FILE:echo_library>)
add_dependencies(host_description_test weivel_core_shared described_library echo_library)
weivel_c_test(host_references_test host/references_test.c weivel_c_testing)
weivel_c_test(host_seams_test host/seams_test.c weivel_c_testing $<TARGET_FILE:weivel_core_shared>)
add_dependencies(host_seams_test weivel_core_shared)
weivel_c_test(host_library_threads_test host/library_threads_test.c weivel_c_testing
    $<TARGET_FILE:weivel_core_shared> $<TARGET_FILE:service_library>)
add_dependencies(host_library_threads_test weivel_core_shared service_library)
weivel_c_test(host_entry_closing_test host/entry_closing_test.c weivel_c_testing
    $<TARGET_FILE:weivel_core_shared> $<TARGET_FILE:entry_library>)
add_dependencies(host_entry_closing_test weivel_core_shared entry_library)


function(weivel_c_standard_check name expected)
    add_test(NAME ${name}
        COMMAND ${CMAKE_COMMAND} -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
            -DSTANDARD_INCLUDE=${WEIVEL_C_STANDARD_INCLUDE_DIRECTORY}
            -DLINK_GRAPH=${CMAKE_CURRENT_BINARY_DIR}/links.txt
            ${ARGN} -P ${CMAKE_CURRENT_SOURCE_DIR}/host/calls_nothing_of_the_standard.cmake)
    if(NOT expected STREQUAL "")
        set_tests_properties(${name} PROPERTIES
            PASS_REGULAR_EXPRESSION "${expected}"
            FAIL_REGULAR_EXPRESSION "the host names none of the standard's")
    endif()
endfunction()

weivel_c_standard_check(host_calls_nothing_of_the_standard "")
set(standard_evasions ${CMAKE_CURRENT_SOURCE_DIR}/host/standard_evasions)
weivel_c_standard_check(host_calls_nothing_of_the_standard_refuses_a_name_called
    "names the standard's function weivel_standard_library_release"
    -DEVASION=${standard_evasions}/name_called.c)
weivel_c_standard_check(host_calls_nothing_of_the_standard_refuses_a_name_split_across_literals
    "spells the standard's function weivel_standard_read_library in a string"
    -DEVASION=${standard_evasions}/name_split_across_literals.c)
weivel_c_standard_check(host_calls_nothing_of_the_standard_refuses_the_json_reader
    "names the standard's function weivel_json_parse"
    -DEVASION=${standard_evasions}/json_reader.c)
weivel_c_standard_check(host_calls_nothing_of_the_standard_refuses_a_name_assembled_at_run_time
    "spells weivel_json_, the start of the standard's names, in a string"
    -DEVASION=${standard_evasions}/name_assembled_at_run_time.c)
weivel_c_standard_check(host_calls_nothing_of_the_standard_refuses_a_pasted_name
    "pastes tokens together onto a name of weivel"
    -DEVASION=${standard_evasions}/pasted_name.c)
weivel_c_standard_check(host_calls_nothing_of_the_standard_refuses_a_host_test_linking_the_standard
    "host_sneaking_test links the standard's weivel_standard_shared through host_sneaking_test"
    -DLINK_EVASION=${standard_evasions}/host_test_links_the_standard.txt)
weivel_c_standard_check(host_calls_nothing_of_the_standard_refuses_the_host_linking_through_another_target
    "weivel_c links the standard's weivel_standard through helping"
    -DLINK_EVASION=${standard_evasions}/host_links_through_another_target.txt)
weivel_c_standard_check(host_calls_nothing_of_the_standard_refuses_a_host_test_named_like_a_standard_test
    "standard_sneaking_test links the standard's weivel_standard_shared through standard_sneaking_test"
    -DLINK_EVASION=${standard_evasions}/standard_named_host_test_links_the_standard.txt)
weivel_c_standard_check(host_calls_nothing_of_the_standard_refuses_a_host_test_linking_the_conformance_module
    "host_sneaking_test links the standard's weivel_c_conformance through host_sneaking_test"
    -DLINK_EVASION=${standard_evasions}/host_test_links_the_conformance_module.txt)
weivel_c_standard_check(host_calls_nothing_of_the_standard_refuses_a_pragma_linking_it
    "pragma_link.c links the standard by #pragma comment"
    -DEVASION=${standard_evasions}/pragma_link.c)
weivel_c_standard_check(host_calls_nothing_of_the_standard_refuses_the_library_named_in_a_string
    "library_named_in_a_string.c spells weivel_standard, the start of the standard's names, in a string"
    -DEVASION=${standard_evasions}/library_named_in_a_string.c)
weivel_c_standard_check(host_calls_nothing_of_the_standard_refuses_a_name_in_characters
    "name_in_characters.c spells the standard's function weivel_standard_read_library in a string"
    -DEVASION=${standard_evasions}/name_in_characters.c)
weivel_c_standard_check(host_calls_nothing_of_the_standard_refuses_a_name_printed_through_a_format
    "name_printed_through_a_format.c spells a name of weivel's around a format conversion"
    -DEVASION=${standard_evasions}/name_printed_through_a_format.c)
weivel_c_standard_check(host_calls_nothing_of_the_standard_refuses_the_start_of_a_name_in_one_file
    "name_start_in_one_file.c ends a string in weivel_stan, the start of the standard's names"
    -DEVASION=${standard_evasions}/name_start_in_one_file.c)
weivel_c_standard_check(host_calls_nothing_of_the_standard_refuses_the_end_of_a_name_in_another_file
    "name_end_in_another_file.c spells the end of the standard's function weivel_standard_read_library"
    -DEVASION=${standard_evasions}/name_end_in_another_file.c)
weivel_c_standard_check(host_calls_nothing_of_the_standard_refuses_a_name_stringized
    "name_stringized.c names the standard's function weivel_standard_read_library without its weivel_"
    -DEVASION=${standard_evasions}/name_stringized.c)
weivel_c_standard_check(host_calls_nothing_of_the_standard_refuses_a_link_by_path
    "weivel_c links the standard's C:/build/standard/Debug/weivel_standard.lib through weivel_c"
    -DLINK_EVASION=${standard_evasions}/link_by_path.txt)
weivel_c_standard_check(host_calls_nothing_of_the_standard_refuses_a_link_by_flag
    "weivel_c links the standard's -lweivel_standard through weivel_c"
    -DLINK_EVASION=${standard_evasions}/link_by_flag.txt)
weivel_c_standard_check(host_calls_nothing_of_the_standard_refuses_a_link_by_target_file
    "weivel_c links the standard's [$]<TARGET_FILE:weivel_standard_shared> through weivel_c"
    -DLINK_EVASION=${standard_evasions}/link_by_target_file.txt)
weivel_c_standard_check(host_calls_nothing_of_the_standard_refuses_a_link_in_a_configuration
    "weivel_c links the standard's [$]<[$]<CONFIG:Debug>:weivel_standard> through weivel_c"
    -DLINK_EVASION=${standard_evasions}/link_in_a_configuration.txt)

function(weivel_c_link_graph_fixture scenario expected)
    add_test(NAME host_calls_nothing_of_the_standard_refuses_a_${scenario}_link
        COMMAND ${CMAKE_COMMAND} -DFIXTURE=${standard_evasions}/project -DSCENARIO=${scenario}
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/link_graph_fixture_${scenario} -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
            -DSTANDARD_INCLUDE=${WEIVEL_C_STANDARD_INCLUDE_DIRECTORY}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/host/check_a_link_graph_fixture.cmake)
    set_tests_properties(host_calls_nothing_of_the_standard_refuses_a_${scenario}_link PROPERTIES
        PASS_REGULAR_EXPRESSION "${expected}"
        FAIL_REGULAR_EXPRESSION "the host names none of the standard's")
endfunction()

weivel_c_link_graph_fixture(late "weivel_c links the standard's weivel_standard through weivel_c")
weivel_c_link_graph_fixture(imported
    "weivel_c_shared links the standard's /elsewhere/libweivel_standard.so through standard_copy")
weivel_c_link_graph_fixture(nested "host_deep_test links the standard's weivel_standard through host_deep_test")

if(MSVC)
    set(import_reader -DFORMAT=pe "-DREADER=${CMAKE_LINKER}")
elseif(APPLE)
    find_program(WEIVEL_C_OTOOL otool)
    set(import_reader -DFORMAT=macho "-DREADER=${WEIVEL_C_OTOOL}" "-DSYMBOLS=${CMAKE_NM}")
else()
    set(import_reader -DFORMAT=elf "-DREADER=${CMAKE_OBJDUMP}" "-DSYMBOLS=${CMAKE_NM}")
endif()

function(weivel_c_import_check name expected binaries)
    add_test(NAME ${name}
        COMMAND ${CMAKE_COMMAND} ${import_reader} "-DBINARIES=${binaries}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/host/imports_nothing_of_the_standard.cmake)
    if(NOT expected STREQUAL "")
        set_tests_properties(${name} PROPERTIES
            PASS_REGULAR_EXPRESSION "${expected}"
            FAIL_REGULAR_EXPRESSION "none of the [0-9]+ host binaries")
    endif()
endfunction()

get_property(host_binaries GLOBAL PROPERTY WEIVEL_C_HOST_BINARIES)
list(APPEND host_binaries "$<TARGET_FILE:weivel_c_shared>")
string(JOIN "|" host_binaries ${host_binaries})
weivel_c_import_check(host_imports_nothing_of_the_standard "" "${host_binaries}")
weivel_c_import_check(host_imports_nothing_of_the_standard_refuses_a_binary_importing_it
    "standard_manifest_test_shared[^ ]* imports the standard: [^ ]*weivel_c_standard"
    "$<TARGET_FILE:standard_manifest_test_shared>")

add_test(NAME core_table_covers_the_core
    COMMAND ${CMAKE_COMMAND} -DCORE_HEADER=${WEIVEL_C_CORE_INCLUDE_DIRECTORY}/weivel/core.h
        -DFUNCTIONS=${PROJECT_SOURCE_DIR}/src/core/functions.h
        -P ${CMAKE_CURRENT_SOURCE_DIR}/core/table_covers_the_core.cmake)

add_test(NAME core_only_the_wrapper_calls
    COMMAND ${CMAKE_COMMAND} -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
        -DFUNCTIONS=${PROJECT_SOURCE_DIR}/src/core/functions.h
        -P ${CMAKE_CURRENT_SOURCE_DIR}/core/only_the_wrapper_calls.cmake)

function(weivel_c_evasion name stands_as expected)
    add_test(NAME core_only_the_wrapper_calls_refuses_${name}
        COMMAND ${CMAKE_COMMAND} -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
            -DFUNCTIONS=${PROJECT_SOURCE_DIR}/src/core/functions.h
            -DEVASION=${CMAKE_CURRENT_SOURCE_DIR}/core/evasions/${name}.c -DEVASION_STANDS_AS=${stands_as}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/core/only_the_wrapper_calls.cmake)
    set_tests_properties(core_only_the_wrapper_calls_refuses_${name} PROPERTIES
        PASS_REGULAR_EXPRESSION "${expected}"
        FAIL_REGULAR_EXPRESSION "only the wrapper calls the")
endfunction()

weivel_c_evasion(field_named_through_a_macro src/context/dispatch.c
    "src/context/dispatch.c names the table's advance 1 times where 0 are due")
weivel_c_evasion(offset_into_the_table src/timer/timer.c
    "src/timer/timer.c takes an offset into the table, which only src/core/load.c may")
weivel_c_evasion(comment_between_operator_and_name src/core/calls.c
    "src/core/calls.c names the table's core_destroy 1 times where 0 are due")
weivel_c_evasion(pointer_copied_in_the_wrapper src/core/wrapper.c
    "src/core/wrapper.c names the table's unregister 1 times where 0 are due")
weivel_c_evasion(pointer_copied_in_the_calls src/core/calls.c
    "src/core/calls.c names the table's release_plugin 2 times where 1 are due")

set(WEIVEL_C_KIT_BENCHMARKS ${WEIVEL_C_KIT_DIRECTORY}/benchmarks)
add_executable(benchmarks_benchmark benchmarks/benchmark.c)
target_link_libraries(benchmarks_benchmark PRIVATE weivel_c_benchmarks)
weivel_c_strict_warnings(benchmarks_benchmark)
add_dependencies(benchmarks_benchmark weivel_core_shared echo_library)

option(WEIVEL_C_BENCHMARKS_REQUIRED
    "Fail, rather than skip, a release-only benchmark gate run outside a Release build, as a job running the gates must"
    OFF)

# Runs one mode of the benchmarks against the real core, holding it to `expected_exit` and, when given, `must_match`.
function(weivel_c_benchmark name expected_exit must_match)
    string(JOIN "|" arguments ${ARGN})
    add_test(NAME benchmarks_${name}
        COMMAND ${CMAKE_COMMAND} -DPROGRAM=$<TARGET_FILE:benchmarks_benchmark>
            "-DARGUMENTS=${arguments}" -DEXPECTED_EXIT=${expected_exit} -DCONFIG=$<CONFIG>
            "-DMUST_MATCH=${must_match}" ${WEIVEL_C_BENCHMARK_RELEASE_ONLY}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/run.cmake)
    set_tests_properties(benchmarks_${name} PROPERTIES TIMEOUT 300)
    if(NOT WEIVEL_C_BENCHMARKS_REQUIRED)
        set_tests_properties(benchmarks_${name} PROPERTIES SKIP_RETURN_CODE 77)
    endif()
endfunction()

set(benchmark_core $<TARGET_FILE:weivel_core_shared>)
set(benchmark_library $<TARGET_FILE:echo_library>)
set(benchmark_fixtures ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/fixtures)
weivel_c_benchmark(identity_hands_the_consumer_the_providers_own_object 0 "crossed into the core 0 times"
    identity ${benchmark_core})
weivel_c_benchmark(identity_refuses_a_copy_of_the_providers_object 1 "not the provider's own object"
    identity-copied ${benchmark_core})
weivel_c_benchmark(control_budget_refuses_a_call_over_it 1 "is over the budget of 1.00 ns"
    control-budget ${benchmark_core} ${benchmark_fixtures}/one_nanosecond.txt brief)
weivel_c_benchmark(control_ratio_fails_on_a_platform_the_budget_gives_no_line 1 "gives no line for control-call on"
    control-ratio ${benchmark_core} ${benchmark_fixtures}/no_platform_line.txt brief)
weivel_c_benchmark(library_ratio_fails_on_a_platform_the_budget_gives_no_line 1 "gives no line for library-call on"
    library-ratio ${benchmark_core} ${benchmark_library} ${benchmark_fixtures}/no_platform_line.txt brief)
weivel_c_benchmark(same_language_fails_on_a_platform_the_budget_gives_no_noise_line 1
    "gives no line for quiet-noise, worst-noise or noise-margin on"
    same-language ${benchmark_core} ${benchmark_fixtures}/no_platform_line.txt brief)
weivel_c_benchmark(library_definition_fails_on_a_platform_the_budget_gives_no_noise_line 1
    "library-call-definition: the budget gives no line for quiet-noise, worst-noise or noise-margin on"
    library-definition ${benchmark_core} ${benchmark_library} ${benchmark_fixtures}/no_platform_line.txt brief)
weivel_c_benchmark(a_budget_with_a_single_noise_margin_is_refused 2 "'noise-margin 11.68' is neither"
    control-ratio ${benchmark_core} ${benchmark_fixtures}/single_noise_margin.txt brief)
# With a quiet noise of 1 and no noise of its own, a run's bound is 1 plus its platform's margin: half over the margin
# holds only by this platform's margin, and a whole unit and a half over it fails by it.
if(WIN32)
    set(benchmark_platform_margin 202)
elseif(APPLE)
    set(benchmark_platform_margin 303)
else()
    set(benchmark_platform_margin 101)
endif()
math(EXPR benchmark_margin_plus_one "${benchmark_platform_margin} + 1")
weivel_c_benchmark(same_language_judges_by_the_noise_margin_of_its_own_platform 0
    "same-language platform=[a-z]+ ratio=[0-9.]+ bound=[0-9.]+ noise_margin=${benchmark_platform_margin}[.]0000 "
    same-language-judge ${benchmark_core} ${benchmark_fixtures}/noise_margin_per_platform.txt
    ${benchmark_platform_margin}.5 1)
weivel_c_benchmark(same_language_refuses_a_ratio_past_the_noise_margin_of_its_own_platform 1
    "is outside its bound of ${benchmark_margin_plus_one}[.]0000"
    same-language-judge ${benchmark_core} ${benchmark_fixtures}/noise_margin_per_platform.txt
    ${benchmark_margin_plus_one}.5 1)

set(WEIVEL_C_BENCHMARK_RELEASE_ONLY -DRELEASE_ONLY=ON)
weivel_c_benchmark(measure_prints_every_measurement 0 "weivel-c-benchmark same-language platform=[a-z]+ repetition=10 "
    measure ${benchmark_core} ${benchmark_library} 10)
weivel_c_benchmark(control_call_holds_the_cores_budget 0 "control-call-budget median_ns="
    control-budget ${benchmark_core} ${WEIVEL_C_KIT_BENCHMARKS}/budget.txt)
set_tests_properties(benchmarks_measure_prints_every_measurement benchmarks_control_call_holds_the_cores_budget
    PROPERTIES LABELS release-benchmark)
weivel_c_benchmark(control_call_holds_its_bound_over_the_bare_table_call 0
    "control-call platform=[a-z]+ ratio_median=[0-9.]+ bound="
    control-ratio ${benchmark_core} ${WEIVEL_C_KIT_BENCHMARKS}/c.txt)
weivel_c_benchmark(library_call_holds_its_bound_over_a_direct_call 0
    "library-call platform=[a-z]+ ratio_median=[0-9.]+ bound="
    library-ratio ${benchmark_core} ${benchmark_library} ${WEIVEL_C_KIT_BENCHMARKS}/c.txt)
weivel_c_benchmark(same_language_call_holds_the_noise_bounds 0 "same-language platform=[a-z]+ ratio=[0-9.]+ bound="
    same-language ${benchmark_core} ${WEIVEL_C_KIT_BENCHMARKS}/c.txt)
weivel_c_benchmark(library_call_definition_holds_the_noise_bounds 0
    "library-call-definition platform=[a-z]+ ratio=[0-9.]+ bound="
    library-definition ${benchmark_core} ${benchmark_library} ${WEIVEL_C_KIT_BENCHMARKS}/c.txt)
set_tests_properties(benchmarks_control_call_holds_its_bound_over_the_bare_table_call
    benchmarks_library_call_holds_its_bound_over_a_direct_call benchmarks_same_language_call_holds_the_noise_bounds
    benchmarks_library_call_definition_holds_the_noise_bounds
    PROPERTIES LABELS release-benchmark)
