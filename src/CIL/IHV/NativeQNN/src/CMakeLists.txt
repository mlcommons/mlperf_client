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

project(IHV_NativeQNN VERSION 1.0)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(WIN32)
    set(CMAKE_GENERATOR "Visual Studio 17 2022" CACHE STRING "CMake Generator")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set a separate directory for intermediate build files
set(CMAKE_INTERMEDIATE_DIR "${CMAKE_BINARY_DIR}/Build")

if("${CMAKE_RUNTIME_OUTPUT_DIRECTORY}" STREQUAL "")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_INTERMEDIATE_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_INTERMEDIATE_DIR}")
message(STATUS "Output directory: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

set(CMAKE_CONFIGURATION_TYPES "Release;Debug;RelWithDebInfo" CACHE STRING "List of build configurations" FORCE)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build, options are: Debug Release RelWithDebInfo." FORCE)
endif()

add_compile_definitions(
  $<$<CONFIG:Debug>:DEBUG_BUILD>
  $<$<CONFIG:Release>:RELEASE_BUILD>
  $<$<CONFIG:RelWithDebInfo>:RELWITHDEBINFO_BUILD>
)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(QNN_INSTALL_DIR "" CACHE PATH "Path to the QNN installation directory")

if(NOT QNN_INSTALL_DIR)
	message(FATAL_ERROR "QNN_INSTALL_DIR is not set. Please set QNN_INSTALL_DIR to the QNN installation directory.")
else()
	find_file(QNN_README NAMES "QNN_README.txt" PATHS ${QNN_INSTALL_DIR} NO_DEFAULT_PATH REQUIRED)
endif()

# Force use of the release runtime library in all configurations
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:>")
endif()

# Define the source files for the library
file(GLOB_RECURSE SOURCES "*.cpp")

# Define the header files for the library
file(GLOB_RECURSE HEADERS "*.h")
file(GLOB_RECURSE GENIE_HEADERS "${QNN_INSTALL_DIR}/include/*.h")

set (QNN_INCLUDE_DIRECTORY "${QNN_INSTALL_DIR}/include")

if(${CMAKE_GENERATOR_PLATFORM} MATCHES "ARM64" OR ${CMAKE_GENERATOR_PLATFORM} MATCHES "AARCH64")
    set (QNN_LIB_DIRECTORY "${QNN_INSTALL_DIR}/lib/aarch64-windows-msvc")
    set (QNN_LIB_SKEL_DIRECTORY "${QNN_INSTALL_DIR}/lib/hexagon-v73/unsigned")
else()
    set (QNN_LIB_DIRECTORY "${QNN_INSTALL_DIR}/lib/x86_64-windows-msvc")
    set (QNN_LIB_SKEL_DIRECTORY "")
endif()

# Define source groups
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Source files" FILES ${SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Header files" FILES ${HEADERS})
source_group(TREE ${QNN_INSTALL_DIR} PREFIX "Genie Header Files" FILES "${QNN_INCLUDE_DIRECTORY}")

include_directories(
    ${QNN_INCLUDE_DIRECTORY}
    ${QNN_INCLUDE_DIRECTORY}/QNN
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../common"
)

# Create a shared library from the source files
add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS} ${GENIE_HEADERS})

add_subdirectory(../../../../../deps/JSON JSON)
add_subdirectory(../../../../../deps/JSONSchema JSONSchema)

# Add common code from CIL, used by ONNX too
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../common" common_cil)

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_COMMAND}")

add_library(qnn_genie_libs SHARED IMPORTED GLOBAL)
set_target_properties(qnn_genie_libs PROPERTIES
             IMPORTED_LOCATION "${QNN_LIB_DIRECTORY}/Genie.dll"
             IMPORTED_IMPLIB "${QNN_LIB_DIRECTORY}/Genie.lib"
             INTERFACE_INCLUDE_DIRECTORIES "${QNN_INCLUDE_DIRECTORY}")
target_include_directories(qnn_genie_libs INTERFACE ${QNN_INCLUDE_DIRECTORY})

set(QNN_GENIE_LIBS_INCLUDE_DIR "${QNN_INCLUDE_DIRECTORY}")
set(QNN_GENIE_LIBS_SOURCE_DIR "${QNN_INCLUDE_DIRECTORY}")

include_directories(
	"${CMAKE_CURRENT_SOURCE_DIR}/../../../../../deps/gen-ai/"
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator::validator
	common_cil
    qnn_genie_libs
)


list(APPEND QNN_EXTRA_DEPS "Genie")
list(APPEND QNN_EXTRA_DEPS "QnnGenAiTransformer")
list(APPEND QNN_EXTRA_DEPS "QnnGenAiTransformerCpuOpPkg")
list(APPEND QNN_EXTRA_DEPS "QnnGenAiTransformerModel")
list(APPEND QNN_EXTRA_DEPS "QnnSystem")

if(${CMAKE_GENERATOR_PLATFORM} MATCHES "ARM64" OR ${CMAKE_GENERATOR_PLATFORM} MATCHES "AARCH64")
    list(APPEND QNN_EXTRA_DEPS "QnnSystem")
    list(APPEND QNN_EXTRA_DEPS "QnnHtp")
    list(APPEND QNN_EXTRA_DEPS "QnnHtpNetRunExtensions")
    list(APPEND QNN_EXTRA_DEPS "QnnHtpV73Stub")
    list(APPEND QNN_EXTRA_SKEL_DEPS "libQnnHtpV73Skel.so")
    list(APPEND QNN_EXTRA_SKEL_DEPS "libqnnhtpv73.cat")
endif()

foreach(LIB ${QNN_EXTRA_DEPS})
  list(APPEND QNN_EXTRA_DEPS_RELEASE ${LIB}${CMAKE_SHARED_LIBRARY_SUFFIX})
endforeach()

foreach(LIB ${QNN_EXTRA_SKEL_DEPS})
  list(APPEND QNN_EXTRA_SKEL_DEPS_RELEASE "${QNN_LIB_SKEL_DIRECTORY}/${LIB}")
endforeach()

list(JOIN QNN_EXTRA_DEPS_RELEASE ";" QNN_EXTRA_DEPS_RELEASE)
list(JOIN QNN_EXTRA_SKEL_DEPS_RELEASE ";" QNN_EXTRA_SKEL_DEPS_RELEASE)

string(APPEND _cmd "${CMAKE_COMMAND};-E;copy_if_different;${QNN_EXTRA_DEPS_RELEASE};$<TARGET_FILE_DIR:${PROJECT_NAME}>")

if(${CMAKE_GENERATOR_PLATFORM} MATCHES "ARM64" OR ${CMAKE_GENERATOR_PLATFORM} MATCHES "AARCH64")
    string(APPEND _cmd_htp "${CMAKE_COMMAND};-E;copy_if_different;${QNN_EXTRA_SKEL_DEPS_RELEASE};$<TARGET_FILE_DIR:${PROJECT_NAME}>")
else()
    string(APPEND _cmd_htp "${CMAKE_COMMAND};-E;echo;Not copying NPU libs on ARM64")
endif()

# Copy dependent libraries
add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    # Copy the QNN dependent libraries
    COMMAND "${_cmd}"
    COMMAND "${_cmd_htp}"
    COMMENT "Copying QNN DLLs for IHV"
	WORKING_DIRECTORY ${QNN_LIB_DIRECTORY}
	COMMAND_EXPAND_LISTS
)