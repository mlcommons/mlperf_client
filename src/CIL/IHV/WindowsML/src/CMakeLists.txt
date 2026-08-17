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

# MIGraphX EP (AMD GPU rail 2.x EP) is currently non-functional in the
# Microsoft Store-distributed package but is still surfaced by the WinML
# catalog on AMD systems. Enumerating/registering it triggers spurious
# initialization that the benchmark cannot use. Leave this OFF until the EP
# becomes functional in the certified WinML rail; then flip ON.
option(MLPERF_WINDOWSML_ENABLE_MIGRAPHX
       "Enable enumeration/registration of the MIGraphX WinML EP" OFF)
if(MLPERF_WINDOWSML_ENABLE_MIGRAPHX)
  target_compile_definitions(${PROJECT_NAME} PRIVATE
      MLPERF_WINDOWSML_ENABLE_MIGRAPHX=1)
  message(STATUS "WindowsML: MIGraphX EP enumeration ENABLED")
else()
  target_compile_definitions(${PROJECT_NAME} PRIVATE
      MLPERF_WINDOWSML_ENABLE_MIGRAPHX=0)
  message(STATUS "WindowsML: MIGraphX EP enumeration DISABLED (default)")
endif()

add_subdirectory(../../../../../deps/JSON JSON)
add_subdirectory(../../../../../deps/JSONSchema JSONSchema)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../common" common_cil)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../common/directml" common_directml_cil)

# Centralized version pins. Bump here, then nuke build/IHV/WindowsML to force re-install.
set(MML_OGA_VERSION    "0.14.1")
set(MML_WINML_VERSION  "2.4.66-preview")
set(MML_OGA_URL   "https://www.nuget.org/api/v2/package/Microsoft.ML.OnnxRuntimeGenAI.WinML/${MML_OGA_VERSION}")

# Ensure ML package is available during configure
set(WAML_PACKAGE_ROOT   "${CMAKE_CURRENT_BINARY_DIR}/.nuget/packages/Microsoft.Windows.AI.MachineLearning")
set(WAML_VERSION_STAMP  "${WAML_PACKAGE_ROOT}/.installed-version")

# Invalidate cached install if pinned version changed since last configure
if(EXISTS "${WAML_VERSION_STAMP}")
  file(READ "${WAML_VERSION_STAMP}" _installed_version)
  string(STRIP "${_installed_version}" _installed_version)
  if(NOT "${_installed_version}" STREQUAL "${MML_WINML_VERSION}")
    message(STATUS "Microsoft.Windows.AI.MachineLearning version changed (${_installed_version} -> ${MML_WINML_VERSION}); refreshing cache...")
    file(REMOVE_RECURSE "${WAML_PACKAGE_ROOT}")
  endif()
endif()

if(NOT EXISTS "${WAML_PACKAGE_ROOT}")
  message(STATUS "Microsoft.Windows.AI.MachineLearning not found at ${WAML_PACKAGE_ROOT}; installing ${MML_WINML_VERSION} into build tree...")
  execute_process(
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/install-winml.ps1" -Version ${MML_WINML_VERSION} -OutputDir "${CMAKE_CURRENT_BINARY_DIR}/.nuget/packages"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    RESULT_VARIABLE WINML_INSTALL_RES
    OUTPUT_VARIABLE WINML_INSTALL_OUT
    ERROR_VARIABLE WINML_INSTALL_ERR
  )
  if(NOT WINML_INSTALL_RES EQUAL 0)
    message(FATAL_ERROR "Failed to install Microsoft.Windows.AI.MachineLearning (code ${WINML_INSTALL_RES}).\n${WINML_INSTALL_ERR}")
  endif()
  file(WRITE "${WAML_VERSION_STAMP}" "${MML_WINML_VERSION}\n")
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

set(WAML_METADATA_DIR "${WAML_PACKAGE_ROOT}/metadata")

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
      -in "${WAML_METADATA_DIR}"
      ${CPPWINRT_REF_ARGS}
      -out "${WAML_METADATA_DIR}"
    DEPENDS "${WAML_METADATA_DIR}/Microsoft.Windows.AI.MachineLearning.winmd"
    COMMENT "Generating C++/WinRT projection headers for Microsoft.Windows.AI.MachineLearning"
)
add_dependencies(${PROJECT_NAME} generate_winml_projection)

if(NOT (CMAKE_SYSTEM_PROCESSOR MATCHES "arm" OR CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64" OR CMAKE_GENERATOR_PLATFORM MATCHES "ARM64"))
  set(OGA_LIB_DIR "${OGA_ROOT}/runtimes/win-x64/native")
  # 2.0.300 layout: .lib files live in lib/native/<arch>, .dll files in runtimes/win-<arch>/native
  set(WAML_LIB_DIR "${WAML_PACKAGE_ROOT}/lib/native/x64")
  set(WAML_DLL_DIR "${WAML_PACKAGE_ROOT}/runtimes/win-x64/native")
else()
  set(OGA_LIB_DIR "${OGA_ROOT}/runtimes/win-arm64/native")
  set(WAML_LIB_DIR "${WAML_PACKAGE_ROOT}/lib/native/arm64")
  set(WAML_DLL_DIR "${WAML_PACKAGE_ROOT}/runtimes/win-arm64/native")
endif()

set(OGA_INCLUDE_DIR "${OGA_ROOT}/build/native/include")
set(WAML_INCLUDE_DIR "${WAML_PACKAGE_ROOT}/include")

file(GLOB OGA_LIBS "${OGA_LIB_DIR}/*.lib")
file(GLOB OGA_DLLS "${OGA_LIB_DIR}/*.dll")
file(GLOB WAML_LIBS "${WAML_LIB_DIR}/*.lib")
file(GLOB WAML_DLLS "${WAML_DLL_DIR}/*.dll")

target_include_directories(${PROJECT_NAME}
    PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../common"
    "${OGA_INCLUDE_DIR}"
    "${WAML_INCLUDE_DIR}"
    "${WAML_METADATA_DIR}"
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

# `onnxruntime-genai-cuda.dll` goes into a `tensorrtrtx/` subfolder so the
# loader can't find it unless the IHV explicitly adds that subfolder for
# NvTensorRtRtx scenarios. See base_inference.cpp for the rationale.
foreach(_oga_dll IN LISTS OGA_DLLS)
  get_filename_component(_oga_name "${_oga_dll}" NAME)
  if(_oga_name STREQUAL "onnxruntime-genai-cuda.dll")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/tensorrtrtx"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_oga_dll}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/tensorrtrtx/"
    )
  else()
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_oga_dll}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    )
  endif()
endforeach()

if(WAML_DLLS)
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${WAML_DLLS}
        $<TARGET_FILE_DIR:${PROJECT_NAME}>
  )
endif()


# OGA 0.14.x ships onnxruntime-genai-cuda.dll built against CUDA 12 (it imports
# cudart64_12.dll). OGA probes the CUDA backend during OgaCreateModel even when
# the model uses NvTensorRtRtx, so cudart64_12.dll must be loadable next to the
# IHV.
#
# We deliberately do NOT use the system CUDA toolkit here: CI/CD machines build
# with CUDA 13 (required for llama-cpp), which ships only `cudart64_13.dll` —
# ABI-incompatible with OGA's CUDA 12 imports. Forcing CI to install a second
# CUDA 12 toolkit would be fragile and brittle.
#
# Instead, fetch the redistributable archive directly from NVIDIA's official
# CUDA redist server (https://developer.download.nvidia.com/compute/cuda/redist/)
# at configure time. This is the path NVIDIA documents for static linking /
# redistribution; the archives are versioned, EULA-compliant for redistribution
# (see https://docs.nvidia.com/cuda/eula/index.html), and bandwidth is cheap
# (~600 KB).
#
# To bump: visit the URL above, look under cuda_cudart/windows-x86_64/, copy
# the latest 12.x archive name, and update MLPERF_WINDOWSML_CUDART_VERSION
# below. Users can override with -DMLPERF_WINDOWSML_CUDART_DLL_PATH=<absolute
# path> to skip the download and use a local copy.
if(NOT (CMAKE_SYSTEM_PROCESSOR MATCHES "arm" OR CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64" OR CMAKE_GENERATOR_PLATFORM MATCHES "ARM64"))
  if(NOT MLPERF_WINDOWSML_CUDART_DLL_PATH)
    set(MLPERF_WINDOWSML_CUDART_VERSION "12.6.77")
    set(_cudart_archive_name
        "cuda_cudart-windows-x86_64-${MLPERF_WINDOWSML_CUDART_VERSION}-archive")
    set(_cudart_url
        "https://developer.download.nvidia.com/compute/cuda/redist/cuda_cudart/windows-x86_64/${_cudart_archive_name}.zip")
    set(_cudart_cache_zip
        "${CMAKE_CURRENT_BINARY_DIR}/.nuget/${_cudart_archive_name}.zip")
    set(_cudart_extract_dir
        "${CMAKE_CURRENT_BINARY_DIR}/.nuget/cudart-${MLPERF_WINDOWSML_CUDART_VERSION}")
    set(_cudart_extracted_dll
        "${_cudart_extract_dir}/${_cudart_archive_name}/bin/cudart64_12.dll")

    if(NOT EXISTS "${_cudart_extracted_dll}")
      if(NOT EXISTS "${_cudart_cache_zip}")
        message(STATUS "Downloading CUDA 12 redistributable: ${_cudart_url}")
        file(DOWNLOAD "${_cudart_url}" "${_cudart_cache_zip}"
             STATUS _cudart_dl_status
             TLS_VERIFY ON
             SHOW_PROGRESS)
        list(GET _cudart_dl_status 0 _cudart_dl_rc)
        list(GET _cudart_dl_status 1 _cudart_dl_msg)
        if(NOT _cudart_dl_rc EQUAL 0)
          file(REMOVE "${_cudart_cache_zip}")
          message(WARNING
              "Failed to download cuda_cudart from ${_cudart_url} "
              "(rc=${_cudart_dl_rc}: ${_cudart_dl_msg}). "
              "onnxruntime-genai-cuda.dll will fail to load. "
              "Pass -DMLPERF_WINDOWSML_CUDART_DLL_PATH=<absolute path to "
              "cudart64_12.dll> to use a local copy.")
        endif()
      endif()
      if(EXISTS "${_cudart_cache_zip}")
        file(ARCHIVE_EXTRACT INPUT "${_cudart_cache_zip}"
             DESTINATION "${_cudart_extract_dir}")
      endif()
    endif()

    if(EXISTS "${_cudart_extracted_dll}")
      set(MLPERF_WINDOWSML_CUDART_DLL_PATH "${_cudart_extracted_dll}")
    endif()
  endif()

  if(MLPERF_WINDOWSML_CUDART_DLL_PATH AND EXISTS "${MLPERF_WINDOWSML_CUDART_DLL_PATH}")
    message(STATUS "Staging CUDA 12 runtime DLL into tensorrtrtx/: ${MLPERF_WINDOWSML_CUDART_DLL_PATH}")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/tensorrtrtx"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MLPERF_WINDOWSML_CUDART_DLL_PATH}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/tensorrtrtx/"
    )
  else()
    message(WARNING
        "cudart64_12.dll not available. NV-flavored WindowsML scenarios will "
        "fail at OGA model load. Either let CMake download it from NVIDIA's "
        "redist server (requires internet at configure time) or pass "
        "-DMLPERF_WINDOWSML_CUDART_DLL_PATH=<absolute path>.")
  endif()
endif()

# Link necessary WinRT libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    windowsapp
    runtimeobject
)
