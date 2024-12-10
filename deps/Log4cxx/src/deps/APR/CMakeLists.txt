cmake_minimum_required(VERSION 3.26)
project(APR)

set(APR_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(NOT APPLE)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
set(BUILD_SHARED_LIBS OFF)
set(APR_BUILD_TESTAPR OFF)
set(INSTALL_PDB ON)

add_subdirectory(${APR_SOURCE_DIR} apr)
else()

message(STATUS "Path ${ENV_PATH}")

# Run buildconf to generate configure script
execute_process(
   COMMAND ${CMAKE_COMMAND} -E env "PATH=${ENV_PATH}:$ENV{PATH}" ./buildconf
    WORKING_DIRECTORY ${APR_SOURCE_DIR}
    RESULT_VARIABLE BUILDCONF_RESULT
)

# Check if the buildconf step was successful
if(NOT BUILDCONF_RESULT EQUAL "0")
    message(FATAL_ERROR "buildconf failed")
endif()

# Configuration step with execute_process
execute_process(
    COMMAND ${CMAKE_COMMAND} -E env "CC=${CMAKE_C_COMPILER}" ./configure --prefix=${CMAKE_INSTALL_PREFIX} --enable-static EXTRA_CPPFLAGS='-std=c++20'
    WORKING_DIRECTORY ${APR_SOURCE_DIR}
    RESULT_VARIABLE CONFIGURE_RESULT
)


# Check if the configure step was successful
if(NOT CONFIGURE_RESULT EQUAL "0")
    message(FATAL_ERROR "Configuration failed")
endif()

# Build step with a custom target
add_custom_target(BuildExternal
    COMMAND make
    WORKING_DIRECTORY ${APR_SOURCE_DIR}
    COMMENT "Building APR..."
)

# Install step with a custom target
add_custom_target(InstallExternal ALL
    COMMAND make install
    WORKING_DIRECTORY ${APR_SOURCE_DIR}
    COMMENT "Installing APR..."
    DEPENDS BuildExternal
)

endif()