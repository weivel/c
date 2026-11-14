cmake_minimum_required(VERSION 3.21)
project(link_graph_fixture NONE)

include(${CMAKE_CURRENT_LIST_DIR}/../../link_graph.cmake)

weivel_c_write_link_graph_at_the_end(${GRAPH})

add_library(weivel_standard INTERFACE)
add_library(weivel_c INTERFACE)
add_library(weivel_c_shared INTERFACE)
add_library(weivel_c_testing INTERFACE)

if(SCENARIO STREQUAL "imported")
    add_library(standard_copy SHARED IMPORTED)
    set_target_properties(standard_copy PROPERTIES IMPORTED_LOCATION /elsewhere/libweivel_standard.so)
    target_link_libraries(weivel_c_shared INTERFACE standard_copy)
elseif(SCENARIO STREQUAL "nested")
    add_subdirectory(nested)
endif()

if(SCENARIO STREQUAL "late")
    target_link_libraries(weivel_c INTERFACE weivel_standard)
endif()
