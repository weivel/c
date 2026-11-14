# -DSOURCE_ROOT=<this repository> -DSTANDARD_INCLUDE=<the standard's include directory>
# -DDEFINITIONS=<the module targets' definitions> [-DEVASION=<a source file>] [-DDEFINITIONS_EVASION=<more lines>]
#
# Fails when the `standard` module holds a default of its own: it may hold none anywhere but in the standard's binary,
# nor add a format, a file name or a variable the standard does not own. Its sources and its public header are read as
# read_tokens.cmake reads them, comments taken out and set aside what the language spells as a string and is no value:
# a header named by `#include "..."`, C++'s `extern "C"` and a symbol's `visibility("default")`. It fails on
#
# - any string at all: a string literal, an identifier stringized by `#`, or a character literal, since every file
#   name, format, id and text the module needs is the standard's, read from its binary;
# - a name of the standard's that compiles one of its values into this binary, its service id macros and the mirror
#   placeholder, where the standard's functions answer the same from its own;
# - a name that reads the environment or opens a file on the module's own account, the macro forms the platform's
#   headers map onto a narrow or wide function among them;
# - a header named by `#include "..."` that leaves src/standard, other than the module's own public header and the
#   standard's headers, since a file outside the module is never read here;
# - a definition the build gives the module's targets that carries a value (`NAME=...`), which compiles a value into
#   the binary no source spells; DEFINITIONS holds each, one `<target> <definition>` a line, written at configure.
#
# With EVASION the file given is read as one more source, and with DEFINITIONS_EVASION its lines as more definitions:
# how each way around the check stays failing.

cmake_minimum_required(VERSION 3.15)

include(${CMAKE_CURRENT_LIST_DIR}/../read_tokens.cmake)

set(copied_values WEIVEL_STANDARD_SERVICE_CONFIG WEIVEL_STANDARD_SERVICE_SECRETS WEIVEL_STANDARD_SERVICE_PATHS
    WEIVEL_STANDARD_MIRROR_PLACEHOLDER)
set(own_reads getenv getenv_s secure_getenv _wgetenv _wgetenv_s _dupenv_s _wdupenv_s environ _environ __environ
    _wenviron GetEnvironmentVariable GetEnvironmentVariableA GetEnvironmentVariableW GetEnvironmentStrings
    GetEnvironmentStringsA GetEnvironmentStringsW ExpandEnvironmentStrings ExpandEnvironmentStringsA
    ExpandEnvironmentStringsW fopen fopen_s freopen freopen_s _wfopen _wfopen_s _fsopen _wfsopen fdopen _fdopen open
    _open _wopen _sopen _sopen_s openat access _access _waccess _access_s faccessat opendir fdopendir stat lstat _stat
    _wstat CreateFile CreateFileA CreateFileW CreateFile2 OpenFile FindFirstFile FindFirstFileA FindFirstFileW
    FindFirstFileEx FindFirstFileExA FindFirstFileExW)

# Appends to `offenders` each header `source` names by `#include "..."` that leaves the module.
function(check_the_quoted_includes source text)
    get_filename_component(directory "${source}" DIRECTORY)
    string(REGEX MATCHALL "#[ \t]*include[ \t]*\"[^\"\n]*\"" includes "${text}")
    foreach(include IN LISTS includes)
        string(REGEX REPLACE "^#[ \t]*include[ \t]*\"([^\"\n]*)\"$" "\\1" named "${include}")
        get_filename_component(resolved "${named}" ABSOLUTE BASE_DIR "${directory}")
        string(FIND "${resolved}" "${SOURCE_ROOT}/src/standard/" inside)
        if(EXISTS "${resolved}" AND inside EQUAL 0)
        elseif(named MATCHES "^weivel/c/standard(\\.h|/)" OR EXISTS "${STANDARD_INCLUDE}/${named}")
        else()
            list(APPEND offenders "${source} includes \"${named}\", which leaves src/standard")
        endif()
    endforeach()
    set(offenders "${offenders}" PARENT_SCOPE)
endfunction()

function(check_the_source source)
    file(READ "${source}" text)
    check_the_quoted_includes("${source}" "${text}")
    string(REGEX REPLACE "#[ \t]*include[ \t]*\"[^\"\n]*\"" "" text "${text}")
    string(REGEX REPLACE "extern[ \t]*\"C\"|visibility\\(\"default\"\\)" "" text "${text}")
    read_tokens("${text}" tokens literals literal_count)
    if(literal_count GREATER 0)
        list(APPEND offenders "${source} holds a string literal, which only the standard's binary may")
    endif()
    foreach(name IN LISTS copied_values)
        if(tokens MATCHES " ${name} ")
            list(APPEND offenders "${source} names ${name}, compiling the standard's value into this binary")
        endif()
    endforeach()
    foreach(name IN LISTS own_reads)
        if(tokens MATCHES " ${name} ")
            list(APPEND offenders "${source} names ${name}, reading the environment or a file on its own account")
        endif()
    endforeach()
    set(offenders "${offenders}" PARENT_SCOPE)
endfunction()

function(check_the_definitions)
    if(NOT EXISTS "${DEFINITIONS}")
        message(FATAL_ERROR "no definitions of the module's targets at '${DEFINITIONS}', so they were not checked")
    endif()
    file(STRINGS "${DEFINITIONS}" lines)
    if(DEFINED DEFINITIONS_EVASION)
        file(STRINGS "${DEFINITIONS_EVASION}" more)
        list(APPEND lines ${more})
    endif()
    foreach(line IN LISTS lines)
        if(line MATCHES "^([^ ]+) (.*=.*)$")
            list(APPEND offenders "${CMAKE_MATCH_1} defines ${CMAKE_MATCH_2}, compiling a value into the module")
        endif()
    endforeach()
    set(offenders "${offenders}" PARENT_SCOPE)
endfunction()

file(GLOB_RECURSE sources "${SOURCE_ROOT}/src/standard/*.c" "${SOURCE_ROOT}/src/standard/*.h"
    "${SOURCE_ROOT}/include/weivel/c/standard/*.h")
list(APPEND sources "${SOURCE_ROOT}/include/weivel/c/standard.h")
if(DEFINED EVASION)
    list(APPEND sources "${EVASION}")
endif()

set(offenders "")
foreach(source IN LISTS sources)
    check_the_source("${source}")
endforeach()
check_the_definitions()

if(offenders)
    foreach(offender IN LISTS offenders)
        message("${offender}")
    endforeach()
    list(LENGTH offenders offender_count)
    message(FATAL_ERROR "the standard module holds a default of its own in ${offender_count} places")
endif()
list(LENGTH sources source_count)
message(STATUS "the standard module holds no default of its own across ${source_count} sources")
