cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0135 NEW)

# Set the project name and version
project(IHV_GGML VERSION 1.0)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include(ExternalProject)

message(STATUS "Building with IHV GGML support...")

# Define directories for GGML
set(IHV_GGML_BINARY_DIR ${CMAKE_BINARY_DIR}/IHV/GGML)
set(IHV_GGML_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(IHV_GGML_INSTALL_DIR ${CMAKE_BINARY_DIR}/IHV/GGML/bin)

set(ENV_PATH $ENV{PATH})

if(WIN32)
string(REPLACE "\\" "/" ENV_PATH ${ENV_PATH})
endif()

set(GGML_VERSION_INFO "")
find_program(GIT_EXECUTABLE NAMES git git.exe NO_CMAKE_FIND_ROOT_PATH)
if(GIT_EXECUTABLE)
    message(STATUS "Found Git: ${GIT_EXECUTABLE}")
else()
    # Set explicit version info to avoid Git dependency
    set(GGML_VERSION_INFO "-DGGML_BUILD_NUMBER=1 -DGGML_BUILD_COMMIT=unknown")
    message(WARNING "Git not found. Build info will not be accurate.")
endif()

set(GIT_EXECUTABLE_ARG "")
if(NOT IOS)
    set(GIT_EXECUTABLE_ARG -DGIT_EXECUTABLE=${GIT_EXECUTABLE})
endif()

# Forward hip_DIR from the top-level so the child configure can find HIP
set(HIP_DIR_ARG)
if(DEFINED hip_DIR AND hip_DIR)
    file(TO_CMAKE_PATH "${hip_DIR}" hip_DIR_CMAKE)
    set(HIP_DIR_ARG -Dhip_DIR:PATH=${hip_DIR_CMAKE})
endif()

# Forward hipblas_DIR and rocblas_DIR if available
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

set(IOS_ARGS "")
if(IOS)
    list(APPEND IOS_ARGS
        -DXCODE_ATTRIBUTE_CODE_SIGN_IDENTITY=
        -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=ON
    )
endif()

# Detect SPIRV-Headers for Vulkan builds (mirrors the check in llama.cpp ci/run.sh)
set(SPIRV_HEADERS_DIR_ARG)
if(MLPERF_IHV_GGML_VULKAN)
    set(SPIRV_HEADERS_CANDIDATES "/usr/local/lib/cmake/vulkan/SPIRV-Headers")
    if(WIN32 AND DEFINED ENV{VULKAN_SDK})
        file(TO_CMAKE_PATH "$ENV{VULKAN_SDK}" _vk_sdk)
        list(APPEND SPIRV_HEADERS_CANDIDATES
            "${_vk_sdk}/Lib/cmake"
            "${_vk_sdk}/Lib/cmake/SPIRV-Headers"
            "${_vk_sdk}/share/cmake/SPIRV-Headers")
    endif()
    foreach(_dir IN LISTS SPIRV_HEADERS_CANDIDATES)
        if(EXISTS "${_dir}/SPIRV-HeadersConfig.cmake")
            set(SPIRV_HEADERS_DIR_ARG "-DSPIRV-Headers_DIR=${_dir}")
            break()
        endif()
    endforeach()
endif()

set(GGML_PLATFORM_ARGS "")
if(WIN32 AND CMAKE_GENERATOR_PLATFORM)
    set(GGML_PLATFORM_ARGS "-A" "${CMAKE_GENERATOR_PLATFORM}")
endif()

# Forward the resolved CUDA compiler so the EP sub-builds use the same toolkit
# the top-level detected (and not a stray nvcc earlier on PATH).
set(GGML_CUDA_COMPILER_ARG "")
if(MLPERF_IHV_GGML_CUDA AND CUDAToolkit_NVCC_EXECUTABLE)
    set(GGML_CUDA_COMPILER_ARG "-DCMAKE_CUDA_COMPILER=${CUDAToolkit_NVCC_EXECUTABLE}")
endif()

# CUDA architectures for the llama.cpp CUDA EP. Use a pipe-separated list so
# the value survives nested ExternalProject command generation.
set(MLPERF_GGML_CUDA_ARCHS "75-virtual|80-virtual|86-real|89-real|120a-real|121a-real"
    CACHE STRING "GGML CUDA architectures ('|'-separated; 'native' = local GPU)")

# Forward the cross toolchain so the GGML EP sub-build targets the cross arch.
set(GGML_TOOLCHAIN_ARG "")
if(CMAKE_TOOLCHAIN_FILE)
    set(GGML_TOOLCHAIN_ARG "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
endif()

ExternalProject_Add(ihv_ggml
    SOURCE_DIR ${IHV_GGML_SOURCE_DIR}
    BINARY_DIR ${IHV_GGML_BINARY_DIR}
    INSTALL_DIR ${IHV_GGML_INSTALL_DIR}
    BUILD_ALWAYS ON

    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E echo "Configuring IHV GGML for ${CMAKE_BUILD_TYPE}"
    COMMAND ${CMAKE_COMMAND} -B ${IHV_GGML_BINARY_DIR} -S ${IHV_GGML_SOURCE_DIR}
        -G "${CMAKE_GENERATOR}"
        ${GGML_PLATFORM_ARGS}
        ${GGML_TOOLCHAIN_ARG}
        -DCMAKE_INSTALL_PREFIX=${IHV_GGML_INSTALL_DIR}
        -DCMAKE_CXX_STANDARD=20
        -DENV_PATH=${ENV_PATH}
        -DGGML_METAL=${MLPERF_IHV_GGML_METAL}
        -DGGML_VULKAN=${MLPERF_IHV_GGML_VULKAN}
        -DGGML_CUDA=${MLPERF_IHV_GGML_CUDA}
        -DMLPERF_GGML_CUDA_ARCHS=${MLPERF_GGML_CUDA_ARCHS}
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DGGML_ROCM=${MLPERF_IHV_GGML_ROCM}
        -DIOS=${IOS}
        ${GGML_CUDA_COMPILER_ARG}
        ${GIT_EXECUTABLE_ARG}
        ${HIP_DIR_ARG}
        ${HIPBLAS_DIR_ARG}
        ${HIPBLAS_COMMON_DIR_ARG}
        ${ROCBLAS_DIR_ARG}
        ${GGML_VERSION_INFO}
        ${SPIRV_HEADERS_DIR_ARG}
        ${IOS_ARGS}

    BUILD_COMMAND ${CMAKE_COMMAND} -E echo "Starting IHV GGML build for ${CMAKE_BUILD_TYPE}..."
    COMMAND ${CMAKE_COMMAND} --build ${IHV_GGML_BINARY_DIR} --config $<CONFIG> --parallel

    INSTALL_COMMAND ${CMAKE_COMMAND} --install ${IHV_GGML_BINARY_DIR} --config $<CONFIG> --prefix ${IHV_GGML_INSTALL_DIR}/$<CONFIG> 
    LOG_CONFIGURE ON
    LOG_BUILD ON
    LOG_INSTALL ON
    #BUILD_BYPRODUCTS 
    INSTALL_BYPRODUCTS
        # Those targets are not always built, but there is no harm in declaring too much sideproducts
        ${CMAKE_BINARY_DIR}/IHV/GGML/bin/${CMAKE_BUILD_TYPE}/${CMAKE_SHARED_LIBRARY_PREFIX}IHV_GGML_EPs${CMAKE_SHARED_LIBRARY_SUFFIX}
        ${CMAKE_BINARY_DIR}/IHV/GGML/CUDA/bin/${CMAKE_BUILD_TYPE}/${CMAKE_SHARED_LIBRARY_PREFIX}llama_cpp_CUDA${CMAKE_SHARED_LIBRARY_SUFFIX}
        ${CMAKE_BINARY_DIR}/IHV/GGML/Vulkan/bin/${CMAKE_BUILD_TYPE}/${CMAKE_SHARED_LIBRARY_PREFIX}llama_cpp_Vulkan${CMAKE_SHARED_LIBRARY_SUFFIX}
)


set_target_properties(ihv_ggml PROPERTIES FOLDER "IHV")
