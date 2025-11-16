cmake_minimum_required(VERSION 3.23)
cmake_policy(SET CMP0135 NEW)

project(IHV_WindowsML VERSION 1.0)

if(NOT WIN32)
  message(FATAL_ERROR "Unsupported platform. This project requires Windows.")
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

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "*.h")
# Exclude NuGet package sources/headers from being compiled
list(FILTER SOURCES EXCLUDE REGEX ".*/\\.nuget/.*")
list(FILTER HEADERS EXCLUDE REGEX ".*/\\.nuget/.*")

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Source files" FILES ${SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Header files" FILES ${HEADERS})

add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS})

add_subdirectory(../../../../../deps/JSON JSON)
add_subdirectory(../../../../../deps/JSONSchema JSONSchema)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../common" common_cil)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../common/directml" common_directml_cil)

set(MML_OGA_URL   "https://www.nuget.org/api/v2/package/Microsoft.ML.OnnxRuntimeGenAI.WinML/0.10.0")
# set(MML_WAML_URL  "https://www.nuget.org/api/v2/package/Microsoft.Windows.AI.MachineLearning/0.3.131-beta")

# Ensure ML package is available during configure
set(WASDKML_PACKAGE_ROOT   "${CMAKE_CURRENT_BINARY_DIR}/.nuget/packages/Microsoft.WindowsAppSDK.ML")
if(NOT EXISTS "${WASDKML_PACKAGE_ROOT}")
  message(STATUS "Microsoft.WindowsAppSDK.ML not found at ${WASDKML_PACKAGE_ROOT}; installing into build tree...")
  execute_process(
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/install-winappsdk-ml.ps1" -Version 1.8.2095 -OutputDir "${CMAKE_CURRENT_BINARY_DIR}/.nuget/packages"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    RESULT_VARIABLE WINAPPSDK_ML_INSTALL_RES
    OUTPUT_VARIABLE WINAPPSDK_ML_INSTALL_OUT
    ERROR_VARIABLE WINAPPSDK_ML_INSTALL_ERR
  )
  if(NOT WINAPPSDK_ML_INSTALL_RES EQUAL 0)
    message(FATAL_ERROR "Failed to install Microsoft.WindowsAppSDK.ML (code ${WINAPPSDK_ML_INSTALL_RES}).\n${WINAPPSDK_ML_INSTALL_ERR}")
  endif()
endif()

# Resolve Windows SDK Union Metadata (Windows.winmd) from installed Windows SDK
set(WINSDK_DIR "$ENV{WindowsSdkDir}")
set(WINSDK_VER "$ENV{WindowsSDKVersion}")
if(WINSDK_DIR STREQUAL "")
  set(WINSDK_DIR "C:/Program Files (x86)/Windows Kits/10")
endif()
if(NOT WINSDK_VER STREQUAL "")
  string(REGEX REPLACE "\\\\$" "" WINSDK_VER "${WINSDK_VER}")
  set(WSDK_UNION_METADATA_DIR "${WINSDK_DIR}/UnionMetadata/${WINSDK_VER}")
else()
  # Only consider versioned folders (exclude 'Facade')
  file(GLOB WINSDK_UM_DIRS "${WINSDK_DIR}/UnionMetadata/[0-9]*")
  if(NOT WINSDK_UM_DIRS)
    message(FATAL_ERROR "Windows SDK UnionMetadata not found under ${WINSDK_DIR}. Install Windows 10/11 SDK.")
  endif()
  list(SORT WINSDK_UM_DIRS)
  list(REVERSE WINSDK_UM_DIRS)
  list(GET WINSDK_UM_DIRS 0 WSDK_UNION_METADATA_DIR)
endif()
set(WINDOWS_WINMD "${WSDK_UNION_METADATA_DIR}/Windows.winmd")
if(NOT EXISTS "${WINDOWS_WINMD}")
  message(FATAL_ERROR "Windows.winmd not found in ${WSDK_UNION_METADATA_DIR}. Ensure Windows SDK is installed.")
endif()
set(WINDOWS_FACADE_WINMD "${WSDK_UNION_METADATA_DIR}/Facade/windows.winmd")
set(CPPWINRT_REF_ARGS -reference "${WINDOWS_WINMD}")
if(EXISTS "${WINDOWS_FACADE_WINMD}")
  list(APPEND CPPWINRT_REF_ARGS -reference "${WINDOWS_FACADE_WINMD}")
endif()

set(WASDKML_METADATA_DIR "${WASDKML_PACKAGE_ROOT}/metadata")

include(FetchContent)

FetchContent_Declare(
  OGA
  URL ${MML_OGA_URL}
)

FetchContent_MakeAvailable(
  OGA
)

set(OGA_ROOT "${oga_SOURCE_DIR}")

# Generate C++/WinRT projection headers before build
find_program(CPPWINRT_EXECUTABLE NAMES cppwinrt)
if(NOT CPPWINRT_EXECUTABLE)
  message(FATAL_ERROR "cppwinrt tool not found in PATH. Install C++/WinRT tooling (Windows SDK/VS) so 'cppwinrt' is available.")
endif()

add_custom_target(generate_winml_projection
    COMMAND ${CPPWINRT_EXECUTABLE}
      -verbose
      -in "${WSDK_UNION_METADATA_DIR}"
      -in "${WASDKML_METADATA_DIR}"
      ${CPPWINRT_REF_ARGS}
      -out "${WASDKML_METADATA_DIR}"
    DEPENDS "${WASDKML_METADATA_DIR}/Microsoft.Windows.AI.MachineLearning.winmd"
    COMMENT "Generating C++/WinRT projection headers for Microsoft.Windows.AI.MachineLearning"
)
add_dependencies(${PROJECT_NAME} generate_winml_projection)

get_filename_component(NUGET_PACKAGES_ROOT "${WASDKML_PACKAGE_ROOT}" DIRECTORY)
set(WASDK_FOUNDATION_INCLUDE_DIR "${NUGET_PACKAGES_ROOT}/Microsoft.WindowsAppSDK.Foundation/include")

if(NOT (CMAKE_SYSTEM_PROCESSOR MATCHES "arm" OR CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64" OR CMAKE_GENERATOR_PLATFORM MATCHES "ARM64"))
  set(OGA_LIB_DIR "${OGA_ROOT}/runtimes/win-x64/native")
  set(WASDKML_LIB_DIR "${WASDKML_PACKAGE_ROOT}/runtimes/win-x64/native")
  set(WASDKML_DLL_DIR "${WASDKML_PACKAGE_ROOT}/runtimes-framework/win-x64/native")
  set(WASDK_FOUNDATION_LIB_DIR "${NUGET_PACKAGES_ROOT}/Microsoft.WindowsAppSDK.Foundation/lib/native/x64")
else()
  set(OGA_LIB_DIR "${OGA_ROOT}/runtimes/win-arm64/native")
  set(WASDKML_LIB_DIR "${WASDKML_PACKAGE_ROOT}/runtimes/win-arm64/native")
  set(WASDKML_DLL_DIR "${WASDKML_PACKAGE_ROOT}/runtimes-framework/win-arm64/native")
  set(WASDK_FOUNDATION_LIB_DIR "${NUGET_PACKAGES_ROOT}/Microsoft.WindowsAppSDK.Foundation/lib/native/arm64")
  
endif()

set(OGA_INCLUDE_DIR "${OGA_ROOT}/build/native/include")
set(WASDKML_INCLUDE_DIR "${WASDKML_PACKAGE_ROOT}/include")

file(GLOB OGA_LIBS "${OGA_LIB_DIR}/*.lib")
file(GLOB OGA_DLLS "${OGA_LIB_DIR}/*.dll")
file(GLOB WASDKML_LIBS "${WASDKML_LIB_DIR}/*.lib")
file(GLOB WASDKML_DLLS "${WASDKML_DLL_DIR}/*.dll")
file(GLOB WASDKML_NATIVE_DLLS "${WASDKML_LIB_DIR}/*.dll")

target_include_directories(${PROJECT_NAME}
    PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../common"
    "${OGA_INCLUDE_DIR}"
    "${WASDKML_INCLUDE_DIR}"
    "${WASDK_FOUNDATION_INCLUDE_DIR}"
    "${WASDKML_METADATA_DIR}"
)

# Avoid WindowsAppSDK ML auto-initializer (MSBuild-only header dependencies)
target_compile_definitions(${PROJECT_NAME} PRIVATE MICROSOFT_WINDOWSAPPSDK_ML_DISABLE_AUTOINITIALIZE)

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_COMMAND}")

# Configure Windows App SDK for unpackaged apps and self-contained deployment
# These MSBuild properties are picked up by the Visual Studio generator
set_target_properties(${PROJECT_NAME} PROPERTIES
    VS_GLOBAL_WindowsPackageType "None"                     # Unpackaged app: enable bootstrapper path
    VS_GLOBAL_WindowsAppSDKSelfContained "true"            # Deploy ORT/DirectML/WinML binaries alongside output
    VS_GLOBAL_WindowsAppSdkBootstrapInitialize "true"      # Allow bootstrapping from this DLL when host doesn't
)


target_link_libraries(${PROJECT_NAME}
    PRIVATE
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator::validator
    common_cil
    common_directml_cil
    ${OGA_LIBS}
    ${WASDKML_LIBS}
    "${WASDK_FOUNDATION_LIB_DIR}/Microsoft.WindowsAppRuntime.Bootstrap.lib"
)

if(OGA_DLLS)
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${OGA_DLLS}
        $<TARGET_FILE_DIR:${PROJECT_NAME}>
  )
endif()

if(WASDKML_DLLS)
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${WASDKML_DLLS}
        $<TARGET_FILE_DIR:${PROJECT_NAME}>
  )
endif()

if(WASDKML_NATIVE_DLLS)
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${WASDKML_NATIVE_DLLS}
        $<TARGET_FILE_DIR:${PROJECT_NAME}>
  )
endif()

# Link necessary WinRT libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    windowsapp
    runtimeobject 
)
