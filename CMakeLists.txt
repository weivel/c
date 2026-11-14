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
    src/host/libraries.c src/host/library_calls.c)
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

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    add_library(weivel_c_testing STATIC ${WEIVEL_C_SOURCES})
    weivel_c_target(weivel_c_testing)
    target_compile_definitions(weivel_c_testing PUBLIC WEIVEL_C_TESTING)
    enable_testing()
    add_subdirectory(test)
endif()
