cmake_minimum_required(VERSION 3.15)
project(MLPerf)

if(APPLE)
set(CMAKE_OSX_DEPLOYMENT_TARGET 12)
endif()

include(FetchContent)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(WIN32)
    set(CMAKE_GENERATOR "Visual Studio 16 2019" CACHE STRING "CMake Generator")
elseif(APPLE)
    set(CMAKE_GENERATOR "XCode" CACHE STRING "CMake Generator")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_program(CLANG_TIDY_EXE NAMES "clang-tidy" REQUIRED)

set(CLANG_TIDY_COMMAND "${CLANG_TIDY_EXE}" "-checks=-*,modernize-*")

# Add extra arguments for exception handling based on compiler
if (MSVC)
    list(APPEND CLANG_TIDY_COMMAND "--extra-arg=/EHsc")
else()
    list(APPEND CLANG_TIDY_COMMAND "--extra-arg=-fexceptions")
endif()

if(APPLE)
	set(PLATFORM "MacOS")
elseif(WIN32)
	if("${CMAKE_SIZEOF_VOID_P}" STREQUAL "8")
		set(PLATFORM "Windows")
	else()
		message(FATAL_ERROR "This project requires Windows x64.")
	endif()
else()
		message(FATAL_ERROR "Unsupported platform.")
endif()

message(STATUS "PLATFORM: ${PLATFORM}")
message(STATUS "CMAKE_SYSTEM_PROCESSOR: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "CMAKE_GENERATOR_PLATFORM: ${CMAKE_GENERATOR_PLATFORM}")

if(WIN32 AND NOT CMAKE_GENERATOR_PLATFORM)
    message(STATUS "CMAKE_VS_PLATFORM_NAME_DEFAULT: ${CMAKE_VS_PLATFORM_NAME_DEFAULT}")
    set(CMAKE_GENERATOR_PLATFORM ${CMAKE_VS_PLATFORM_NAME_DEFAULT})
endif()

# Set a separate directory for intermediate build files
set(CMAKE_INTERMEDIATE_DIR "${CMAKE_BINARY_DIR}/${PLATFORM}/$<CONFIG>")

if("${CMAKE_RUNTIME_OUTPUT_DIRECTORY}" STREQUAL "")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/Bin/${PLATFORM}/${CMAKE_GENERATOR_PLATFORM}/$<CONFIG>")
    
    if(IOS)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
    endif()
endif()
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_INTERMEDIATE_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_INTERMEDIATE_DIR}")

message(STATUS "Output directory: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

# Set the build configurations
set(CMAKE_CONFIGURATION_TYPES "Release;Debug;RelWithDebInfo" CACHE STRING "List of build configurations" FORCE)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build, options are: Debug Release RelWithDebInfo." FORCE)
endif()

add_compile_definitions(
	$<$<CONFIG:Debug>:DEBUG_BUILD>
	$<$<CONFIG:Release>:RELEASE_BUILD>
    $<$<CONFIG:RelWithDebInfo>:RELWITHDEBINFO_BUILD>
)

add_subdirectory(deps/JSON)
set(BUILD_SHARED_LIBS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
add_subdirectory(deps/JSONSchema)

set(HTTPLIB_COMPILE ON CACHE BOOL "Lets compile the httplib as a regular library instead of header-only" FORCE)
set(HTTPLIB_REQUIRE_OPENSSL ON CACHE BOOL "Use static OpenSSL library" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "Disable ZLIB support" FORCE)
set(HTTPLIB_REQUIRE_BROTLI OFF CACHE BOOL "Disable Brotli usage" FORCE)
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "Disable Brotli usage" FORCE)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/deps/OpenSSL")

add_subdirectory(deps/cpp-httplib)
set(HTTPLIB_LIBRARY "httplib")
set(HTTPLIB_INCLUDE_DIR ${httplib_SOURCE_DIR})

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/deps/Log4cxx")

if(APPLE AND NOT IOS)
set(INFOWARE_USE_OPENCL OFF CACHE BOOL "Use OpenCL in the infoware" FORCE)
add_subdirectory(deps/infoware)
endif()

if(WIN32)
# Options to enable/disable IHV for Windows

	list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/deps/Qnn")

    # Check if we are not on ARM
    if(NOT (CMAKE_SYSTEM_PROCESSOR MATCHES "arm" OR CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64" OR CMAKE_GENERATOR_PLATFORM MATCHES "ARM64"))
        # Native OpenVINO only supported on x64 build
        option(MLPERF_IHV_NATIVE_OPENVINO "Enable IHV Native OpenVINO EP" ON)
		option(MLPERF_IHV_NATIVE_QNN "Enable IHV Native QNN EP" OFF)
        option(MLPERF_IHV_ORT_GENAI "Enable Onnx Runtime GenAI path using DML." ON)
        option(MLPERF_IHV_ORT_GENAI_RYZENAI "Enable ONNX Runtime GenAI path using RyzenAI." ON)
        set(MLPERF_IHV_GGML_VULKAN OFF)
        option(MLPERF_IHV_GGML_CUDA "Enable IHV GGML CUDA EP" ON)
        option(MLPERF_IHV_WINDOWSML "Enable IHV Windows ML" ON)
    else()
        set(MLPERF_IHV_NATIVE_OPENVINO OFF)
		option(MLPERF_IHV_NATIVE_QNN "Enable IHV Native QNN EP" ON)
        option(MLPERF_IHV_ORT_GENAI "Enable Onnx Runtime GenAI path using DML." OFF)
        set(MLPERF_IHV_ORT_GENAI_RYZENAI OFF)
        set(MLPERF_IHV_GGML_VULKAN OFF)
        set(MLPERF_IHV_GGML_CUDA OFF)
        option(MLPERF_IHV_WINDOWSML "Enable IHV Windows ML" OFF)
	endif()

elseif(APPLE)
# Options to enable/disable IHV for MacOS

	set(MLPERF_IHV_NATIVE_OPENVINO OFF)
	set(MLPERF_IHV_ORT_GENAI OFF)
    set(MLPERF_IHV_ORT_GENAI_RYZENAI OFF)
    option(MLPERF_IHV_GGML_METAL "Enable IHV GGML Metal EP" ON)
    set(MLPERF_IHV_GGML_VULKAN OFF)
    set(MLPERF_IHV_GGML_CUDA OFF)
    option(MLPERF_IHV_MLX "Enable IHV MLX EP" ON)
	set(MLPERF_IHV_NATIVE_QNN OFF)
	set(MLPERF_IHV_WINDOWSML OFF)
endif()

if(MLPERF_IHV_GGML_VULKAN)
find_package(Vulkan COMPONENTS glslc)
find_program(glslc_executable NAMES glslc HINTS Vulkan::glslc)

if (NOT Vulkan_FOUND OR NOT glslc_executable)
    message(WARNING "Vulkan not found, the build will be executed without Vulkan, please install Vulkan SDK, or set the path to Vulkan SDK in the environment variable VULKAN_SDK. ")
    set(MLPERF_IHV_GGML_VULKAN OFF)
else()
    message(STATUS "Vulkan + glslc found: enabling GGML Vulkan build")
endif()

endif()

if(MLPERF_IHV_GGML_CUDA)
find_package(CUDAToolkit)

if (NOT CUDAToolkit_FOUND)
    message(WARNING "CUDA toolkit not found, the build will be executed without CUDA, please install CUDA Toolkit (12.6), or set the path to CUDA Toolkit in the environment variable. ")
    set(MLPERF_IHV_GGML_CUDA OFF)
else()
    message(STATUS "CUDA Toolkit found: enabling GGML CUDA build")
endif()

endif()


if(MLPERF_IHV_GGML_METAL OR MLPERF_IHV_GGML_VULKAN OR MLPERF_IHV_GGML_CUDA)
    set(MLPERF_IHV_GGML ON CACHE BOOL "Enable IHV GGML" FORCE)
else()
    set(MLPERF_IHV_GGML OFF CACHE BOOL "Disable IHV GGML" FORCE)
endif()

option(MLPERF_BUILD_GUI "Build the GUI target" OFF)
set(MLPERF_PACK_VENDORS_DEFAULT ${MLPERF_BUILD_GUI} CACHE BOOL "Zip and pack vendors default configurations" FORCE)

add_subdirectory(src)

# Keep dependencies in one tree, enables hiding them in the solution.
macro(get_all_targets_recursive targets dir)
    get_property(subdirectories DIRECTORY ${dir} PROPERTY SUBDIRECTORIES)
    foreach(subdir ${subdirectories})
        get_all_targets_recursive(${targets} ${subdir})
    endforeach()

    get_property(current_targets DIRECTORY ${dir} PROPERTY BUILDSYSTEM_TARGETS)
    list(APPEND ${targets} ${current_targets})
endmacro()

get_all_targets_recursive(all_targets ${CMAKE_CURRENT_SOURCE_DIR})

foreach(target ${all_targets})
    get_target_property(target_source_dir ${target} SOURCE_DIR)

     # Get current FOLDER property of target
     get_target_property(current_folder ${target} FOLDER)

    if(target_source_dir AND ("${target_source_dir}" MATCHES ".*/_deps/.*" OR "${target_source_dir}" MATCHES ".*/deps/.*"))
        if(current_folder)
            # If the target already has a FOLDER property, prepend "Dependencies/" to it
            set_target_properties(${target} PROPERTIES FOLDER "Dependencies/${current_folder}")
        else()
            set_target_properties(${target} PROPERTIES FOLDER "Dependencies")
        endif()
    elseif(current_folder MATCHES "ExternalProjectTargets")
        if(${target} MATCHES "DepsPacker")
            set_target_properties(${target} PROPERTIES FOLDER "")
        else()
            set_target_properties(${target} PROPERTIES FOLDER "Dependencies")
        endif()
    endif()
endforeach()
