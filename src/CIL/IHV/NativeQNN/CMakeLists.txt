#[[ Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================
]]
cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0135 NEW)

# Set the project name and version
project(IHV_NativeQNN VERSION 1.0)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include(FetchContent)

if(NOT QNN_INSTALL_DIR)
    message(STATUS "QNN_INSTALL_DIR is not set. Using QAIRT_SDK 2.35 from official source")

# Download and unpack the QAIRT SDK FILE file
FetchContent_Declare(
  native_qnn_qairt_sdk
  URL https://softwarecenter.qualcomm.com/api/download/software/sdks/Qualcomm_AI_Runtime_Community/All/2.35.3.250617/v2.35.3.250617.zip # URL to the QAIRT SDK ZIP file
)

FetchContent_MakeAvailable(native_qnn_qairt_sdk)
set(QNN_INSTALL_DIR "${native_qnn_qairt_sdk_SOURCE_DIR}/2.35.3.250617" CACHE PATH "Path to the QNN installation directory" FORCE)
endif()

find_file(QNN_README NAMES "QNN_README.txt" PATHS ${QNN_INSTALL_DIR} NO_DEFAULT_PATH REQUIRED) 

include(ExternalProject)

source_group(TREE ${QNN_INSTALL_DIR} PREFIX "Genie Header Files" FILES ${QNN_INSTALL_DIR}/include)

message(STATUS "Building with IHV NativeQNN support...")

# Define directories for NativeQnn
set(IHV_NATIVE_QNN_BINARY_DIR ${CMAKE_BINARY_DIR}/IHV/NativeQNN)
set(IHV_NATIVE_QNN_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(IHV_NATIVE_QNN_INSTALL_DIR ${CMAKE_BINARY_DIR}/IHV/NativeQNN/bin)

set(ENV_PATH $ENV{PATH})

if(WIN32)
string(REPLACE "\\" "/" ENV_PATH ${ENV_PATH})
string(REPLACE "\\" "/" QNN_INSTALL_DIR ${QNN_INSTALL_DIR})
endif()

set (IHV_NATIVE_QNN_GENERATOR_PLATFORM_COMMAND -A ${CMAKE_GENERATOR_PLATFORM})

ExternalProject_Add(ihv_native_qnn
    SOURCE_DIR ${IHV_NATIVE_QNN_SOURCE_DIR}
    BINARY_DIR ${IHV_NATIVE_QNN_BINARY_DIR}
    INSTALL_DIR ${IHV_NATIVE_QNN_INSTALL_DIR}
    BUILD_ALWAYS ON

    CONFIGURE_COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" ${IHV_NATIVE_QNN_GENERATOR_PLATFORM_COMMAND} -B ${IHV_NATIVE_QNN_BINARY_DIR} -S ${IHV_NATIVE_QNN_SOURCE_DIR}
        -DCMAKE_INSTALL_PREFIX=${IHV_NATIVE_QNN_INSTALL_DIR}
        -DCMAKE_CXX_STANDARD=20
        -DQNN_INSTALL_DIR=${QNN_INSTALL_DIR}

    # Custom build command
    BUILD_COMMAND ${CMAKE_COMMAND} -E echo "Starting IHV NativeQnn build for ${CMAKE_BUILD_TYPE}..."
    COMMAND ${CMAKE_COMMAND} --build ${IHV_NATIVE_QNN_BINARY_DIR} --config $<CONFIG>

    # Custom install command
    INSTALL_COMMAND ${CMAKE_COMMAND} --install ${IHV_NATIVE_QNN_BINARY_DIR}
    LOG_CONFIGURE ON
    LOG_BUILD ON
    LOG_INSTALL ON
)


set_target_properties(ihv_native_qnn PROPERTIES FOLDER "IHV")
set_target_properties(ihv_native_qnn PROPERTIES INCLUDE_DIRECTORIES "${QNN_INSTALL_DIR}/include")
set_target_properties(ihv_native_qnn PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${QNN_INSTALL_DIR}/include")