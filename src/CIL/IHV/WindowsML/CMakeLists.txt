cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0135 NEW)

# Set the project name and version
project(IHV_WindowsML VERSION 1.0)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "Unsupported platform.")
endif()

include(ExternalProject)

message(STATUS "Building with IHV WindowsML support...")

# Define directories for ORT GenAI
set(IHV_WIN_ML_BINARY_DIR ${CMAKE_BINARY_DIR}/IHV/WindowsML)
set(IHV_WIN_ML_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(IHV_WIN_ML_INSTALL_DIR ${CMAKE_BINARY_DIR}/IHV/WindowsML/bin)

set(ENV_PATH $ENV{PATH})

if(WIN32)
string(REPLACE "\\" "/" ENV_PATH ${ENV_PATH})
endif()

ExternalProject_Add(ihv_win_ml
    SOURCE_DIR ${IHV_WIN_ML_SOURCE_DIR}
    BINARY_DIR ${IHV_WIN_ML_BINARY_DIR}
    INSTALL_DIR ${IHV_WIN_ML_INSTALL_DIR}
    BUILD_ALWAYS ON

    # Custom configuration command
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E echo "Configuring IHV WindowsML for ${CMAKE_BUILD_TYPE}"
    COMMAND ${CMAKE_COMMAND} -B ${IHV_WIN_ML_BINARY_DIR} -S ${IHV_WIN_ML_SOURCE_DIR}
        -G "${CMAKE_GENERATOR}"
        -A ${CMAKE_GENERATOR_PLATFORM}
        -DCMAKE_INSTALL_PREFIX=${IHV_WIN_ML_INSTALL_DIR}
        -DCMAKE_CXX_STANDARD=20
        -DWIN_ML_INSTALL_DIR=${WIN_ML_INSTALL_DIR}
        -DENV_PATH=${ENV_PATH}

    # Custom build command
    BUILD_COMMAND ${CMAKE_COMMAND} -E echo "Starting IHV WindowsML build for ${CMAKE_BUILD_TYPE}..."
    COMMAND ${CMAKE_COMMAND} --build ${IHV_WIN_ML_BINARY_DIR} --config $<CONFIG> 

    # Custom install command
    INSTALL_COMMAND ${CMAKE_COMMAND} --install ${IHV_WIN_ML_BINARY_DIR} --config $<CONFIG> --prefix ${IHV_WIN_ML_INSTALL_DIR}/$<CONFIG> 
    LOG_CONFIGURE ON
    LOG_BUILD ON
    LOG_INSTALL ON
)


set_target_properties(ihv_win_ml PROPERTIES FOLDER "IHV")