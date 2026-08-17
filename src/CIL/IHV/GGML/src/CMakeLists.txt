cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0135 NEW)

project(IHV_GGML_EPs VERSION 1.0)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set a separate directory for intermediate build files
set(CMAKE_INTERMEDIATE_DIR "${CMAKE_BINARY_DIR}/Build")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
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

include(ExternalProject)
set(LLAMA_CPP_SOURCE_DIR ${CMAKE_BINARY_DIR}/_deps/llama_cpp)
ExternalProject_Add(llama_cpp_download
    GIT_REPOSITORY https://github.com/ggerganov/llama.cpp.git
    GIT_TAG        b9782
    SOURCE_DIR     ${LLAMA_CPP_SOURCE_DIR}
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND ""
)

set(_GGML_WIN_ARM OFF)
if(WIN32 AND (CMAKE_GENERATOR_PLATFORM MATCHES "ARM64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|arm64|aarch64"))
    set(_GGML_WIN_ARM ON)
endif()
set(_GGML_ARM_TOOLCHAIN ${LLAMA_CPP_SOURCE_DIR}/cmake/arm64-windows-llvm.cmake)
set(_GGML_HOST_TOOLCHAIN ${LLAMA_CPP_SOURCE_DIR}/cmake/x64-windows-llvm.cmake)

macro(ggml_ensure_ninja OUT_VAR)
    find_program(_GGML_NINJA NAMES ninja ninja-build ninja.exe)
    if(NOT _GGML_NINJA)
        if(WIN32)
            set(_NINJA_BOOTSTRAP_DIR "${CMAKE_BINARY_DIR}/.ninja-bootstrap")
            file(MAKE_DIRECTORY "${_NINJA_BOOTSTRAP_DIR}")
            set(_NINJA_ZIP "${_NINJA_BOOTSTRAP_DIR}/ninja-win.zip")
            set(_NINJA_EXE "${_NINJA_BOOTSTRAP_DIR}/ninja.exe")
            if(NOT EXISTS "${_NINJA_EXE}")
                file(DOWNLOAD "https://github.com/ninja-build/ninja/releases/download/v1.11.1/ninja-win.zip" "${_NINJA_ZIP}" SHOW_PROGRESS)
                execute_process(
                    COMMAND ${CMAKE_COMMAND} -E tar xf "${_NINJA_ZIP}"
                    WORKING_DIRECTORY "${_NINJA_BOOTSTRAP_DIR}"
                )
            endif()
            if(EXISTS "${_NINJA_EXE}")
                set(_GGML_NINJA "${_NINJA_EXE}")
            else()
                message(FATAL_ERROR "Failed to bootstrap Ninja. Please install Ninja and retry.")
            endif()
        else()
            message(FATAL_ERROR "Ninja not found. Please install Ninja.")
        endif()
    endif()
    set(${OUT_VAR} "${_GGML_NINJA}")
endmacro()

set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Force use of the release runtime library in all configurations
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:>DLL")
endif()

# Define the source files for the library
file(GLOB_RECURSE SOURCES "*.cpp")

# Define the header files for the library
file(GLOB_RECURSE HEADERS "*.h")

# Show LLM sources in the IHV_GGML_EPs project but don't compile them
# (they build into the per-backend llama_cpp_<Backend> libraries).
list(FILTER SOURCES EXCLUDE REGEX "LLM")
list(FILTER HEADERS EXCLUDE REGEX "LLM")
# Exclude files in the LLM directory from being compiled
foreach(SOURCE_FILE ${SOURCES})
    if(SOURCE_FILE MATCHES "LLM")
        set_source_files_properties(${SOURCE_FILE} PROPERTIES HEADER_FILE_ONLY TRUE)
    endif()
endforeach()

# Define source groups
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Source files" FILES ${SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Header files" FILES ${HEADERS})

# Create a shared library from the source files
add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS})

if (IOS)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        FRAMEWORK TRUE
        FRAMEWORK_VERSION A
        MACOSX_FRAMEWORK_IDENTIFIER "org.mlcommons.mlperfihvGGML"
        MACOSX_FRAMEWORK_SHORT_VERSION_STRING 1.0
        MACOSX_FRAMEWORK_BUNDLE_VERSION 1.0
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Apple Development"
    )
endif()

set(BUILD_SHARED_LIBS OFF)
add_subdirectory(../../../../../deps/JSON JSON)
add_subdirectory(../../../../../deps/JSONSchema JSONSchema)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../../common/dylib.cmake)
target_link_libraries(${PROJECT_NAME} dylib nlohmann_json::nlohmann_json)

target_include_directories(${PROJECT_NAME}
    PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../common"
)

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_COMMAND}")

# RAM-bounded parallelism: an unbounded -j OOM-kills the EP build (nvcc fattn
# instances peak ~5 GiB). Budget ~6 GiB/job. Override with -DGGML_EP_BUILD_JOBS=N.
set(GGML_EP_BUILD_JOBS "" CACHE STRING "Parallel jobs for llama.cpp EP builds (default: RAM-bounded)")
if(NOT GGML_EP_BUILD_JOBS)
    cmake_host_system_information(RESULT _ggml_mem_mb QUERY TOTAL_PHYSICAL_MEMORY)
    cmake_host_system_information(RESULT _ggml_cores QUERY NUMBER_OF_LOGICAL_CORES)
    math(EXPR _ggml_jobs "${_ggml_mem_mb} / 6000")
    if(_ggml_jobs LESS 1)
        set(_ggml_jobs 1)
    endif()
    if(_ggml_cores GREATER 0 AND _ggml_cores LESS _ggml_jobs)
        set(_ggml_jobs ${_ggml_cores})
    endif()
    set(GGML_EP_BUILD_JOBS ${_ggml_jobs})
endif()
message(STATUS "GGML EP build parallelism: -j${GGML_EP_BUILD_JOBS}")

function(add_llama_cpp_ep_library library_name binary_dir install_dir)
    set(GIT_EXECUTABLE_ARG "")
    if(NOT IOS)
        set(GIT_EXECUTABLE_ARG -DGIT_EXECUTABLE=${GIT_EXECUTABLE})
    endif()

    set(IOS_ARGS "")
    if(IOS)
        list(APPEND IOS_ARGS
            -DXCODE_ATTRIBUTE_CODE_SIGN_IDENTITY=
            -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=ON
        )
    endif()

    set(_gen_args -G "${CMAKE_GENERATOR}")
    set(_cfg_args -DCMAKE_BUILD_TYPE=$<CONFIG>)
    set(_build_cfg --config $<CONFIG>)
    set(_env_prefix "")
    if(_GGML_WIN_ARM)
        ggml_ensure_ninja(_ninja_exe)
        get_filename_component(_ninja_dir "${_ninja_exe}" DIRECTORY)
        set(_gen_args -G "Ninja" -DCMAKE_MAKE_PROGRAM=${_ninja_exe})
        set(_cfg_args -DCMAKE_BUILD_TYPE=$<CONFIG> -DCMAKE_TOOLCHAIN_FILE=${_GGML_ARM_TOOLCHAIN} -DGGML_NATIVE=OFF -DGGML_OPENMP=OFF)
        set(_build_cfg "")
        set(_env_prefix ${CMAKE_COMMAND} -E env "--modify" "PATH=path_list_prepend:${_ninja_dir}")
        if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64")
            list(APPEND _env_prefix --unset=LIB --unset=INCLUDE --unset=LIBPATH)
        endif()
        find_program(_GGML_CLANGXX NAMES clang++ NO_CMAKE_FIND_ROOT_PATH)
        if(_GGML_CLANGXX)
            get_filename_component(_clang_dir "${_GGML_CLANGXX}" DIRECTORY)
            list(APPEND _env_prefix "--modify" "PATH=path_list_prepend:${_clang_dir}")
        endif()
    elseif(CMAKE_TOOLCHAIN_FILE)
        # Linux cross-compile: forward the cross toolchain to the EP sub-build
        # and disable -march=native tuning.
        list(APPEND _cfg_args -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE} -DGGML_NATIVE=OFF)
    endif()

    ExternalProject_Add(${library_name}
        DEPENDS llama_cpp_download
        SOURCE_DIR ${EP_LIBRARY_SOURCE_DIR}
        BINARY_DIR ${binary_dir}
        INSTALL_DIR ${install_dir}
        BUILD_ALWAYS ON

        # Custom configuration command
        CONFIGURE_COMMAND ${CMAKE_COMMAND} -E echo "Configuring ${library_name} for ${CMAKE_BUILD_TYPE}"
        COMMAND ${_env_prefix} ${CMAKE_COMMAND} -B ${binary_dir} -S ${EP_LIBRARY_SOURCE_DIR}
            ${_gen_args}
            -DCMAKE_INSTALL_PREFIX=${install_dir}
            -DCMAKE_CXX_STANDARD=20
            ${_cfg_args}
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DENV_PATH=${ENV_PATH}
            -DPROJECT_NAME=${library_name}
            -DIOS=${IOS}
            -DLLAMA_CPP_SOURCE_DIR=${LLAMA_CPP_SOURCE_DIR}
            ${GIT_EXECUTABLE_ARG}
            ${IOS_ARGS}
            ${ARGN}

        # Custom build command
        BUILD_COMMAND ${CMAKE_COMMAND} -E echo "Starting ${library_name} build for ${CMAKE_BUILD_TYPE}..."
        COMMAND ${_env_prefix} ${CMAKE_COMMAND} --build ${binary_dir} ${_build_cfg} --parallel ${GGML_EP_BUILD_JOBS}

        # Custom install command
        INSTALL_COMMAND ${CMAKE_COMMAND} --install ${binary_dir} ${_build_cfg} --prefix ${install_dir}/$<CONFIG>
        LOG_CONFIGURE ON
        LOG_BUILD ON
        LOG_INSTALL ON
    )

endfunction()

# Define directories for both Vulkan and Metal libraries
set(EP_LIBRARY_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/LLM)
set(EP_LIBRARY_BINARY_DIR_VULKAN ${CMAKE_BINARY_DIR}/Vulkan)
set(EP_LIBRARY_BINARY_DIR_CUDA ${CMAKE_BINARY_DIR}/CUDA)
set(EP_LIBRARY_BINARY_DIR_METAL ${CMAKE_BINARY_DIR}/Metal)
set(EP_LIBRARY_BINARY_DIR_ROCM ${CMAKE_BINARY_DIR}/ROCm)
set(EP_LIBRARY_INSTALL_DIR_VULKAN ${CMAKE_BINARY_DIR}/Vulkan/bin)
set(EP_LIBRARY_INSTALL_DIR_CUDA ${CMAKE_BINARY_DIR}/CUDA/bin)
set(EP_LIBRARY_INSTALL_DIR_METAL ${CMAKE_BINARY_DIR}/Metal/bin)
set(EP_LIBRARY_INSTALL_DIR_ROCM ${CMAKE_BINARY_DIR}/ROCm/bin)

# Add GGML_Vulkan if GGML_VULKAN is set
if(GGML_VULKAN)
    set(SPIRV_HEADERS_DIR_ARG "")
    if(SPIRV-Headers_DIR)
        set(SPIRV_HEADERS_DIR_ARG "-DSPIRV-Headers_DIR:PATH=${SPIRV-Headers_DIR}")
    endif()
    set(_vk_shaders_gen_toolchain_arg "")
    if(_GGML_WIN_ARM AND CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64")
        set(_vk_shaders_gen_toolchain_arg
            "-DGGML_VULKAN_SHADERS_GEN_TOOLCHAIN=${_GGML_HOST_TOOLCHAIN}")
    endif()
    add_llama_cpp_ep_library(llama_cpp_Vulkan ${EP_LIBRARY_BINARY_DIR_VULKAN} ${EP_LIBRARY_INSTALL_DIR_VULKAN} -DGGML_VULKAN=ON -DGGML_CUDA=OFF -DGGML_METAL=OFF -DGGML_HIP=OFF ${SPIRV_HEADERS_DIR_ARG} ${_vk_shaders_gen_toolchain_arg})
endif()

# Add GGML_CUDA if GGML_CUDA is set
if(GGML_CUDA)
    set(_cuda_host_args "")
    set(_cuda_cache_init_args "")
    if(_GGML_WIN_ARM)
        set(_cuda_host_args -DCMAKE_CUDA_HOST_COMPILER=clang++)
    endif()
    # Pin nvcc to the toolkit the top-level resolved (forwarded as CMAKE_CUDA_COMPILER).
    # Only for non-VS generators (Ninja/Make), where nvcc is found via PATH and a shadowing
    # toolkit (e.g. conda) is a risk. With the Visual Studio generator CUDA is detected via
    # the MSBuild CUDA integration; forcing CMAKE_CUDA_COMPILER there breaks the compiler-id
    # check (works on main, which omits it).
    if(CMAKE_CUDA_COMPILER AND NOT CMAKE_GENERATOR MATCHES "Visual Studio")
        list(APPEND _cuda_host_args -DCMAKE_CUDA_COMPILER=${CMAKE_CUDA_COMPILER})
    endif()
    # --threads=0 lets nvcc compile the multiple CUDA architectures below in
    # parallel (one thread per arch, capped at CPU count). Write the arch list
    # through a cache-init file so semicolons are not re-split while generating
    # nested ExternalProject configure commands. The (otherwise unused)
    # -DMLPERF_GGML_CUDA_ARCHS keeps the arch list in the configure command so
    # changing it re-triggers the sub-build configure (the -C path alone would not).
    if(DEFINED MLPERF_GGML_CUDA_ARCHS AND NOT MLPERF_GGML_CUDA_ARCHS STREQUAL "")
        set(_mlperf_cuda_archs "${MLPERF_GGML_CUDA_ARCHS}")
        string(REPLACE "|" ";" _mlperf_cuda_archs "${_mlperf_cuda_archs}")
        set(_cuda_cache_init "${CMAKE_BINARY_DIR}/llama_cpp_CUDA-cache-init.cmake")
        file(WRITE "${_cuda_cache_init}"
            "set(CMAKE_CUDA_ARCHITECTURES \"${_mlperf_cuda_archs}\" CACHE STRING \"GGML CUDA architectures\" FORCE)\n")
        list(APPEND _cuda_cache_init_args -C "${_cuda_cache_init}" "-DMLPERF_GGML_CUDA_ARCHS=${MLPERF_GGML_CUDA_ARCHS}")
        message(STATUS "MLPerf GGML CUDA architectures cache init: ${_mlperf_cuda_archs}")
    endif()
    add_llama_cpp_ep_library(llama_cpp_CUDA ${EP_LIBRARY_BINARY_DIR_CUDA} ${EP_LIBRARY_INSTALL_DIR_CUDA} -DGGML_VULKAN=OFF -DGGML_CUDA=ON -DGGML_METAL=OFF -DGGML_HIP=OFF -DGGML_NATIVE=OFF ${_cuda_cache_init_args} "-DCMAKE_CUDA_FLAGS=--threads=0" ${_cuda_host_args})
endif()

# Add GGML_Metal if GGML_METAL is set
if(GGML_METAL)
    set(GGML_METAL_ARGS -DGGML_VULKAN=OFF -DGGML_CUDA=OFF -DGGML_METAL=ON -DGGML_HIP=OFF)
    if(IOS)
        list(APPEND GGML_METAL_ARGS -DGGML_NATIVE=OFF -DGGML_CPU_ARM_ARCH=native)
    endif()
    add_llama_cpp_ep_library(llama_cpp_Metal ${EP_LIBRARY_BINARY_DIR_METAL} ${EP_LIBRARY_INSTALL_DIR_METAL} ${GGML_METAL_ARGS})
endif()

# Add GGML_ROCM if GGML_ROCM is set
if(GGML_ROCM)
    ggml_ensure_ninja(NINJA_PROGRAM)

    # Minimal: just forward hip_DIR/hipblas_DIR/rocblas_DIR if set at the top-level
    set(HIP_DIR_ARG)
    if(DEFINED hip_DIR AND hip_DIR)
        file(TO_CMAKE_PATH "${hip_DIR}" hip_DIR_CMAKE)
        set(HIP_DIR_ARG -Dhip_DIR:PATH=${hip_DIR_CMAKE})
    endif()
    set(HIPBLAS_DIR_ARG)
    if(DEFINED hipblas_DIR AND hipblas_DIR)
        file(TO_CMAKE_PATH "${hipblas_DIR}" hipblas_DIR_CMAKE)
        set(HIPBLAS_DIR_ARG -Dhipblas_DIR:PATH=${hipblas_DIR_CMAKE})
        # Derive hipblas-common config dir as sibling of hipblas
        get_filename_component(_HIPBLAS_PARENT "${hipblas_DIR_CMAKE}" DIRECTORY)
        set(HIPBLAS_COMMON_DIR_ARG -Dhipblas-common_DIR:PATH=${_HIPBLAS_PARENT}/hipblas-common)
    endif()
    set(ROCBLAS_DIR_ARG)
    if(DEFINED rocblas_DIR AND rocblas_DIR)
        file(TO_CMAKE_PATH "${rocblas_DIR}" rocblas_DIR_CMAKE)
        set(ROCBLAS_DIR_ARG -Drocblas_DIR:PATH=${rocblas_DIR_CMAKE})
    endif()

    set(_ROCM_ROOT_FROM_HIP "")
    if(DEFINED hip_DIR AND hip_DIR)
        file(TO_CMAKE_PATH "${hip_DIR}" _HIP_DIR_NORM)
        string(REGEX REPLACE "/lib/cmake/hip$" "" _ROCM_ROOT_FROM_HIP "${_HIP_DIR_NORM}")
        if(_ROCM_ROOT_FROM_HIP STREQUAL "${_HIP_DIR_NORM}")
            string(REGEX REPLACE "/hip/lib/cmake$" "" _ROCM_ROOT_FROM_HIP "${_HIP_DIR_NORM}")
        endif()
    endif()
    if(NOT _ROCM_ROOT_FROM_HIP AND DEFINED ENV{ROCM_PATH} AND NOT "$ENV{ROCM_PATH}" STREQUAL "")
        file(TO_CMAKE_PATH "$ENV{ROCM_PATH}" _ROCM_ROOT_FROM_HIP)
    endif()

    set(ROCM_ENV ${_ROCM_ROOT_FROM_HIP})
    
    set(COMPILER_ARGS)
    if(_ROCM_ROOT_FROM_HIP)
        set(_CAND_C   "${_ROCM_ROOT_FROM_HIP}/bin/clang.exe")
        set(_CAND_CXX "${_ROCM_ROOT_FROM_HIP}/bin/clang++.exe")
        if(EXISTS "${_CAND_C}")
            list(APPEND COMPILER_ARGS -DCMAKE_C_COMPILER:FILEPATH=${_CAND_C})
        endif()
        if(EXISTS "${_CAND_CXX}")
            list(APPEND COMPILER_ARGS -DCMAKE_CXX_COMPILER:FILEPATH=${_CAND_CXX})
        endif()
    endif()

    # Pass along GIT_EXECUTABLE if present so subproject can use it; otherwise omit
    set(GIT_EXECUTABLE_ARG)
    if(GIT_EXECUTABLE)
        set(GIT_EXECUTABLE_ARG -DGIT_EXECUTABLE=${GIT_EXECUTABLE})
    endif()

    ExternalProject_Add(llama_cpp_ROCm
        DEPENDS llama_cpp_download
        SOURCE_DIR ${EP_LIBRARY_SOURCE_DIR}
        BINARY_DIR ${EP_LIBRARY_BINARY_DIR_ROCM}
        INSTALL_DIR ${EP_LIBRARY_INSTALL_DIR_ROCM}
        BUILD_ALWAYS ON

        # Mixed hip/cpp builds are not supported by CMake Visual Studio generator. Use Ninja instead.
        CMAKE_GENERATOR Ninja

        # Use CMAKE_CACHE_ARGS instead of CMAKE_ARGS to preserve GPU_TARGETS format
        # This prevents ExternalProject_Add from processing it as a list and splitting it at semicolons
        CMAKE_CACHE_ARGS
            -DCMAKE_BUILD_TYPE:STRING=$<CONFIG>
            -DCMAKE_INSTALL_PREFIX:PATH=${EP_LIBRARY_INSTALL_DIR_ROCM}
            -DCMAKE_CXX_STANDARD:STRING=20
            -DCMAKE_MAKE_PROGRAM:FILEPATH=${NINJA_PROGRAM}
            ${COMPILER_ARGS}
            -DENV_PATH:STRING=${ENV_PATH}
            -DPROJECT_NAME:STRING=llama_cpp_ROCm
            -DIOS:BOOL=${IOS}
            -DGGML_OPENMP:BOOL=OFF
            -DGGML_VULKAN:BOOL=OFF
            -DGGML_CUDA:BOOL=OFF
            -DGGML_METAL:BOOL=OFF
            -DGGML_HIP:BOOL=ON
            "-DGPU_TARGETS:STRING=gfx1100\\;gfx1101\\;gfx1102\\;gfx1150\\;gfx1151\\;gfx1200\\;gfx1201"
            -DLLAMA_CPP_SOURCE_DIR:PATH=${LLAMA_CPP_SOURCE_DIR}
            ${HIP_DIR_ARG}
            ${HIPBLAS_DIR_ARG}
            ${HIPBLAS_COMMON_DIR_ARG}
            ${ROCBLAS_DIR_ARG}

        # Custom build command (export ROCM_PATH to avoid quoting issues in flags)
        BUILD_COMMAND ${CMAKE_COMMAND} -E env ROCM_PATH=${ROCM_ENV} ${CMAKE_COMMAND} --build ${EP_LIBRARY_BINARY_DIR_ROCM}

        # Custom install command
        INSTALL_COMMAND ${CMAKE_COMMAND} -E env ROCM_PATH=${ROCM_ENV} ${CMAKE_COMMAND} --install ${EP_LIBRARY_BINARY_DIR_ROCM} --prefix ${EP_LIBRARY_INSTALL_DIR_ROCM}

        LOG_CONFIGURE ON
        LOG_BUILD ON
        LOG_INSTALL ON
    )
endif()
