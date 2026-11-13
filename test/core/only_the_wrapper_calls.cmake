# -DSOURCE_ROOT=<this repository> -DFUNCTIONS=<src/core/functions.h>
#
# Fails when a source of the layer calls a function of the core anywhere but where the wrapper does: src/core/wrapper.c
# creates and destroys the core, drains it and makes the one advance of a due deadline, and src/core/calls.c holds one
# function per other call, each naming one field of the table once, for the wrapper to call. Nothing may name a core
# function directly at all, since the layer links no core.
#
# The first guard is structural and this check is the second: the `weivel_core *` a call needs never leaves
# src/core/wrapper.c, which hands it only to the one function of src/core/calls.c it was given. So outside those two
# files this fails on any mention of a member named like a function of the core, called or not, which is what catches
# a table field copied into a variable or called through a dereference. A member of the layer's own structs is
# therefore never named like a function of the core.

cmake_minimum_required(VERSION 3.15)

set(wrapper_file "src/core/wrapper.c")
set(calls_file "src/core/calls.c")
set(wrapper_calls core_create core_destroy next_event event_release advance)
set(never_in_calls core_create core_destroy next_event event_release)

file(READ "${FUNCTIONS}" list_text)
string(REGEX MATCHALL "FUNCTION\\([a-z_]+\\)" entries "${list_text}")
set(functions "")
foreach(entry IN LISTS entries)
    string(REGEX REPLACE "^FUNCTION\\(([a-z_]+)\\)$" "\\1" name "${entry}")
    list(APPEND functions "${name}")
endforeach()
list(LENGTH functions function_count)
if(function_count EQUAL 0)
    message(FATAL_ERROR "no function read from ${FUNCTIONS}, so nothing was checked")
endif()

file(GLOB_RECURSE sources RELATIVE "${SOURCE_ROOT}"
    "${SOURCE_ROOT}/src/*.c" "${SOURCE_ROOT}/src/*.h" "${SOURCE_ROOT}/include/*.h")
list(LENGTH sources source_count)
if(source_count EQUAL 0)
    message(FATAL_ERROR "no source found under ${SOURCE_ROOT}, so nothing was checked")
endif()

set(offenders "")
set(calls_made 0)
foreach(source IN LISTS sources)
    file(READ "${SOURCE_ROOT}/${source}" text)
    foreach(name IN LISTS functions)
        string(REGEX MATCHALL "(->|\\.)[ ]*${name}[ ]*\\(" through_the_table "${text}")
        list(LENGTH through_the_table times)
        string(REGEX MATCH "(^|[^a-z_])weivel_${name}[ ]*\\(" direct "${text}")
        if(direct)
            list(APPEND offenders "${source} names weivel_${name} directly")
        endif()
        string(REGEX MATCH "(->|\\.)[ \t\r\n]*${name}[^a-z_]" mentioned "${text}")
        if(mentioned AND NOT source STREQUAL wrapper_file AND NOT source STREQUAL calls_file)
            list(APPEND offenders "${source} mentions the table's ${name}, outside the wrapper")
            continue()
        endif()
        if(times EQUAL 0)
            continue()
        endif()
        if(source STREQUAL wrapper_file)
            if(NOT name IN_LIST wrapper_calls)
                list(APPEND offenders "${source} calls ${name} through the table, which only ${calls_file} may")
            endif()
        elseif(source STREQUAL calls_file)
            math(EXPR calls_made "${calls_made} + ${times}")
            if(times GREATER 1 OR name IN_LIST never_in_calls)
                list(APPEND offenders "${source} calls ${name} through the table ${times} times")
            endif()
        else()
            list(APPEND offenders "${source} calls ${name} through the table, outside the wrapper")
        endif()
    endforeach()
endforeach()

list(LENGTH never_in_calls kept_by_the_wrapper)
math(EXPR expected_calls "${function_count} - ${kept_by_the_wrapper}")
if(NOT calls_made EQUAL expected_calls)
    list(APPEND offenders "${calls_file} makes ${calls_made} calls through the table where ${expected_calls} are due")
endif()
if(offenders)
    string(REPLACE ";" "\n" report "${offenders}")
    message(FATAL_ERROR "${report}")
endif()
message(STATUS "only the wrapper calls the ${function_count} functions of the core, across ${source_count} sources")
