# -DSOURCE_ROOT=<this repository> -DSTANDARD_INCLUDE=<the standard's include directory> -DLINK_GRAPH=<links.txt>
# [-DEVASION=<a source file>] [-DLINK_EVASION=<lines of links>]
#
# Fails when the host can reach the standard's binary. The host may use the standard's published declaration types,
# but it never loads, links or calls the standard's binary. Only two modules may: the `standard` module, whose own
# library is weivel_c_standard, and the `conformance` module, weivel_c_conformance, which reads the kit's vectors with
# the standard's JSON reader; each with its tests. Two ways are read here, and imports_nothing_of_the_standard.cmake
# reads a third, the binaries built:
#
# - Every source outside those two modules, under src/, include/ and test/, read as read_tokens.cmake reads it: the
#   strings it spells, adjacent literals joined, an identifier stringized onto them and a run of character literals
#   read as one string. It fails on a name of a function the standard exports, every `WEIVEL_STANDARD_API`
#   declaration of its headers, so a new one is covered unread, also without its `weivel_` for a macro to stringize
#   or paste onto; on a string that spells one, the stem of the
#   standard's names (`weivel_standard`, `weivel_json_`), a name of weivel's around a format conversion, a string
#   that ends in the start of that stem or begins with the end of one of its functions' names, from any of which a
#   lookup by name could be assembled; on a `#pragma comment(lib, ...)` naming the standard; and on a token pasted
#   onto a name of weivel's, which spells a name no reading can see, but for the core's loader pasting the core's own
#   names, which only_the_wrapper_calls.cmake answers for.
# - The link graph link_graph.cmake writes once the whole project is configured: each target of every directory at
#   any depth, imported ones included, and every link item, option and flag it carries. It fails when a target this
#   repository defines, other than those `allowed_to_link_the_standard` names, each by its exact name, reaches through
#   any target in the graph a link item that names the standard (`weivel_standard...`, `weivel_c_standard...`) or
#   weivel_c_conformance, which links the standard: an item is read part by part, generator expressions taken apart
#   and each part's path and extension taken off, so a full path, `-l`, `$<TARGET_FILE:...>` or a configuration's
#   expression name it as plainly as the target's name does. A name is never exempt for how it begins, so a host test
#   named like a standard test is read.
#
# What no reading of tokens can see: a name assembled at run time from pieces none of which holds the stem, the start
# of it or the end of a function's name. Such a lookup imports nothing either, so it is held by review alone.
#
# With EVASION the file given is read as one more source, and with LINK_EVASION its lines are read as more of the
# graph: how each way around the check stays failing.

cmake_minimum_required(VERSION 3.15)

include(${CMAKE_CURRENT_LIST_DIR}/../read_tokens.cmake)

set(stems "weivel_(c_)?standard" "weivel_json_")
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

# Appends to `found` each way one string, `literal`, spells a name of the standard's or a piece of one.
function(check_the_literal source literal functions)
    foreach(name IN LISTS functions)
        string(REGEX REPLACE "^weivel_(standard|json)" "" tail "${name}")
        if(literal MATCHES "${name}")
            list(APPEND found "${source} spells the standard's function ${name} in a string")
        elseif(literal MATCHES "[dn]${tail}")
            list(APPEND found "${source} spells the end of the standard's function ${name} in a string")
        endif()
    endforeach()
    foreach(stem IN LISTS stems)
        if(literal MATCHES "${stem}")
            list(APPEND found "${source} spells ${CMAKE_MATCH_0}, the start of the standard's names, in a string")
        endif()
    endforeach()
    if(literal MATCHES "weivel_[A-Za-z0-9_]*%")
        list(APPEND found "${source} spells a name of weivel's around a format conversion in a string")
    endif()
    if(literal MATCHES "weivel_(s|st|sta|stan|stand|standa|standar|j|js|jso|json)$")
        list(APPEND found "${source} ends a string in ${CMAKE_MATCH_0}, the start of the standard's names")
    endif()
    set(found "${found}" PARENT_SCOPE)
endfunction()

# Appends to `offenders` each way `source`, as its `text`, `tokens` and `literals`, reaches a function of the standard.
function(check_the_tokens source text tokens literals functions)
    set(found "${offenders}")
    if(tokens MATCHES " weivel_(standard|json)[A-Za-z0-9_]*  ## " OR
       (tokens MATCHES " weivel_[A-Za-z0-9_]*  ## " AND NOT source STREQUAL "src/core/load.c"))
        list(APPEND found "${source} pastes tokens together onto a name of weivel's, which this check cannot read")
    endif()
    if(text MATCHES "#[ \t]*pragma[ \t]+comment[ \t]*\\([ \t]*lib[ \t]*,[^)\n]*weivel_(c_)?(standard|json)")
        list(APPEND found "${source} links the standard by #pragma comment(lib, ...)")
    endif()
    foreach(name IN LISTS functions)
        string(REGEX REPLACE "^weivel_" "" unprefixed "${name}")
        if(tokens MATCHES " ${name} ")
            list(APPEND found "${source} names the standard's function ${name}")
        elseif(tokens MATCHES " ${unprefixed} ")
            list(APPEND found "${source} names the standard's function ${name} without its weivel_, to stringize or paste")
        endif()
    endforeach()
    foreach(literal IN LISTS literals)
        check_the_literal("${source}" "${literal}" "${functions}")
    endforeach()
    set(offenders "${found}" PARENT_SCOPE)
endfunction()

function(check_the_sources functions)
    find_the_sources(sources)
    if(DEFINED EVASION)
        list(APPEND sources "${EVASION}")
    endif()
    set(offenders "")
    foreach(source IN LISTS sources)
        set(path "${SOURCE_ROOT}/${source}")
        if(IS_ABSOLUTE "${source}")
            set(path "${source}")
        endif()
        file(READ "${path}" text)
        read_tokens("${text}" tokens literals)
        check_the_tokens("${source}" "${text}" "${tokens}" "${literals}" "${functions}")
    endforeach()
    list(LENGTH sources source_count)
    set(offenders "${offenders}" PARENT_SCOPE)
    set(sources_read ${source_count} PARENT_SCOPE)
endfunction()

# Reads the link graph into a `links_of_<target>` variable per target, `graph_targets` to every target it names and
# `${result}` to those this repository defines.
macro(read_the_graph result)
    file(STRINGS "${LINK_GRAPH}" graph_lines)
    if(DEFINED LINK_EVASION)
        file(STRINGS "${LINK_EVASION}" evasion_lines)
        list(APPEND graph_lines ${evasion_lines})
    endif()
    set(${result} "")
    set(graph_targets "")
    foreach(line IN LISTS graph_lines)
        if(line MATCHES "^(own|other|imported) ([^ ]+) ?(.*)$")
            set(graph_origin "${CMAKE_MATCH_1}")
            set(graph_target "${CMAKE_MATCH_2}")
            string(REPLACE "," ";" graph_links "${CMAKE_MATCH_3}")
            list(APPEND links_of_${graph_target} ${graph_links})
            list(APPEND graph_targets "${graph_target}")
            if(graph_origin STREQUAL "own")
                list(APPEND ${result} "${graph_target}")
            endif()
        endif()
    endforeach()
    list(REMOVE_DUPLICATES ${result})
    list(REMOVE_DUPLICATES graph_targets)
endmacro()

# Sets `${parts}` to the parts of a link item, its generator expressions taken apart, and `${result}` to the item when
# a part, its path and extension taken off, names the standard or the conformance module; else "".
function(read_the_item item parts result)
    string(REGEX REPLACE "[$<>:,; \t]+" ";" item_parts "${item}")
    set(named "")
    foreach(part IN LISTS item_parts)
        string(REGEX REPLACE "^.*[/\\\\]" "" bare "${part}")
        string(REGEX REPLACE "\\..*$" "" bare "${bare}")
        if(bare MATCHES "weivel_(c_)?standard|weivel_c_conformance")
            set(named "${item}")
        endif()
    endforeach()
    set(${parts} "${item_parts}" PARENT_SCOPE)
    set(${result} "${named}" PARENT_SCOPE)
endfunction()

# Sets `${result}` to the item naming the standard that `target` reaches and the target it is reached through, or "".
function(reach_of target result)
    set(waiting "${target}")
    set(seen "${target}")
    set(reached "")
    while(waiting AND NOT reached)
        list(GET waiting 0 current)
        list(REMOVE_AT waiting 0)
        foreach(item IN LISTS links_of_${current})
            read_the_item("${item}" parts named)
            if(named AND NOT reached)
                set(reached "${named} through ${current}")
            endif()
            foreach(part IN LISTS parts)
                if(part IN_LIST graph_targets AND NOT part IN_LIST seen)
                    list(APPEND seen "${part}")
                    list(APPEND waiting "${part}")
                endif()
            endforeach()
        endforeach()
    endwhile()
    set(${result} "${reached}" PARENT_SCOPE)
endfunction()

function(check_the_links)
    read_the_graph(own_targets)
    set(found "")
    foreach(target IN LISTS host_targets)
        if(NOT target IN_LIST own_targets)
            list(APPEND found "${LINK_GRAPH} names no target ${target}, so its links were not checked")
        endif()
    endforeach()
    foreach(target IN LISTS own_targets)
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
