cmake_minimum_required(VERSION 3.26)
project(APR-Iconv)

set(APR_UTIL_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(NOT APPLE)

message(FATAL ERROR "APR-Iconv not available for non-UNIX")

else()

set(APR_ICONV_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

include(FetchContent)

# Fetch ONNX Runtime source code
FetchContent_Declare(
    apr-iconv
    GIT_REPOSITORY https://github.com/apache/apr-iconv.git
    GIT_TAG trunk
    SOURCE_DIR     ${APR_ICONV_SOURCE_DIR}
)
FetchContent_Populate(apr-iconv)

execute_process(
   COMMAND ${CMAKE_COMMAND} -E env "PATH=${ENV_PATH}:$ENV{PATH}" ./buildconf --with-apr=${APR_SOURCE_DIR}/src
    WORKING_DIRECTORY ${APR_ICONV_SOURCE_DIR}
    RESULT_VARIABLE BUILDCONF_RESULT
)

if(NOT BUILDCONF_RESULT EQUAL "0")
    message(FATAL_ERROR "buildconf failed")
endif()


execute_process(
    COMMAND ${CMAKE_COMMAND} -E env "CC=${CMAKE_C_COMPILER}" ./configure --prefix=${CMAKE_INSTALL_PREFIX} EXTRA_CPPFLAGS='-std=c++20' --with-apr=${APR_INSTALL_DIR}
    WORKING_DIRECTORY ${APR_ICONV_SOURCE_DIR}
    RESULT_VARIABLE CONFIGURE_RESULT
)

if(NOT CONFIGURE_RESULT EQUAL "0")
    message(FATAL_ERROR "Configuration failed")
endif()

add_custom_target(BuildExternal
    COMMAND make
    WORKING_DIRECTORY ${APR_ICONV_SOURCE_DIR}
    COMMENT "Building APR-Iconv..."
)

add_custom_target(InstallExternal ALL
    COMMAND make install && make clean
    WORKING_DIRECTORY ${APR_ICONV_SOURCE_DIR}
    COMMENT "Installing APR-Iconv..."
    DEPENDS BuildExternal
)


endif()


