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

function(weivel_c_test name source library)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE ${library})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    weivel_c_strict_warnings(${name})
    add_test(NAME ${name} COMMAND ${name} ${ARGN})
    set_tests_properties(${name} PROPERTIES TIMEOUT 60)
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
    weivel_c_test(standard_discovery_test${variant} standard/discovery_test.c weivel_c_standard${variant}
        $<TARGET_FILE:weivel_core_shared> $<TARGET_FILE:entry_library>
        ${CMAKE_CURRENT_BINARY_DIR}/standard_discovery_test${variant}.plugins)
    add_dependencies(standard_discovery_test${variant} weivel_core_shared entry_library)
endforeach()

add_test(NAME standard_holds_no_default
    COMMAND ${CMAKE_COMMAND} -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/standard/holds_no_default.cmake)

function(weivel_c_default_evasion name expected)
    add_test(NAME standard_holds_no_default_refuses_${name}
        COMMAND ${CMAKE_COMMAND} -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
            -DEVASION=${CMAKE_CURRENT_SOURCE_DIR}/standard/default_evasions/${name}.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/standard/holds_no_default.cmake)
    set_tests_properties(standard_holds_no_default_refuses_${name} PROPERTIES
        PASS_REGULAR_EXPRESSION "${expected}"
        FAIL_REGULAR_EXPRESSION "holds no default of its own")
endfunction()

weivel_c_default_evasion(file_name "file_name.c holds a string literal")
weivel_c_default_evasion(service_id_copied "names WEIVEL_STANDARD_SERVICE_CONFIG")
weivel_c_default_evasion(environment_read "names getenv")

weivel_c_test(host_library_test host/library_test.c weivel_c_testing
    $<TARGET_FILE:weivel_core_shared> $<TARGET_FILE:service_library> $<TARGET_FILE:echo_library>
    ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt)
add_dependencies(host_library_test weivel_core_shared service_library echo_library)
weivel_c_test(host_library_loading_test host/library_loading_test.c weivel_c_testing
    $<TARGET_FILE:weivel_core_shared> $<TARGET_FILE:service_library> $<TARGET_FILE:echo_library>)
add_dependencies(host_library_loading_test weivel_core_shared service_library echo_library)
weivel_c_test(host_references_test host/references_test.c weivel_c_testing)
weivel_c_test(host_library_threads_test host/library_threads_test.c weivel_c_testing
    $<TARGET_FILE:weivel_core_shared> $<TARGET_FILE:service_library>)
add_dependencies(host_library_threads_test weivel_core_shared service_library)
weivel_c_test(host_entry_closing_test host/entry_closing_test.c weivel_c_testing
    $<TARGET_FILE:weivel_core_shared> $<TARGET_FILE:entry_library>)
add_dependencies(host_entry_closing_test weivel_core_shared entry_library)

# Writes each target of this repository and what it links, one line each, `<target> <link>,<link>`, for the check that
# no target but the standard module's reaches the standard.
function(weivel_c_write_link_graph file)
    set(lines "")
    foreach(directory ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
        get_property(targets DIRECTORY ${directory} PROPERTY BUILDSYSTEM_TARGETS)
        foreach(target IN LISTS targets)
            get_target_property(links ${target} LINK_LIBRARIES)
            get_target_property(interface_links ${target} INTERFACE_LINK_LIBRARIES)
            set(all_links "")
            foreach(link IN LISTS links interface_links)
                if(link AND NOT link MATCHES "-NOTFOUND$")
                    list(APPEND all_links "${link}")
                endif()
            endforeach()
            string(REPLACE ";" "," all_links "${all_links}")
            string(APPEND lines "${target} ${all_links}\n")
        endforeach()
    endforeach()
    file(WRITE ${file} "${lines}")
endfunction()


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

weivel_c_write_link_graph(${CMAKE_CURRENT_BINARY_DIR}/links.txt)
