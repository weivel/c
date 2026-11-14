if(NOT EXISTS ${PROJECT_SOURCE_DIR}/siblings/core/CMakeLists.txt)
    message(FATAL_ERROR "siblings/core is missing: the tests load the real core, so link or clone the core checkout "
        "there (for example a directory junction or symlink named siblings/core pointing at the core repository)")
endif()

add_subdirectory(${PROJECT_SOURCE_DIR}/siblings/core ${PROJECT_BINARY_DIR}/core EXCLUDE_FROM_ALL)

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
    foreach(suite host lifecycle threads)
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

weivel_c_test(host_library_test host/library_test.c weivel_c_testing
    $<TARGET_FILE:weivel_core_shared> $<TARGET_FILE:service_library> $<TARGET_FILE:echo_library>
    ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt)
add_dependencies(host_library_test weivel_core_shared service_library echo_library)
weivel_c_test(host_library_threads_test host/library_threads_test.c weivel_c_testing
    $<TARGET_FILE:weivel_core_shared> $<TARGET_FILE:service_library>)
add_dependencies(host_library_threads_test weivel_core_shared service_library)
weivel_c_test(host_entry_closing_test host/entry_closing_test.c weivel_c_testing
    $<TARGET_FILE:weivel_core_shared> $<TARGET_FILE:entry_library>)
add_dependencies(host_entry_closing_test weivel_core_shared entry_library)

add_test(NAME host_calls_nothing_of_the_standard
    COMMAND ${CMAKE_COMMAND} -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
        -DSTANDARD_INCLUDE=${WEIVEL_C_STANDARD_INCLUDE_DIRECTORY}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/host/calls_nothing_of_the_standard.cmake)

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
