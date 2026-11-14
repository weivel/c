# -DSOURCE_ROOT=<this repository> -DFUNCTIONS=<src/core/functions.h>
# [-DEVASION=<a file of test/core/evasions> -DEVASION_STANDS_AS=<a source path under SOURCE_ROOT>]
#
# Fails when a source of the layer could reach a function of the core anywhere but where the wrapper does:
# src/core/wrapper.c creates and destroys the core, drains it and makes the one advance of a due deadline, and
# src/core/calls.c holds one function per other call, each calling one field of the table once, for the wrapper to call.
#
# The first guard is structural and this check is the second: the `weivel_core *` a call needs never leaves
# src/core/wrapper.c, which hands it only to the one function of src/core/calls.c it was given. So this reads every
# source as its tokens, with comments, string and character literals taken out first, and fails on:
#
# - a name equal to a function of the core anywhere but in the list of them and the loader, which may name any; the
#   header that declares the table, once for its field; and the two calling files, where each stands exactly as often
#   as it is due and every one of those is a call through the table, `->name(` or `.name(`. That catches a field
#   reached through a macro's argument, past a comment, or copied into a variable and called from there. A name of the
#   layer's own is therefore never spelled like a function of the core;
# - the name of a core function itself, `weivel_` and all, since the layer links no core;
# - an offset taken into the table outside the loader, which reaches a field without naming it;
# - two tokens pasted together outside the loader, which spells a name this check cannot read.
#
# With EVASION the file given is read as though it were appended to EVASION_STANDS_AS: how each way around the check
# once found is kept failing.

cmake_minimum_required(VERSION 3.15)

set(list_file "src/core/functions.h")
set(table_header "include/weivel/c/core.h")
set(loader_file "src/core/load.c")
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
if(DEFINED EVASION)
    if(NOT EXISTS "${EVASION}")
        message(FATAL_ERROR "no evasion at ${EVASION}")
    endif()
    if(NOT EVASION_STANDS_AS IN_LIST sources)
        list(APPEND sources "${EVASION_STANDS_AS}")
    endif()
endif()

# Sets `${result}` to the text of `source`, with the evasion standing as it appended.
function(read_source source result)
    set(text "")
    if(EXISTS "${SOURCE_ROOT}/${source}")
        file(READ "${SOURCE_ROOT}/${source}" text)
    endif()
    if(DEFINED EVASION AND source STREQUAL EVASION_STANDS_AS)
        file(READ "${EVASION}" evasion_text)
        string(APPEND text "\n${evasion_text}")
    endif()
    set(${result} "${text}" PARENT_SCOPE)
endfunction()

# Sets `${result}` to the tokens of `text` this check reads, each between two spaces of its own: names, `->`, `.`,
# `(`, `)` and `##`. A comment or a literal is matched whole first, so nothing inside one is ever read as a token.
function(read_tokens text result)
    string(REPLACE ";" " " text "${text}")
    set(block_comment "/\\*([^*]|\\*+[^*/])*\\*+/")
    set(line_comment "//[^\n]*")
    set(string_literal "\"([^\"\\\\\n]|\\\\.)*\"")
    set(character_literal "'([^'\\\\\n]|\\\\.)*'")
    string(REGEX MATCHALL
        "${block_comment}|${line_comment}|${string_literal}|${character_literal}|[A-Za-z_][A-Za-z0-9_]*|->|##|[().]"
        matched "${text}")
    set(tokens "")
    foreach(token IN LISTS matched)
        if(token MATCHES "^([A-Za-z_][A-Za-z0-9_]*|->|##|[().])$")
            string(APPEND tokens " ${token} ")
        endif()
    endforeach()
    set(${result} " ${tokens} " PARENT_SCOPE)
endfunction()

# Sets `${result}` to how often `source` must name the function `name`, or to "any".
function(mentions_due source name result)
    set(due 0)
    if(source STREQUAL list_file OR source STREQUAL loader_file)
        set(due "any")
    elseif(source STREQUAL table_header)
        set(due 1)
    elseif(source STREQUAL wrapper_file AND name IN_LIST wrapper_calls)
        set(due 1)
    elseif(source STREQUAL calls_file AND NOT name IN_LIST never_in_calls)
        set(due 1)
    endif()
    set(${result} ${due} PARENT_SCOPE)
endfunction()

# Appends to `offenders` what `source` does that only the loader may: take an offset into the table, paste tokens.
function(check_what_only_the_loader_does source tokens)
    if(source STREQUAL loader_file)
        return()
    endif()
    set(found "${offenders}")
    if(tokens MATCHES " ## ")
        list(APPEND found "${source} pastes tokens together, which only ${loader_file} may")
    endif()
    if(tokens MATCHES " offsetof  \\(  weivel_c_core_table ")
        list(APPEND found "${source} takes an offset into the table, which only ${loader_file} may")
    endif()
    set(offenders "${found}" PARENT_SCOPE)
endfunction()

# Appends to `offenders` how `source` names the function `name` where it should not.
function(check_the_function source tokens name)
    set(found "${offenders}")
    if(tokens MATCHES " weivel_${name} ")
        list(APPEND found "${source} names weivel_${name} directly")
    endif()
    mentions_due("${source}" "${name}" due)
    if(NOT due STREQUAL "any")
        string(REGEX MATCHALL " ${name} " mentioned "${tokens}")
        list(LENGTH mentioned mentions)
        if(NOT mentions EQUAL due)
            list(APPEND found "${source} names the table's ${name} ${mentions} times where ${due} are due")
        elseif(source STREQUAL wrapper_file OR source STREQUAL calls_file)
            string(REGEX MATCHALL " (->|\\.)  ${name}  \\( " called "${tokens}")
            list(LENGTH called calls)
            if(NOT calls EQUAL due)
                list(APPEND found "${source} names the table's ${name} without calling it through the table")
            endif()
        endif()
    endif()
    set(offenders "${found}" PARENT_SCOPE)
endfunction()

set(offenders "")
foreach(source IN LISTS sources)
    read_source("${source}" text)
    read_tokens("${text}" tokens)
    check_what_only_the_loader_does("${source}" "${tokens}")
    foreach(name IN LISTS functions)
        check_the_function("${source}" "${tokens}" "${name}")
    endforeach()
endforeach()

if(offenders)
    foreach(offender IN LISTS offenders)
        message("${offender}")
    endforeach()
    list(LENGTH offenders offender_count)
    message(FATAL_ERROR "the core can be reached past the wrapper in ${offender_count} places")
endif()
message(STATUS "only the wrapper calls the ${function_count} functions of the core, across ${source_count} sources")
