cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0135 NEW)

PROJECT(MLPerf
LANGUAGES CXX C
VERSION 1.0.0
)
enable_testing()

set(CMAKE_VERBOSE_MAKEFILE ON CACHE BOOL ON FORCE)



# Specify paths to prebuilt libraries
set(DEPENDENCIES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../deps)
#list(APPEND CMAKE_PREFIX_PATH ${DEPENDENCIES_DIR}/example)

set(CLANG_FORMAT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/.clang-format" CACHE INTERNAL "")

# for clang-tidy (this enable to find system header files).
if(CMAKE_EXPORT_COMPILE_COMMANDS)
set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES})
endif()

# Inclusion configuration.
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(list_of_targets "")

# Projects
add_subdirectory(cil)
add_subdirectory(cli)
if(MLPERF_BUILD_GUI)
    add_subdirectory(gui)
endif()

option(BUILD_TESTS "Build the tests" ON)
if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
