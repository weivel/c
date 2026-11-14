# -DSOURCE_ROOT=<this repository> -DSTANDARD_INCLUDE=<the standard's include directory> [-DEVASION=<a source file>]
#
# Fails when a source of the layer names a function the standard exports: the host may use the standard's published
# declaration types, but it never loads, links or calls the standard's binary, so a function of it named anywhere
# under src/ or include/ is a call or an address the layer could only meet by linking the standard. The functions are
# read from the standard's own headers, every `WEIVEL_STANDARD_API` declaration, so a new one is covered unread. With
# EVASION the file given is read as one more source.

cmake_minimum_required(VERSION 3.15)

file(GLOB standard_headers "${STANDARD_INCLUDE}/weivel/*.h")
set(functions "")
foreach(header IN LISTS standard_headers)
    file(READ "${header}" header_text)
    string(REGEX MATCHALL "WEIVEL_STANDARD_API[^;(]*[ *](weivel_standard_[a-z0-9_]+)[ \t\r\n]*\\(" declarations
        "${header_text}")
    foreach(declaration IN LISTS declarations)
        string(REGEX MATCH "weivel_standard_[a-z0-9_]+[ \t\r\n]*\\($" name "${declaration}")
        string(REGEX REPLACE "[ \t\r\n]*\\($" "" name "${name}")
        list(APPEND functions "${name}")
    endforeach()
endforeach()
list(LENGTH functions function_count)
if(function_count EQUAL 0)
    message(FATAL_ERROR "no function of the standard read from ${STANDARD_INCLUDE}, so nothing was checked")
endif()

file(GLOB_RECURSE sources "${SOURCE_ROOT}/src/*.c" "${SOURCE_ROOT}/src/*.h" "${SOURCE_ROOT}/include/*.h")
if(DEFINED EVASION)
    list(APPEND sources "${EVASION}")
endif()

set(offenders 0)
foreach(source IN LISTS sources)
    file(READ "${source}" text)
    string(REGEX MATCHALL "weivel_standard_[a-z0-9_]+" names "${text}")
    foreach(name IN LISTS names)
        if(name IN_LIST functions)
            message(SEND_ERROR "${source} names the standard's function ${name}; the host never calls the standard")
            math(EXPR offenders "${offenders} + 1")
        endif()
    endforeach()
endforeach()
if(offenders GREATER 0)
    message(FATAL_ERROR "${offenders} names of the standard's functions in the layer's sources")
endif()
message(STATUS "the layer names none of the standard's ${function_count} functions")
