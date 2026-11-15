cmake_minimum_required(VERSION 3.15)
project(weivel_c C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

set(WEIVEL_C_CORE_INCLUDE_DIRECTORY ${PROJECT_SOURCE_DIR}/siblings/core/include
    CACHE PATH "The directory that holds the core's weivel/core.h, the only part of the core this layer is built with")
if(NOT EXISTS ${WEIVEL_C_CORE_INCLUDE_DIRECTORY}/weivel/core.h)
    message(FATAL_ERROR "no weivel/core.h under ${WEIVEL_C_CORE_INCLUDE_DIRECTORY}: link or clone the core checkout "
        "at siblings/core, or set WEIVEL_C_CORE_INCLUDE_DIRECTORY to the core's include directory")
endif()

set(WEIVEL_C_STANDARD_INCLUDE_DIRECTORY ${PROJECT_SOURCE_DIR}/siblings/standard/include
    CACHE PATH "The directory that holds the standard's weivel/library.h, whose declaration types the host loads C libraries from; the standard's binary is never linked")
if(NOT EXISTS ${WEIVEL_C_STANDARD_INCLUDE_DIRECTORY}/weivel/library.h)
    message(FATAL_ERROR "no weivel/library.h under ${WEIVEL_C_STANDARD_INCLUDE_DIRECTORY}: link or clone the standard "
        "checkout at siblings/standard, or set WEIVEL_C_STANDARD_INCLUDE_DIRECTORY to the standard's include directory")
endif()

function(weivel_c_strict_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

set(WEIVEL_C_SOURCES
    src/core/load.c src/core/events.c src/core/wrapper.c src/core/calls.c
    src/handles/table.c
    src/timer/timer.c
    src/context/contexts.c src/context/records.c src/context/services.c src/context/capabilities.c
    src/context/capacity.c src/context/dispatch.c
    src/host/entry.c src/host/plugins.c src/host/activation.c src/host/release.c src/host/lifetime.c
    src/host/libraries.c src/host/library_calls.c src/host/library_ids.c src/host/manifests.c)
if(WIN32)
    list(APPEND WEIVEL_C_SOURCES src/core/binary_windows.c src/core/mutex_windows.c src/host/references_windows.c)
else()
    list(APPEND WEIVEL_C_SOURCES src/core/binary_posix.c src/core/mutex_posix.c src/host/references_posix.c)
endif()

function(weivel_c_target target)
    target_include_directories(${target} PUBLIC include ${WEIVEL_C_CORE_INCLUDE_DIRECTORY}
        ${WEIVEL_C_STANDARD_INCLUDE_DIRECTORY})
    target_link_libraries(${target} PUBLIC ${CMAKE_DL_LIBS} Threads::Threads)
    weivel_c_strict_warnings(${target})
endfunction()

add_library(weivel_c STATIC ${WEIVEL_C_SOURCES})
weivel_c_target(weivel_c)
set_target_properties(weivel_c PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(weivel_c_shared SHARED ${WEIVEL_C_SOURCES})
weivel_c_target(weivel_c_shared)
target_compile_definitions(weivel_c_shared PRIVATE WEIVEL_C_BUILDING_SHARED INTERFACE WEIVEL_C_USING_SHARED)
set_target_properties(weivel_c_shared PROPERTIES
    OUTPUT_NAME weivel_c
    C_VISIBILITY_PRESET hidden
    ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/shared)

set(WEIVEL_C_CORE_SOURCE_DIRECTORY ${PROJECT_SOURCE_DIR}/siblings/core
    CACHE PATH "The core checkout whose headers the standard is built with, when no weivel_core target exists yet")
set(WEIVEL_C_STANDARD_SOURCE_DIRECTORY ${PROJECT_SOURCE_DIR}/siblings/standard
    CACHE PATH "The standard checkout the optional standard module links, when no weivel_standard target exists yet")

# Adds the standard's targets, and the core's its build needs, unless the including project already has them.
function(weivel_c_add_the_standard)
    if(TARGET weivel_standard)
        return()
    endif()
    if(NOT TARGET weivel_core)
        add_subdirectory(${WEIVEL_C_CORE_SOURCE_DIRECTORY} ${PROJECT_BINARY_DIR}/core EXCLUDE_FROM_ALL)
    endif()
    add_subdirectory(${WEIVEL_C_STANDARD_SOURCE_DIRECTORY} ${PROJECT_BINARY_DIR}/standard EXCLUDE_FROM_ALL)
    set_target_properties(weivel_standard_shared PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
endfunction()

set(WEIVEL_C_STANDARD_MODULE_SOURCES src/standard/status.c src/standard/manifests.c src/standard/services.c)

# The optional standard module: its own library, the one target of this repository that links the standard.
function(weivel_c_add_the_standard_module)
    weivel_c_add_the_standard()
    add_library(weivel_c_standard STATIC ${WEIVEL_C_STANDARD_MODULE_SOURCES})
    target_link_libraries(weivel_c_standard PUBLIC weivel_c weivel_standard)
    weivel_c_strict_warnings(weivel_c_standard)
    set_target_properties(weivel_c_standard PROPERTIES POSITION_INDEPENDENT_CODE ON)
    add_library(weivel_c_standard_shared SHARED ${WEIVEL_C_STANDARD_MODULE_SOURCES})
    target_link_libraries(weivel_c_standard_shared PUBLIC weivel_c_shared weivel_standard_shared)
    weivel_c_strict_warnings(weivel_c_standard_shared)
    target_compile_definitions(weivel_c_standard_shared
        PRIVATE WEIVEL_C_STANDARD_BUILDING_SHARED INTERFACE WEIVEL_C_STANDARD_USING_SHARED)
    set_target_properties(weivel_c_standard_shared PROPERTIES
        OUTPUT_NAME weivel_c_standard
        C_VISIBILITY_PRESET hidden
        ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/shared)
endfunction()

set(WEIVEL_C_CONFORMANCE_SOURCES
    src/conformance/text.c src/conformance/equality.c src/conformance/vectors.c src/conformance/ids.c
    src/conformance/world.c src/conformance/time.c src/conformance/observe.c src/conformance/consume.c
    src/conformance/want.c src/conformance/actions.c src/conformance/lifecycle.c src/conformance/library.c
    src/conformance/driver.c)
if(WIN32)
    list(APPEND WEIVEL_C_CONFORMANCE_SOURCES src/conformance/directory_windows.c)
else()
    list(APPEND WEIVEL_C_CONFORMANCE_SOURCES src/conformance/directory_posix.c)
endif()

# The conformance module: the layer suite's driver, which reads the kit's vectors with the standard's JSON reader and
# so links the standard, as the standard module does and no host target may.
function(weivel_c_add_the_conformance_module)
    add_library(weivel_c_conformance STATIC ${WEIVEL_C_CONFORMANCE_SOURCES})
    target_link_libraries(weivel_c_conformance PUBLIC weivel_c weivel_standard)
    target_include_directories(weivel_c_conformance PRIVATE ${WEIVEL_C_CORE_INCLUDE_DIRECTORY}/../test/echo)
    weivel_c_strict_warnings(weivel_c_conformance)
endfunction()

if(EXISTS ${WEIVEL_C_STANDARD_SOURCE_DIRECTORY}/CMakeLists.txt OR TARGET weivel_standard)
    weivel_c_add_the_standard_module()
    weivel_c_add_the_conformance_module()
else()
    message(STATUS "no standard at ${WEIVEL_C_STANDARD_SOURCE_DIRECTORY}: the optional standard module and the "
        "conformance module are not built")
endif()

set(WEIVEL_C_BENCHMARKS_SOURCES
    src/benchmarks/measure.c src/benchmarks/budget.c src/benchmarks/pairs.c src/benchmarks/control.c
    src/benchmarks/library.c src/benchmarks/same_language.c)

# The benchmarks module: the layer's half of the platform's benchmarks, its own library, linked by its tests alone.
add_library(weivel_c_benchmarks STATIC ${WEIVEL_C_BENCHMARKS_SOURCES})
target_include_directories(weivel_c_benchmarks PUBLIC src/benchmarks)
target_link_libraries(weivel_c_benchmarks PUBLIC weivel_c)
if(NOT MSVC)
    target_link_libraries(weivel_c_benchmarks PUBLIC m)
endif()
weivel_c_strict_warnings(weivel_c_benchmarks)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    add_library(weivel_c_testing STATIC ${WEIVEL_C_SOURCES})
    weivel_c_target(weivel_c_testing)
    target_compile_definitions(weivel_c_testing PUBLIC WEIVEL_C_TESTING)
    enable_testing()
    add_subdirectory(test)
endif()
