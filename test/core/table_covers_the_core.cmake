# -DCORE_HEADER=<the core's weivel/core.h> -DFUNCTIONS=<src/core/functions.h>
#
# Fails when the core's header declares a function the layer's list lacks, or the list names one the header does not
# declare, so a core that grows a function cannot be pinned without the table growing with it.

cmake_minimum_required(VERSION 3.15)

file(READ "${CORE_HEADER}" header)
string(REGEX MATCHALL "WEIVEL_API weivel_status weivel_[a-z_]+\\(" declarations "${header}")
set(declared "")
foreach(declaration IN LISTS declarations)
    string(REGEX REPLACE "^WEIVEL_API weivel_status weivel_([a-z_]+)\\($" "\\1" name "${declaration}")
    list(APPEND declared "${name}")
endforeach()

file(READ "${FUNCTIONS}" list_text)
string(REGEX MATCHALL "FUNCTION\\([a-z_]+\\)" entries "${list_text}")
set(listed "")
foreach(entry IN LISTS entries)
    string(REGEX REPLACE "^FUNCTION\\(([a-z_]+)\\)$" "\\1" name "${entry}")
    list(APPEND listed "${name}")
endforeach()

list(LENGTH declared declared_count)
if(declared_count EQUAL 0)
    message(FATAL_ERROR "no function declaration read from ${CORE_HEADER}, so nothing was checked")
endif()

set(offenders "")
foreach(name IN LISTS declared)
    if(NOT name IN_LIST listed)
        list(APPEND offenders "weivel_${name} is declared by the core and missing from the table")
    endif()
endforeach()
foreach(name IN LISTS listed)
    if(NOT name IN_LIST declared)
        list(APPEND offenders "weivel_${name} is in the table and declared by no core")
    endif()
endforeach()
if(offenders)
    string(REPLACE ";" "\n" report "${offenders}")
    message(FATAL_ERROR "${report}")
endif()
message(STATUS "the table holds all ${declared_count} functions the core declares")
