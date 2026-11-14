# -DFIXTURE=<a project> -DSCENARIO=<name> -DWORK=<a build directory> -DSOURCE_ROOT=... -DSTANDARD_INCLUDE=...
#
# Configures a fixture project that writes its link graph as this repository's build does, in the scenario named, and
# runs calls_nothing_of_the_standard.cmake on that graph, printing what it printed and failing when it fails: how a
# link no fixture line can stand for, one made after the graph used to be written, one through an imported target or
# one in a directory added inside another, stays failing.

cmake_minimum_required(VERSION 3.15)

set(graph "${WORK}/links.txt")
file(REMOVE_RECURSE "${WORK}")
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${FIXTURE}" -B "${WORK}" "-DSCENARIO=${SCENARIO}" "-DGRAPH=${graph}"
    OUTPUT_VARIABLE configured ERROR_VARIABLE configured RESULT_VARIABLE code)
if(NOT code EQUAL 0 OR NOT EXISTS "${graph}")
    message(FATAL_ERROR "the fixture project did not configure or wrote no graph (${code}): ${configured}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" "-DSOURCE_ROOT=${SOURCE_ROOT}" "-DSTANDARD_INCLUDE=${STANDARD_INCLUDE}"
    "-DLINK_GRAPH=${graph}" -P "${CMAKE_CURRENT_LIST_DIR}/calls_nothing_of_the_standard.cmake"
    OUTPUT_VARIABLE checked ERROR_VARIABLE checked RESULT_VARIABLE code)
message("${checked}")
if(NOT code EQUAL 0)
    message(FATAL_ERROR "the check refused the fixture project's graph")
endif()
