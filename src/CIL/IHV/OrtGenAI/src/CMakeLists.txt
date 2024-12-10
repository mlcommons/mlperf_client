cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0135 NEW)

project(IHV_OrtGenAI VERSION 1.0)

if(APPLE)
  message(FATAL_ERROR "Unsupported platform.")
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

message(STATUS "Platform: ${PLATFORM}")
message(STATUS "Architecture: ${CMAKE_SYSTEM_PROCESSOR}")

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

set(ORT_GENAI_INSTALL_DIR "" CACHE PATH "Path to the ONNXRuntime GenAI installation directory.")
option(ORT_GENAI_USE_NVTX "Enable NVTX ranges for profiling." false)


if(NOT ORT_GENAI_INSTALL_DIR)
    message(FATAL_ERROR "ORT_GENAI_INSTALL_DIR is not set. Please set ORT_GENAI_INSTALL_DIR to the ORT GenAI installation directory.")
endif()

if(ORT_GENAI_USE_NVTX)
  add_compile_definitions(USE_NVTX_RANGES)
endif()

# Force use of the release runtime library in all configurations
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:>")
endif()

# Define the source files for the library
file(GLOB_RECURSE SOURCES "*.cpp")

# Define the header files for the library
file(GLOB_RECURSE HEADERS "*.h")

# Define source groups
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Source files" FILES ${SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Header files" FILES ${HEADERS})

# Create a static library from the source files
add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS})

# TODO: FIND ORT GENAI HERE? 
set(ORT_GENAI_LIB_DIR ${ORT_GENAI_INSTALL_DIR}/lib)
set(ORT_GENAI_LIB "onnxruntime-genai.dll")
set(ORT_GENAI_DEPENDENCY "*.dll")

# TODO: Required ?
add_subdirectory(../../../../../deps/JSON JSON)
add_subdirectory(../../../../../deps/JSONSchema JSONSchema)

# Add common code from CIL, used by ONNX too
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../common" common_cil)

target_include_directories(${PROJECT_NAME}
    PUBLIC
    "${ORT_GENAI_INSTALL_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../common"
)

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_COMMAND}")
target_link_directories(${PROJECT_NAME} PRIVATE ${ORT_GENAI_LIB_DIR})
target_link_libraries(${PROJECT_NAME}
    PRIVATE
    ${ORT_GENAI_LIB}
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator::validator
	  common_cil
)

target_link_libraries(${PROJECT_NAME} PUBLIC onnxruntime-genai)
#  Copy dependent libraries
file(GLOB DLL_FILES "${ORT_GENAI_LIB_DIR}/*.dll")
foreach(DLL ${DLL_FILES})
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        "${DLL}"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    )
endforeach()