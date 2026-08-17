cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0135 NEW)

project(IHV_Diffusers VERSION 1.0)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include(ExternalProject)

message(STATUS "Building with IHV Diffusers (Python) support...")

set(IHV_DIFFUSERS_BINARY_DIR ${CMAKE_BINARY_DIR}/IHV/Diffusers)
set(IHV_DIFFUSERS_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
# Install directly into the top-level runtime output for this config --
# CMAKE_RUNTIME_OUTPUT_DIRECTORY already contains $<CONFIG>. No post-build
# copy step needed: DLL + python/ + standalone/ land at the
# final destination during cmake --install.
set(IHV_DIFFUSERS_INSTALL_DIR ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/IHV/Diffusers)

set(ENV_PATH $ENV{PATH})

if(WIN32)
  string(REPLACE "\\" "/" ENV_PATH ${ENV_PATH})
endif()

if(CMAKE_GENERATOR MATCHES "Visual Studio")
    set(DIFFUSERS_PLATFORM_ARGS -A ${CMAKE_GENERATOR_PLATFORM})
else()
    set(DIFFUSERS_PLATFORM_ARGS "")
endif()

# INSTALL_DIR is intentionally omitted: ExternalProject would otherwise
# try to mkdir it at outer configure time, which can't expand $<CONFIG>
# in IHV_DIFFUSERS_INSTALL_DIR. The install path is supplied only via
# --prefix on INSTALL_COMMAND, which is a build-time command where
# $<CONFIG> resolves correctly.
ExternalProject_Add(ihv_diffusers
    SOURCE_DIR ${IHV_DIFFUSERS_SOURCE_DIR}
    BINARY_DIR ${IHV_DIFFUSERS_BINARY_DIR}
    BUILD_ALWAYS ON

    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E echo "Configuring IHV Diffusers for ${CMAKE_BUILD_TYPE}"
    COMMAND ${CMAKE_COMMAND} -B ${IHV_DIFFUSERS_BINARY_DIR} -S ${IHV_DIFFUSERS_SOURCE_DIR}
        -G "${CMAKE_GENERATOR}"
        ${DIFFUSERS_PLATFORM_ARGS}
        -DCMAKE_CXX_STANDARD=20
        -DENV_PATH=${ENV_PATH}
        -DCMAKE_INSTALL_PREFIX=${IHV_DIFFUSERS_INSTALL_DIR}
        -DMLPERF_IHV_DIFFUSERS_INSTALL_PYTHON=${MLPERF_IHV_DIFFUSERS_INSTALL_PYTHON}
        -DMLPERF_IHV_DIFFUSERS_BUILD_STANDALONE=${MLPERF_IHV_DIFFUSERS_BUILD_STANDALONE}
        -DMLPERF_IHV_DIFFUSERS_NVIDIA=${MLPERF_IHV_DIFFUSERS_NVIDIA}
        -DMLPERF_IHV_DIFFUSERS_APPLE=${MLPERF_IHV_DIFFUSERS_APPLE}
        -DMLPERF_IHV_DIFFUSERS_AMD=${MLPERF_IHV_DIFFUSERS_AMD}
        $<$<BOOL:${APPLE}>:-DCMAKE_OSX_DEPLOYMENT_TARGET=13.0>

    BUILD_COMMAND ${CMAKE_COMMAND} -E echo "Starting IHV Diffusers build for ${CMAKE_BUILD_TYPE}..."
    COMMAND ${CMAKE_COMMAND} --build ${IHV_DIFFUSERS_BINARY_DIR} --config $<CONFIG>

    INSTALL_COMMAND ${CMAKE_COMMAND} --install ${IHV_DIFFUSERS_BINARY_DIR} --config $<CONFIG> --prefix ${IHV_DIFFUSERS_INSTALL_DIR}
    LOG_CONFIGURE ON
    LOG_BUILD ON
    LOG_INSTALL ON
)

set_target_properties(ihv_diffusers PROPERTIES FOLDER "IHV")

# Wrapper around the inner diffusers_standalone target so it's reachable
# from the top-level build / IDE.
if(MLPERF_IHV_DIFFUSERS_BUILD_STANDALONE
   AND MLPERF_IHV_DIFFUSERS_INSTALL_PYTHON)
    add_custom_target(diffusers_standalone
        COMMAND ${CMAKE_COMMAND} --build ${IHV_DIFFUSERS_BINARY_DIR}
                                 --target diffusers_standalone
                                 --config $<CONFIG>
        COMMENT "Refreshing Diffusers PyInstaller standalone bundle (sentinel-gated)"
        DEPENDS ihv_diffusers
        VERBATIM)
    set_target_properties(diffusers_standalone PROPERTIES FOLDER "IHV")
endif()
