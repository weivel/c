# -DSOURCE_ROOT=<this repository> [-DEVASION=<a source file>]
#
# Fails when the `standard` module holds a default of its own: it may hold none anywhere but in the standard's binary,
# nor add a format, a file name or a variable the standard does not own. Its sources and its public header are read as
# tokens, comments taken out and set aside what the language spells as a string and is no value: a header named by
# `#include "..."`, C++'s `extern "C"` and a symbol's `visibility("default")`. It fails on
#
# - any string literal at all, since every file name, format, id and text the module needs is the standard's, read
#   from its binary;
# - a name of the standard's that compiles one of its values into this binary, its service id macros and the mirror
#   placeholder, where the standard's functions answer the same from its own;
# - a call that reads the environment or opens a file on the module's own account.
#
# With EVASION the file given is read as one more source: how each way around the check stays failing.

cmake_minimum_required(VERSION 3.15)

include(${CMAKE_CURRENT_LIST_DIR}/../read_tokens.cmake)

set(copied_values WEIVEL_STANDARD_SERVICE_CONFIG WEIVEL_STANDARD_SERVICE_SECRETS WEIVEL_STANDARD_SERVICE_PATHS
    WEIVEL_STANDARD_MIRROR_PLACEHOLDER)
set(own_reads getenv getenv_s secure_getenv _wgetenv _wgetenv_s _dupenv_s GetEnvironmentVariableA
    GetEnvironmentVariableW fopen fopen_s freopen _wfopen _wfopen_s open _open _wopen opendir stat lstat _stat _wstat
    CreateFileA CreateFileW FindFirstFileA FindFirstFileW)

file(GLOB_RECURSE sources "${SOURCE_ROOT}/src/standard/*.c" "${SOURCE_ROOT}/src/standard/*.h"
    "${SOURCE_ROOT}/include/weivel/c/standard/*.h")
list(APPEND sources "${SOURCE_ROOT}/include/weivel/c/standard.h")
if(DEFINED EVASION)
    list(APPEND sources "${EVASION}")
endif()

set(offenders "")
foreach(source IN LISTS sources)
    file(READ "${source}" text)
    string(REGEX REPLACE "#[ \t]*include[ \t]*\"[^\"\n]*\"" "" text "${text}")
    string(REGEX REPLACE "extern[ \t]*\"C\"|visibility\\(\"default\"\\)" "" text "${text}")
    read_tokens("${text}" tokens literals)
    list(LENGTH literals literal_count)
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
endforeach()

if(offenders)
    foreach(offender IN LISTS offenders)
        message("${offender}")
    endforeach()
    list(LENGTH offenders offender_count)
    message(FATAL_ERROR "the standard module holds a default of its own in ${offender_count} places")
endif()
list(LENGTH sources source_count)
message(STATUS "the standard module holds no default of its own across ${source_count} sources")
