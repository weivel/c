if(NOT EXISTS ${PROJECT_SOURCE_DIR}/siblings/core/CMakeLists.txt)
    message(FATAL_ERROR "siblings/core is missing: the tests load the real core, so link or clone the core checkout "
        "there (for example a directory junction or symlink named siblings/core pointing at the core repository)")
endif()

add_subdirectory(${PROJECT_SOURCE_DIR}/siblings/core ${PROJECT_BINARY_DIR}/core EXCLUDE_FROM_ALL)

add_library(partial_core SHARED core/partial_core.c)
weivel_c_strict_warnings(partial_core)
set_target_properties(partial_core PROPERTIES
    C_VISIBILITY_PRESET hidden
    ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/shared)

function(weivel_c_test name source library)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE ${library})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    weivel_c_strict_warnings(${name})
    add_test(NAME ${name} COMMAND ${name} ${ARGN})
    set_tests_properties(${name} PROPERTIES TIMEOUT 60)
endfunction()

foreach(variant "" _shared)
    weivel_c_test(core_load_test${variant} core/load_test.c weivel_c${variant}
        $<TARGET_FILE:weivel_core_shared> $<TARGET_FILE:partial_core> ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt)
    add_dependencies(core_load_test${variant} weivel_core_shared partial_core)
endforeach()

add_test(NAME core_table_covers_the_core
    COMMAND ${CMAKE_COMMAND} -DCORE_HEADER=${WEIVEL_C_CORE_INCLUDE_DIRECTORY}/weivel/core.h
        -DFUNCTIONS=${PROJECT_SOURCE_DIR}/src/core/functions.h
        -P ${CMAKE_CURRENT_SOURCE_DIR}/core/table_covers_the_core.cmake)
