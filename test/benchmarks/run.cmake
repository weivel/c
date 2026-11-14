# -DPROGRAM=<exe> -DARGUMENTS=<argument>|<argument>... -DEXPECTED_EXIT=<n> -DCONFIG=<config> [-DMUST_MATCH=<regex>]
# [-DRELEASE_ONLY=ON]
#
# Runs one mode of the benchmarks and holds it to the exit and the output expected, as the kit's benchmark_test.cmake
# holds the core's. A timing verdict from an unoptimised build means nothing, so a RELEASE_ONLY test skips (exit 77)
# anywhere but a Release build, which the benchmark job of CI is.

cmake_minimum_required(VERSION 3.15)

if(RELEASE_ONLY AND NOT CONFIG STREQUAL "Release")
    message("SKIP: a timing verdict is a Release-build figure and this is a '${CONFIG}' build")
    cmake_language(EXIT 77)
endif()

string(REPLACE "|" ";" arguments "${ARGUMENTS}")
execute_process(COMMAND "${PROGRAM}" ${arguments} RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
set(output "${out}${err}")
message("${output}")
if(NOT code STREQUAL EXPECTED_EXIT)
    message(FATAL_ERROR "exit ${code}, expected ${EXPECTED_EXIT}")
endif()
if(DEFINED MUST_MATCH AND NOT output MATCHES "${MUST_MATCH}")
    message(FATAL_ERROR "the output does not match '${MUST_MATCH}'")
endif()
