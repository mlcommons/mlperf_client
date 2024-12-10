cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0135 NEW)

# Set the project name and version
project(IHV_NativeOpenVINO VERSION 1.0)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

#Starting from OpenVINO 2024.1, TBB is included

set(OPENVINO_INSTALL_DIR "" CACHE PATH "Path to the OpenVINO installation directory")

include(FetchContent)

if(NOT OPENVINO_INSTALL_DIR)
    message(STATUS "OPENVINO_INSTALL_DIR is not set. Using 2024.5 RC4 OpenVINO GenAI from official source")
	
# Download and unpack the ZIP file
FetchContent_Declare(
  OPENVINO_GENAI
  URL https://storage.openvinotoolkit.org/repositories/openvino_genai/packages/pre-release/2024.5.0.0rc4/openvino_genai_windows_2024.5.0.0rc4_x86_64.zip  # URL to the ZIP file
)

FetchContent_MakeAvailable(OPENVINO_GENAI)
set(OPENVINO_INSTALL_DIR ${openvino_genai_SOURCE_DIR} CACHE PATH "Path to the OpenVINO installation directory" FORCE)
endif()

find_file(OPENVINO_SETUPVARS_PATH NAMES "setupvars.bat" PATHS ${OPENVINO_INSTALL_DIR} NO_DEFAULT_PATH REQUIRED) 

set(OpenVINO_DIR "${OPENVINO_INSTALL_DIR}/runtime/cmake")

find_package(OpenVINO REQUIRED HINTS ${OpenVINO_DIR})
find_package(OpenVINOGenAI REQUIRED HINTS ${OpenVINO_DIR})

set(OPENVINO_COMMAND ${CMAKE_COMMAND} -E env ${OPENVINO_SETUPVARS_PATH} && )

include(ExternalProject)

message(STATUS "Building with IHV NativeOpenvino support...")

# Define directories for Native OpenVINO
set(IHV_NATIVE_OPENVINO_BINARY_DIR ${CMAKE_BINARY_DIR}/IHV/NativeOpenVINO)
set(IHV_NATIVE_OPENVINO_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(IHV_NATIVE_OPENVINO_INSTALL_DIR ${CMAKE_BINARY_DIR}/IHV/NativeOpenVINO/bin)

set(ENV_PATH $ENV{PATH})

if(WIN32)
string(REPLACE "\\" "/" ENV_PATH ${ENV_PATH})
endif()

ExternalProject_Add(ihv_native_openvino
    SOURCE_DIR ${IHV_NATIVE_OPENVINO_SOURCE_DIR}
    BINARY_DIR ${IHV_NATIVE_OPENVINO_BINARY_DIR}
    INSTALL_DIR ${IHV_NATIVE_OPENVINO_INSTALL_DIR}
    BUILD_ALWAYS ON

    # Custom configuration command
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E echo "Configuring IHV NativeOpenVINO for ${CMAKE_BUILD_TYPE}"
    COMMAND ${OPENVINO_COMMAND} ${CMAKE_COMMAND} -B ${IHV_NATIVE_OPENVINO_BINARY_DIR} -S ${IHV_NATIVE_OPENVINO_SOURCE_DIR}
        -G "${CMAKE_GENERATOR}"
        -DCMAKE_INSTALL_PREFIX=${IHV_NATIVE_OPENVINO_INSTALL_DIR}
        -DCMAKE_CXX_STANDARD=20
        -DOPENVINO_INSTALL_DIR=${OPENVINO_INSTALL_DIR}
        -DENV_PATH=${ENV_PATH}

    # Custom build command
    BUILD_COMMAND ${CMAKE_COMMAND} -E echo "Starting IHV NativeOpenVINO build for ${CMAKE_BUILD_TYPE}..."
    COMMAND ${CMAKE_COMMAND} --build ${IHV_NATIVE_OPENVINO_BINARY_DIR} --config $<CONFIG> 

    # Custom install command
    INSTALL_COMMAND ${CMAKE_COMMAND} --install ${IHV_NATIVE_OPENVINO_BINARY_DIR} --config $<CONFIG> --prefix ${IHV_NATIVE_OPENVINO_INSTALL_DIR}/$<CONFIG> 
    LOG_CONFIGURE ON
    LOG_BUILD ON
    LOG_INSTALL ON
)


set_target_properties(ihv_native_openvino PROPERTIES FOLDER "IHV")
set_target_properties(ihv_native_openvino PROPERTIES INCLUDE_DIRECTORIES "${OPENVINO_INSTALL_DIR}/runtime/include")
set_target_properties(ihv_native_openvino PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${OPENVINO_INSTALL_DIR}/runtime/include")