cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0135 NEW)

# Set the project name and version
project(IHV_OrtGenAI_RyzenAI VERSION 1.0)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(ORT_GENAI_PREBUILT_INSTALL_DIR)

if(WIN32 AND MSVC)
	if(CMAKE_GENERATOR_PLATFORM MATCHES "ARM64")
		set(ORT_GENAI_PREBUILT_INSTALL_DIR "${CMAKE_SOURCE_DIR}/deps/ONNXRuntimeGenAi/Windows/MSVC/ARM")
	else()
		set(ORT_GENAI_PREBUILT_INSTALL_DIR "${CMAKE_SOURCE_DIR}/deps/ONNXRuntimeGenAi/Windows/MSVC/x64")
	endif()
else()
    message(FATAL_ERROR "Unsupported platform.")
endif()

set(ORT_GENAI_INSTALL_DIR "${ORT_GENAI_PREBUILT_INSTALL_DIR}" CACHE PATH "Path to the Onnx Runtime GenAI installation directory")

if(NOT ORT_GENAI_INSTALL_DIR)
    message(FATAL_ERROR "ORT_GENAI_INSTALL_DIR is not set. Please set ORT_GENAI_INSTALL_DIR to the OnnxRuntime GenAI installation directory.")
endif()

set(ORT_GENAI_DIR "${ORT_GENAI_INSTALL_DIR}")

include(ExternalProject)

message(STATUS "Building with IHV OnnxRuntime GenAI RyzenAI support...")

# Define directories for ORT GenAI-RyzenAI
set(IHV_ORT_GENAI_BINARY_DIR ${CMAKE_BINARY_DIR}/IHV/OrtGenAI-RyzenAI)
set(IHV_ORT_GENAI_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(IHV_ORT_GENAI_INSTALL_DIR ${CMAKE_BINARY_DIR}/IHV/OrtGenAI-RyzenAI/bin)

set(ENV_PATH $ENV{PATH})

if(WIN32)
string(REPLACE "\\" "/" ENV_PATH ${ENV_PATH})
endif()

ExternalProject_Add(ihv_ort_genai_ryzenai
    SOURCE_DIR ${IHV_ORT_GENAI_SOURCE_DIR}
    BINARY_DIR ${IHV_ORT_GENAI_BINARY_DIR}
    INSTALL_DIR ${IHV_ORT_GENAI_INSTALL_DIR}
    BUILD_ALWAYS ON

    # Custom configuration command
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E echo "Configuring IHV OrtGenAI-RyzenAI for ${CMAKE_BUILD_TYPE}"
    COMMAND ${CMAKE_COMMAND} -B ${IHV_ORT_GENAI_BINARY_DIR} -S ${IHV_ORT_GENAI_SOURCE_DIR}
        -G "${CMAKE_GENERATOR}"
        -A ${CMAKE_GENERATOR_PLATFORM}
        -DCMAKE_INSTALL_PREFIX=${IHV_ORT_GENAI_INSTALL_DIR}
        -DCMAKE_CXX_STANDARD=20
        -DORT_GENAI_INSTALL_DIR=${ORT_GENAI_INSTALL_DIR}
        -DENV_PATH=${ENV_PATH}

    # Custom build command
    BUILD_COMMAND ${CMAKE_COMMAND} -E echo "Starting IHV OrtGenAI-RyzenAI build for ${CMAKE_BUILD_TYPE}..."
    COMMAND ${CMAKE_COMMAND} --build ${IHV_ORT_GENAI_BINARY_DIR} --config $<CONFIG> 

    # Custom install command
    INSTALL_COMMAND ${CMAKE_COMMAND} --install ${IHV_ORT_GENAI_BINARY_DIR} --config $<CONFIG> --prefix ${IHV_ORT_GENAI_INSTALL_DIR}/$<CONFIG> 
    LOG_CONFIGURE ON
    LOG_BUILD ON
    LOG_INSTALL ON
)


set_target_properties(ihv_ort_genai_ryzenai PROPERTIES FOLDER "IHV")