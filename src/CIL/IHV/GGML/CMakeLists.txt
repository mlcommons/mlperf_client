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

set(IOS_ARGS "")
if(IOS)
    list(APPEND IOS_ARGS
        -DXCODE_ATTRIBUTE_CODE_SIGN_IDENTITY=
        -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=ON
    )
endif()

ExternalProject_Add(ihv_ggml
    SOURCE_DIR ${IHV_GGML_SOURCE_DIR}
    BINARY_DIR ${IHV_GGML_BINARY_DIR}
    INSTALL_DIR ${IHV_GGML_INSTALL_DIR}
    BUILD_ALWAYS ON

    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E echo "Configuring IHV GGML for ${CMAKE_BUILD_TYPE}"
    COMMAND ${CMAKE_COMMAND} -B ${IHV_GGML_BINARY_DIR} -S ${IHV_GGML_SOURCE_DIR}
        -G "${CMAKE_GENERATOR}"
        -DCMAKE_INSTALL_PREFIX=${IHV_GGML_INSTALL_DIR}
        -DCMAKE_CXX_STANDARD=20
        -DENV_PATH=${ENV_PATH}
        -DGGML_METAL=${MLPERF_IHV_GGML_METAL}
        -DGGML_VULKAN=${MLPERF_IHV_GGML_VULKAN}
        -DGGML_CUDA=${MLPERF_IHV_GGML_CUDA}
        -DIOS=${IOS}
        ${GIT_EXECUTABLE_ARG}
        ${GGML_VERSION_INFO}
        ${IOS_ARGS}

    BUILD_COMMAND ${CMAKE_COMMAND} -E echo "Starting IHV GGML build for ${CMAKE_BUILD_TYPE}..."
    COMMAND ${CMAKE_COMMAND} --build ${IHV_GGML_BINARY_DIR} --config $<CONFIG> 

    INSTALL_COMMAND ${CMAKE_COMMAND} --install ${IHV_GGML_BINARY_DIR} --config $<CONFIG> --prefix ${IHV_GGML_INSTALL_DIR}/$<CONFIG> 
    LOG_CONFIGURE ON
    LOG_BUILD ON
    LOG_INSTALL ON
)


set_target_properties(ihv_ggml PROPERTIES FOLDER "IHV")