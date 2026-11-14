# -DFORMAT=pe|elf|macho -DREADER=<tool> [-DSYMBOLS=<tool>] -DBINARIES=<file>|<file>...
#
# Fails when a binary the host's targets built imports the standard's binary or the standard module's, or a function of
# either: what calls_nothing_of_the_standard.cmake cannot see in sources or the link graph, read from the binaries the
# build made. PE is read with the MSVC linker's `/dump /imports`, which lists each DLL imported and each function taken
# from it; ELF with `objdump -p` for the libraries needed and `nm -D --undefined-only` for the functions left for the
# loader to find; Mach-O with `otool -L` and `nm -u`. A static library imports nothing, so what it references is read
# from an executable that links it.
#
# What this cannot see: the standard linked statically into a host binary, which leaves no import, and a lookup by a
# name assembled at run time; the link graph reads the first and nothing reads the second.

cmake_minimum_required(VERSION 3.15)

set(names "weivel_(c_)?standard[A-Za-z0-9_.]*|weivel_json_[A-Za-z0-9_]+")

function(read_the_imports file result)
    if(FORMAT STREQUAL "pe")
        set(commands "${READER}|/dump|/imports|${file}")
    elseif(FORMAT STREQUAL "elf")
        set(commands "${READER}|-p|${file}" "${SYMBOLS}|-D|--undefined-only|${file}")
    elseif(FORMAT STREQUAL "macho")
        set(commands "${READER}|-L|${file}" "${SYMBOLS}|-u|${file}")
    else()
        message(FATAL_ERROR "no reader of the imports of a binary of format '${FORMAT}', so nothing was checked")
    endif()
    set(imports "")
    foreach(command IN LISTS commands)
        string(REPLACE "|" ";" command "${command}")
        execute_process(COMMAND ${command} OUTPUT_VARIABLE output ERROR_VARIABLE error RESULT_VARIABLE code)
        if(NOT code EQUAL 0)
            message(FATAL_ERROR "reading the imports of ${file} failed (${code}): ${error}")
        endif()
        string(APPEND imports "${output}")
    endforeach()
    file(TO_NATIVE_PATH "${file}" native)
    string(REPLACE "${file}" "" imports "${imports}")
    string(REPLACE "${native}" "" imports "${imports}")
    set(${result} "${imports}" PARENT_SCOPE)
endfunction()

string(REPLACE "|" ";" binaries "${BINARIES}")
list(LENGTH binaries binary_count)
if(binary_count EQUAL 0)
    message(FATAL_ERROR "no binary given, so nothing was checked")
endif()
set(offenders "")
foreach(binary IN LISTS binaries)
    if(NOT EXISTS "${binary}")
        message(FATAL_ERROR "${binary} was not built, so its imports cannot be read")
    endif()
    read_the_imports("${binary}" imports)
    string(REGEX MATCHALL "${names}" imported "${imports}")
    if(imported)
        list(REMOVE_DUPLICATES imported)
        get_filename_component(name "${binary}" NAME)
        string(REPLACE ";" ", " imported "${imported}")
        list(APPEND offenders "${name} imports the standard: ${imported}")
    endif()
endforeach()
if(offenders)
    foreach(offender IN LISTS offenders)
        message("${offender}")
    endforeach()
    list(LENGTH offenders offender_count)
    message(FATAL_ERROR "a host binary imports the standard in ${offender_count} places")
endif()
message(STATUS "none of the ${binary_count} host binaries imports anything of the standard's")
