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

# Configuration step with execute_process based on platform
if(TARGET_IOS)

execute_process(
    COMMAND git apply --ignore-whitespace --allow-empty --reject ${CMAKE_CURRENT_SOURCE_DIR}/ios.patch
    WORKING_DIRECTORY ${APR_SOURCE_DIR}
    RESULT_VARIABLE PATCH_RESULT
)

if(NOT PATCH_RESULT EQUAL "0")
    message(WARNING "Patching failed. Proceeding without applying the patch.")
endif()

execute_process(
    COMMAND sh -c "cmake -E env ./configure \
        --prefix=${CMAKE_INSTALL_PREFIX} \
        --host=arm64-apple-darwin \
        --build=aarch64-apple-darwin20 \
        CFLAGS=\"-arch arm64 -isysroot $(xcrun --sdk iphoneos --show-sdk-path) -miphoneos-version-min=18.0\" \
        LDFLAGS=\"-arch arm64 -isysroot $(xcrun --sdk iphoneos --show-sdk-path)\" \
        --enable-static \
        --disable-shared \
        --disable-dso \
        --enable-threads \
        ac_cv_file__dev_zero=yes \
        ac_cv_func_setpgrp_void=yes \
        ac_cv_func_fdatasync=no \
        apr_cv_process_shared_works=yes \
        apr_cv_mutex_robust_shared=no \
        apr_cv_tcp_nodelay_with_cork=yes \
        apr_cv_mutex_recursive=yes \
        ac_cv_sizeof_struct_iovec=8 \
        ac_cv_func_inet_addr=yes \
        ac_cv_func_inet_network=yes"
    WORKING_DIRECTORY ${APR_SOURCE_DIR}
    RESULT_VARIABLE CONFIGURE_RESULT
)
else()
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env "CC=${CMAKE_C_COMPILER}" ./configure --prefix=${CMAKE_INSTALL_PREFIX} --enable-static EXTRA_CPPFLAGS='-std=c++20'
        WORKING_DIRECTORY ${APR_SOURCE_DIR}
        RESULT_VARIABLE CONFIGURE_RESULT
    )
endif()

# Check if the configure step was successful
if(NOT CONFIGURE_RESULT EQUAL "0")
    message(FATAL_ERROR "Configuration failed")
endif()

add_custom_target(CleanExternal
    COMMAND make clean
    WORKING_DIRECTORY ${APR_SOURCE_DIR}
)

# Build step with a custom target
add_custom_target(BuildExternal
    COMMAND make
    WORKING_DIRECTORY ${APR_SOURCE_DIR}
    COMMENT "Building APR..."
    DEPENDS CleanExternal
)

# Install step with a custom target
add_custom_target(InstallExternal ALL
    COMMAND make install
    WORKING_DIRECTORY ${APR_SOURCE_DIR}
    COMMENT "Installing APR..."
    DEPENDS BuildExternal
)

endif()