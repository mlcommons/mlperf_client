cmake_minimum_required(VERSION 3.15)
project(MLPerf)

if(APPLE)
    if(IOS)
        set(CMAKE_OSX_DEPLOYMENT_TARGET 12)
    else()
        # 14.0: the Diffusers EP embeds torch >= 2.12 wheels (built minos 14.0).
        set(CMAKE_OSX_DEPLOYMENT_TARGET 14)
    endif()
endif()

include(FetchContent)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(WIN32)
    set(CMAKE_GENERATOR "Visual Studio 16 2019" CACHE STRING "CMake Generator")
elseif(APPLE)
    set(CMAKE_GENERATOR "XCode" CACHE STRING "CMake Generator")
elseif(UNIX AND NOT APPLE)  # Linux
    set(CMAKE_GENERATOR "Unix Makefiles" CACHE STRING "CMake Generator")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# clang-tidy runs on native builds. Cross toolchains don't have it
# wired up, so skip it only when truly cross-compiling (target
# processor differs from host).
if("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "${CMAKE_HOST_SYSTEM_PROCESSOR}")
    find_program(CLANG_TIDY_EXE NAMES "clang-tidy" REQUIRED)
    set(CLANG_TIDY_COMMAND "${CLANG_TIDY_EXE}" "-checks=-*,modernize-*")
    # Add extra arguments for exception handling based on compiler
    if (MSVC)
        list(APPEND CLANG_TIDY_COMMAND "--extra-arg=/EHsc")
    else()
        list(APPEND CLANG_TIDY_COMMAND "--extra-arg=-fexceptions")
    endif()
endif()

if(APPLE)
	set(PLATFORM "MacOS")
elseif(WIN32)
	if("${CMAKE_SIZEOF_VOID_P}" STREQUAL "8")
		set(PLATFORM "Windows")
	else()
		message(FATAL_ERROR "This project requires Windows x64.")
	endif()
elseif(UNIX AND NOT APPLE)  # Linux
	set(PLATFORM "Linux")
else()  
		message(FATAL_ERROR "Unsupported platform.")
endif()

message(STATUS "PLATFORM: ${PLATFORM}")
message(STATUS "CMAKE_SYSTEM_PROCESSOR: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "CMAKE_HOST_SYSTEM_PROCESSOR: ${CMAKE_HOST_SYSTEM_PROCESSOR}")
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

# Platform-specific compiler flags
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MT")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MT")
    set(CMAKE_C_FLAGS_DEBUG   "${CMAKE_C_FLAGS_DEBUG} /MT")
elseif(UNIX AND NOT APPLE)  # Linux
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
endif()

add_compile_definitions(
	$<$<CONFIG:Debug>:DEBUG_BUILD>
	$<$<CONFIG:Release>:RELEASE_BUILD>
    $<$<CONFIG:RelWithDebInfo>:RELWITHDEBINFO_BUILD>
)

add_subdirectory(deps/JSON)
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(deps/JSONSchema)

set(HTTPLIB_COMPILE ON CACHE BOOL "Lets compile the httplib as a regular library instead of header-only" FORCE)
set(HTTPLIB_REQUIRE_OPENSSL ON CACHE BOOL "Use static OpenSSL library" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "Disable ZLIB support" FORCE)
set(HTTPLIB_USE_ZSTD_IF_AVAILABLE OFF CACHE BOOL "Disable ZSTD support" FORCE)
set(HTTPLIB_REQUIRE_BROTLI OFF CACHE BOOL "Disable Brotli usage" FORCE)
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "Disable Brotli usage" FORCE)
if(IOS)
  add_compile_definitions(CPPHTTPLIB_DISABLE_MACOSX_AUTOMATIC_ROOT_CERTIFICATES)
endif()
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/deps/OpenSSL")

# Cross-compiling x64->ARM64: cpp-httplib's find_package(Python3) only runs split.py
# on the host, but would reject the x64 interpreter as wrong-arch. Pin a host one.
if(WIN32 AND CMAKE_GENERATOR_PLATFORM MATCHES "ARM64"
        AND CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64"
        AND NOT Python3_EXECUTABLE)
    find_program(Python3_EXECUTABLE NAMES python python3)
endif()

add_subdirectory(deps/cpp-httplib)
set(HTTPLIB_LIBRARY "httplib")
set(HTTPLIB_INCLUDE_DIR ${httplib_SOURCE_DIR})

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/deps/Log4cxx")

# IHV Subprocess isolation - runs IHV code in a separate process to isolate
# conflicting EP libraries. Supported on desktop platforms; iOS cannot spawn
# subprocesses, so it stays disabled there.
if(IOS)
  set(MLPERF_SUBPROCESS_IHV_ISOLATION OFF)
else()
  option(MLPERF_SUBPROCESS_IHV_ISOLATION "Enable IHV subprocess isolation" ON)
endif()

if(WIN32)
# Options to enable/disable IHV for Windows

	list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/deps/Qnn")

    # Check if we are not on ARM
    if(NOT (CMAKE_SYSTEM_PROCESSOR MATCHES "arm" OR CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64" OR CMAKE_GENERATOR_PLATFORM MATCHES "ARM64"))
        # Native OpenVINO only supported on x64 build
        option(MLPERF_IHV_NATIVE_OPENVINO "Enable IHV Native OpenVINO EP" ON)
		option(MLPERF_IHV_NATIVE_QNN "Enable IHV Native QNN EP" OFF)
        set(MLPERF_IHV_ORT_GENAI OFF)
        option(MLPERF_IHV_ORT_GENAI_RYZENAI "Enable ONNX Runtime GenAI path using RyzenAI." ON)
        option(MLPERF_IHV_GGML_CUDA "Enable IHV GGML CUDA EP" ON)
        set(MLPERF_IHV_GGML_ROCM OFF)
        option(MLPERF_IHV_DIFFUSERS_NVIDIA "Enable NVIDIA / CUDA Diffusers sub-EP" ON)
        option(MLPERF_IHV_DIFFUSERS_AMD "Enable AMD / RyzenAI Diffusers sub-EP" ON)
    else()
        set(MLPERF_IHV_NATIVE_OPENVINO OFF)
		option(MLPERF_IHV_NATIVE_QNN "Enable IHV Native QNN EP" ON)
        set(MLPERF_IHV_ORT_GENAI OFF)
        set(MLPERF_IHV_ORT_GENAI_RYZENAI OFF)
        option(MLPERF_IHV_GGML_CUDA "Enable IHV GGML CUDA EP" OFF)
        set(MLPERF_IHV_GGML_ROCM OFF)
        set(MLPERF_IHV_DIFFUSERS_NVIDIA OFF)
        set(MLPERF_IHV_DIFFUSERS_AMD OFF)
	endif()
    set(MLPERF_IHV_DIFFUSERS_APPLE OFF)
    set(MLPERF_IHV_GGML_VULKAN OFF)
    option(MLPERF_IHV_WINDOWSML "Enable IHV Windows ML" ON)
elseif(APPLE)
# Options to enable/disable IHV for MacOS
	set(MLPERF_IHV_NATIVE_OPENVINO OFF)
	set(MLPERF_IHV_ORT_GENAI OFF)
    set(MLPERF_IHV_ORT_GENAI_RYZENAI OFF)
    option(MLPERF_IHV_GGML_METAL "Enable IHV GGML Metal EP" ON)
    set(MLPERF_IHV_GGML_VULKAN OFF)
    set(MLPERF_IHV_GGML_CUDA OFF)
    set(MLPERF_IHV_GGML_ROCM OFF)
    option(MLPERF_IHV_MLX "Enable IHV MLX EP" ON)
    if(IOS)
        set(MLPERF_IHV_DIFFUSERS_APPLE OFF)
    else()
        option(MLPERF_IHV_DIFFUSERS_APPLE  "Enable Apple / MPS Diffusers sub-EP"  ON)
    endif()
    set(MLPERF_IHV_DIFFUSERS_NVIDIA OFF)
    set(MLPERF_IHV_DIFFUSERS_AMD OFF)
	set(MLPERF_IHV_NATIVE_QNN OFF)
	set(MLPERF_IHV_WINDOWSML OFF)
elseif(UNIX AND NOT APPLE)  # Linux
    option(MLPERF_IHV_NATIVE_OPENVINO "Enable IHV Native OpenVINO EP" ON)
    set(MLPERF_IHV_ORT_GENAI OFF)
    set(MLPERF_IHV_ORT_GENAI_RYZENAI OFF)
    set(MLPERF_IHV_GGML_METAL OFF)
    set(MLPERF_IHV_GGML_VULKAN OFF)
    set(MLPERF_IHV_GGML_ROCM OFF)
    set(MLPERF_IHV_NATIVE_QNN OFF)
    set(MLPERF_IHV_WINDOWSML OFF)
    set(MLPERF_IHV_DIFFUSERS_NVIDIA OFF)
    set(MLPERF_IHV_DIFFUSERS_APPLE  OFF)
    set(MLPERF_IHV_DIFFUSERS_AMD OFF)
    set(MLPERF_IHV_GGML_CUDA OFF)
endif()

if(MLPERF_IHV_DIFFUSERS_NVIDIA OR MLPERF_IHV_DIFFUSERS_APPLE OR MLPERF_IHV_DIFFUSERS_AMD)
    set(MLPERF_IHV_DIFFUSERS ON CACHE BOOL "Enable IHV Diffusers" FORCE)
else()
    set(MLPERF_IHV_DIFFUSERS OFF CACHE BOOL "Disable IHV Diffusers" FORCE)
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
    message(WARNING "CUDA toolkit not found, the build will be executed without CUDA, please install CUDA Toolkit (13.x), or set CUDAToolkit_ROOT / CUDA_PATH. ")
    set(MLPERF_IHV_GGML_CUDA OFF)
else()
    message(STATUS "CUDA Toolkit ${CUDAToolkit_VERSION} found (${CUDAToolkit_NVCC_EXECUTABLE}): enabling GGML CUDA build")
endif()

endif()

if(MLPERF_IHV_GGML_ROCM)
find_package(hip)

if(NOT hip_FOUND)
    message(WARNING "HIP SDK not found, the build will be executed without ROCM/HIP, please install HIP SDK. ")
    set(MLPERF_IHV_GGML_ROCM OFF)
else()
    message(STATUS "HIP SDK found: enabling GGML ROCM build")
    # Detect BLAS backends used by ggml-hip and pass their dirs to subprojects
    find_package(hipblas REQUIRED)
    find_package(rocblas REQUIRED)
endif()

endif()

if(MLPERF_IHV_GGML_METAL OR MLPERF_IHV_GGML_VULKAN OR MLPERF_IHV_GGML_CUDA OR MLPERF_IHV_GGML_ROCM)
    set(MLPERF_IHV_GGML ON CACHE BOOL "Enable IHV GGML" FORCE)
else()
    set(MLPERF_IHV_GGML OFF CACHE BOOL "Disable IHV GGML" FORCE)
endif()

option(MLPERF_BUILD_GUI "Build the GUI target" OFF)
option(MLPERF_IHV_DIFFUSERS_INSTALL_PYTHON "Download embedded Python and install packages for Diffusers EP (dev only)" ON)
include(CMakeDependentOption)
cmake_dependent_option(MLPERF_IHV_DIFFUSERS_BUILD_STANDALONE
    "Provide the diffusers_standalone build target (dev only, on-demand)" ON
    "MLPERF_IHV_DIFFUSERS_INSTALL_PYTHON" OFF)
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
