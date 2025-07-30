cmake_minimum_required(VERSION 3.23)
cmake_policy(SET CMP0135 NEW)

project(IHV_WindowsML VERSION 1.0)

if(APPLE)
  message(FATAL_ERROR "Unsupported platform.")
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

add_compile_options(/await)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

message(STATUS "Platform: ${PLATFORM}")
message(STATUS "Architecture: ${CMAKE_SYSTEM_PROCESSOR}")

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

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:>DLL")
endif()

file(GLOB_RECURSE SOURCES "*.cpp")
file(GLOB_RECURSE HEADERS "*.h")

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Source files" FILES ${SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Header files" FILES ${HEADERS})

add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS})

add_subdirectory(../../../../../deps/JSON JSON)
add_subdirectory(../../../../../deps/JSONSchema JSONSchema)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../common" common_cil)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../common/directml" common_directml_cil)

set(MML_OGA_URL   "https://www.nuget.org/api/v2/package/Microsoft.ML.OnnxRuntimeGenAI.WinML/0.8.3")
set(MML_WAML_URL  "https://www.nuget.org/api/v2/package/Microsoft.Windows.AI.MachineLearning/0.3.131-beta")

include(FetchContent)

FetchContent_Declare(
  OGA
  URL ${MML_OGA_URL}
)
FetchContent_Declare(
  WAML
  URL ${MML_WAML_URL}
)

FetchContent_MakeAvailable(OGA WAML)

set(OGA_ROOT "${oga_SOURCE_DIR}")
set(WAML_ROOT "${waml_SOURCE_DIR}")

if(NOT (CMAKE_SYSTEM_PROCESSOR MATCHES "arm" OR CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64" OR CMAKE_GENERATOR_PLATFORM MATCHES "ARM64"))
  set(OGA_LIB_DIR "${OGA_ROOT}/runtimes/win-x64/native")
  set(WAML_LIB_DIR "${WAML_ROOT}/lib/native/x64")
  set(WAML_DLL_DIR "${WAML_ROOT}/runtimes/win-x64/native")
  set(WAML_MSIX_PATH "${WAML_ROOT}/tools/MSIX/win-x64/Microsoft.Windows.AI.MachineLearning.msix")
else()
  set(OGA_LIB_DIR "${OGA_ROOT}/runtimes/win-arm64/native")
  set(WAML_LIB_DIR "${WAML_ROOT}/lib/native/arm64")
  set(WAML_DLL_DIR "${WAML_ROOT}/runtimes/win-arm64/native")
  set(WAML_MSIX_PATH "${WAML_ROOT}/tools/MSIX/win-arm64/Microsoft.Windows.AI.MachineLearning.msix")
  
endif()

set(OGA_INCLUDE_DIR "${OGA_ROOT}/build/native/include")
set(WAML_INCLUDE_DIR "${WAML_ROOT}/include/onnxruntime/")

file(GLOB OGA_LIBS "${OGA_LIB_DIR}/*.lib")
file(GLOB OGA_DLLS "${OGA_LIB_DIR}/*.dll")
file(GLOB WAML_LIBS "${WAML_LIB_DIR}/*.lib")
file(GLOB WAML_DLLS "${WAML_DLL_DIR}/*.dll")
file(GLOB WAML_MSIX "${WAML_MSIX_PATH}")

message (STATUS "MSIX package: ${WAML_MSIX}")

target_include_directories(${PROJECT_NAME}
    PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../common"
    "${OGA_INCLUDE_DIR}"
    "${WAML_INCLUDE_DIR}"
)

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_COMMAND}")

target_link_libraries(${PROJECT_NAME}
    PRIVATE
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator::validator
    common_cil
    common_directml_cil
    ${OGA_LIBS}
    ${WAML_LIBS}
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      ${OGA_DLLS}
      $<TARGET_FILE_DIR:${PROJECT_NAME}>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      ${WAML_DLLS}
      $<TARGET_FILE_DIR:${PROJECT_NAME}>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${WAML_MSIX}
      $<TARGET_FILE_DIR:${PROJECT_NAME}>
)

# Link necessary WinRT libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    windowsapp
    runtimeobject 
)
