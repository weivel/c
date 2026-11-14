# -DSOURCE_ROOT=<this repository> -DSTANDARD_INCLUDE=<the standard's include directory> -DLINK_GRAPH=<links.txt>
# [-DEVASION=<a source file>] [-DLINK_EVASION=<lines of links>]
#
# Fails when the host can reach the standard's binary. The host may use the standard's published declaration types,
# but it never loads, links or calls the standard's binary. Only two modules may: the `standard` module, whose own
# library is weivel_c_standard, and the `conformance` module, weivel_c_conformance, which reads the kit's vectors with
# the standard's JSON reader; each with its tests. Two ways are read:
#
# - Every source outside those two modules, under src/, include/ and test/, read as its tokens with comments taken
#   out and adjacent string literals joined, as the compiler joins them. It fails on a name of a function the standard
#   exports, every `WEIVEL_STANDARD_API` declaration of its headers, so a new one is covered unread; on a string that
#   spells one, or holds the start of the standard's names (`weivel_standard_`, `weivel_json_`), from which a lookup by
#   name could be assembled; and on a token pasted onto a name of weivel's, which spells a name no reading can see,
#   but for the core's loader pasting the core's own names, which only_the_wrapper_calls.cmake answers for.
# - The link graph the build wrote: each target of this repository and what it links, followed through every target
#   the graph names. It fails when a target other than those `allowed_to_link_the_standard` names, each by its exact
#   name, reaches a target of the standard's (`weivel_standard...`), weivel_c_standard, or weivel_c_conformance, which
#   links the standard. A name is never exempt for how it begins, so a host test named like a standard test is read.
#
# With EVASION the file given is read as one more source, and with LINK_EVASION its lines are read as more of the
# graph: how each way around the check stays failing.

cmake_minimum_required(VERSION 3.15)

include(${CMAKE_CURRENT_LIST_DIR}/../read_tokens.cmake)

set(names_prefixes "weivel_standard_" "weivel_json_")
set(allowed_to_link_the_standard weivel_c_standard weivel_c_standard_shared weivel_c_conformance
    conformance_layer_vectors_test conformance_coverage_test)
foreach(suite manifest services library discovery)
    list(APPEND allowed_to_link_the_standard standard_${suite}_test standard_${suite}_test_shared)
endforeach()
set(host_targets weivel_c weivel_c_shared weivel_c_testing)

# Sets `${result}` to every function the standard's headers export.
function(read_the_standards_functions result)
    file(GLOB standard_headers "${STANDARD_INCLUDE}/weivel/*.h")
    set(functions "")
    foreach(header IN LISTS standard_headers)
        file(READ "${header}" header_text)
        string(REGEX MATCHALL "WEIVEL_STANDARD_API[^;(#]*[ *](weivel_[a-z0-9_]+)[ \t\r\n]*\\(" declarations
            "${header_text}")
        foreach(declaration IN LISTS declarations)
            string(REGEX MATCH "weivel_[a-z0-9_]+[ \t\r\n]*\\($" name "${declaration}")
            string(REGEX REPLACE "[ \t\r\n]*\\($" "" name "${name}")
            list(APPEND functions "${name}")
        endforeach()
    endforeach()
    set(${result} "${functions}" PARENT_SCOPE)
endfunction()

# Sets `${result}` to the sources read: every C source and header of src/, include/ and test/ but the `standard` and
# `conformance` modules' own and the fixtures that must fail this check.
function(find_the_sources result)
    file(GLOB_RECURSE found RELATIVE "${SOURCE_ROOT}"
        "${SOURCE_ROOT}/src/*.c" "${SOURCE_ROOT}/src/*.h" "${SOURCE_ROOT}/include/*.h"
        "${SOURCE_ROOT}/test/*.c" "${SOURCE_ROOT}/test/*.h")
    list(FILTER found EXCLUDE REGEX
        "^(src/(standard|conformance)/|test/(standard|conformance)/|include/weivel/c/(standard|conformance)[./]|test/host/standard_evasions/)")
    set(${result} "${found}" PARENT_SCOPE)
endfunction()

# Appends to `offenders` each way `source`, as its `tokens` and `literals`, reaches a function of the standard.
function(check_the_tokens source tokens literals functions)
    set(found "${offenders}")
    if(tokens MATCHES " weivel_(standard|json)[A-Za-z0-9_]*  ## " OR
       (tokens MATCHES " weivel_[A-Za-z0-9_]*  ## " AND NOT source STREQUAL "src/core/load.c"))
        list(APPEND found "${source} pastes tokens together onto a name of weivel's, which this check cannot read")
    endif()
    foreach(name IN LISTS functions)
        if(tokens MATCHES " ${name} ")
            list(APPEND found "${source} names the standard's function ${name}")
        endif()
    endforeach()
    foreach(literal IN LISTS literals)
        foreach(name IN LISTS functions)
            if(literal MATCHES "${name}")
                list(APPEND found "${source} spells the standard's function ${name} in a string")
            endif()
        endforeach()
        foreach(prefix IN LISTS names_prefixes)
            if(literal MATCHES "${prefix}")
                list(APPEND found "${source} spells ${prefix}, the start of the standard's names, in a string")
            endif()
        endforeach()
    endforeach()
    set(offenders "${found}" PARENT_SCOPE)
endfunction()

function(check_the_sources functions)
    find_the_sources(sources)
    if(DEFINED EVASION)
        list(APPEND sources "${EVASION}")
    endif()
    set(found "")
    foreach(source IN LISTS sources)
        set(path "${SOURCE_ROOT}/${source}")
        if(IS_ABSOLUTE "${source}")
            set(path "${source}")
        endif()
        file(READ "${path}" text)
        read_tokens("${text}" tokens literals)
        set(offenders "${found}")
        check_the_tokens("${source}" "${tokens}" "${literals}" "${functions}")
        set(found "${offenders}")
    endforeach()
    list(LENGTH sources source_count)
    set(offenders "${found}" PARENT_SCOPE)
    set(sources_read ${source_count} PARENT_SCOPE)
endfunction()

# Reads the link graph into a `links_of_<target>` variable per target, and `${result}` to its targets.
macro(read_the_graph result)
    file(STRINGS "${LINK_GRAPH}" graph_lines)
    if(DEFINED LINK_EVASION)
        file(STRINGS "${LINK_EVASION}" evasion_lines)
        list(APPEND graph_lines ${evasion_lines})
    endif()
    set(${result} "")
    foreach(line IN LISTS graph_lines)
        if(line MATCHES "^([^ ]+) ?(.*)$")
            set(graph_target "${CMAKE_MATCH_1}")
            string(REGEX REPLACE "\\$<LINK_ONLY:([^>]*)>" "\\1" graph_links "${CMAKE_MATCH_2}")
            string(REPLACE "," ";" graph_links "${graph_links}")
            list(APPEND links_of_${graph_target} ${graph_links})
            list(APPEND ${result} "${graph_target}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES ${result})
endmacro()

# Sets `${result}` to the standard's target `target` reaches and the target it is reached through, or to "".
function(reach_of target result)
    set(waiting "${target}")
    set(seen "")
    set(reached "")
    while(waiting AND NOT reached)
        list(GET waiting 0 current)
        list(REMOVE_AT waiting 0)
        foreach(link IN LISTS links_of_${current})
            if(link MATCHES "^weivel_(c_)?standard" OR link STREQUAL "weivel_c_conformance")
                set(reached "${link} through ${current}")
            elseif(NOT link IN_LIST seen)
                list(APPEND seen "${link}")
                list(APPEND waiting "${link}")
            endif()
        endforeach()
    endwhile()
    set(${result} "${reached}" PARENT_SCOPE)
endfunction()

function(check_the_links)
    read_the_graph(graph_targets)
    set(found "")
    foreach(target IN LISTS host_targets)
        if(NOT target IN_LIST graph_targets)
            list(APPEND found "${LINK_GRAPH} names no target ${target}, so its links were not checked")
        endif()
    endforeach()
    foreach(target IN LISTS graph_targets)
        if(NOT target IN_LIST allowed_to_link_the_standard)
            reach_of("${target}" reached)
            if(reached)
                list(APPEND found "${target} links the standard's ${reached}; only the standard and conformance modules and their tests may")
            endif()
        endif()
    endforeach()
    list(LENGTH graph_targets target_count)
    set(link_offenders "${found}" PARENT_SCOPE)
    set(targets_read ${target_count} PARENT_SCOPE)
endfunction()

read_the_standards_functions(functions)
list(LENGTH functions function_count)
if(function_count EQUAL 0)
    message(FATAL_ERROR "no function of the standard read from ${STANDARD_INCLUDE}, so nothing was checked")
endif()
set(offenders "")
check_the_sources("${functions}")
check_the_links()
list(APPEND offenders ${link_offenders})
if(offenders)
    foreach(offender IN LISTS offenders)
        message("${offender}")
    endforeach()
    list(LENGTH offenders offender_count)
    message(FATAL_ERROR "the host can reach the standard in ${offender_count} places")
endif()
message(STATUS "the host names none of the standard's ${function_count} functions across ${sources_read} sources, "
    "and no target of the ${targets_read} links the standard but the standard module's")
