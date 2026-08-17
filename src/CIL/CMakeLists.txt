cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0135 NEW)

file(READ "${CMAKE_SOURCE_DIR}/version.json" VERSION_JSON)
string(JSON MAJOR_VERSION GET ${VERSION_JSON} major)
string(JSON MINOR_VERSION GET ${VERSION_JSON} minor)
string(JSON PATCH_VERSION GET ${VERSION_JSON} patch)
string(JSON VERSION_SUFFIX GET ${VERSION_JSON} suffix)
# calculate the build number
execute_process(
        COMMAND git rev-parse --short HEAD
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_COMMIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(STRIP "${VERSION_SUFFIX}" STRIPPED_SUFFIX)
if(STRIPPED_SUFFIX STREQUAL "")
    set(VERSION_STRING "${MAJOR_VERSION}.${MINOR_VERSION}.${PATCH_VERSION}.${GIT_COMMIT_HASH}")
else()
    set(VERSION_STRING "${MAJOR_VERSION}.${MINOR_VERSION}.${PATCH_VERSION}-${VERSION_SUFFIX}.${GIT_COMMIT_HASH}")
endif()
set(VERSION_SHORT_STRING "${MAJOR_VERSION}.${MINOR_VERSION}.${PATCH_VERSION}")
message(STATUS "Git commit hash: ${GIT_COMMIT_HASH}")
configure_file(./version.h.in "${CMAKE_CURRENT_BINARY_DIR}/version.h" @ONLY)
#  add custom command to make sure cmake will track the changes in version.json
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/version.json")
set_property(GLOBAL PROPERTY APP_VERSION_STRING ${VERSION_STRING})
set_property(GLOBAL PROPERTY APP_VERSION_SHORT_STRING ${VERSION_SHORT_STRING})
# read the content of the version.h and display it
file(READ "${CMAKE_CURRENT_BINARY_DIR}/version.h" VERSION_H)
message(STATUS "version file content: ${VERSION_H}")
# Set the project name and version
project(CIL VERSION 1.0)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_program(GIT_EXECUTABLE NAMES git git.exe NO_CMAKE_FIND_ROOT_PATH)

if(WIN32)
    # Use the TARGET platform (CMAKE_GENERATOR_PLATFORM, set by -A) for our
    # tool ExternalProjects on Windows, not the HOST CMAKE_SYSTEM_PROCESSOR.
    # CMAKE_SYSTEM_PROCESSOR reports the host arch when cross-compiling
    # (e.g. x64 host -> ARM64 target), which would otherwise embed host-arch
    # DLLs into the cross-compiled binary and fail at runtime on the target
    # device (LoadLibrary returns ERROR_BAD_EXE_FORMAT /
    # ERROR_EXE_MACHINE_TYPE_MISMATCH).
    if(CMAKE_GENERATOR_PLATFORM)
        set(TOOLS_PLATFORM_ARGS "-A" "${CMAKE_GENERATOR_PLATFORM}")
    else()
        string(TOUPPER ${CMAKE_SYSTEM_PROCESSOR} _TOOLS_PROC)
        if(_TOOLS_PROC MATCHES "AMD64")
            set(TOOLS_PLATFORM_ARGS "-A" "x64")
        elseif(_TOOLS_PROC MATCHES "AARCH64|ARM64")
            set(TOOLS_PLATFORM_ARGS "-A" "ARM64")
        endif()
    endif()
endif()

# Register an ExternalProject for a tool living under src/CIL/tools/<NAME>.
# Applies the MSVC / toolchain / Apple / Windows-platform arguments shared by
# all CIL tools. Per-tool CMake args are passed via EXTRA_CMAKE_ARGS.
function(cil_add_tool_external_project NAME)
    cmake_parse_arguments(_T "" "" "EXTRA_CMAKE_ARGS" ${ARGN})

    set(_src    ${CMAKE_CURRENT_SOURCE_DIR}/tools/${NAME})
    set(_build  ${CMAKE_BINARY_DIR}/_deps/${NAME})
    set(_output ${CMAKE_BINARY_DIR}/${NAME}/bin)
    string(TOUPPER ${NAME} _NAME_UPPER)

    set(_args
        -DCMAKE_BUILD_TYPE=$<CONFIG>
        -D${_NAME_UPPER}_OUTPUT_DIR=${_output}
        ${_T_EXTRA_CMAKE_ARGS}
    )

    if(MSVC)
        list(APPEND _args
            -DCMAKE_MSVC_RUNTIME_LIBRARY=${CMAKE_MSVC_RUNTIME_LIBRARY}
            "-DCMAKE_CXX_FLAGS_RELEASE=${CMAKE_CXX_FLAGS_RELEASE}"
            "-DCMAKE_CXX_FLAGS_DEBUG=${CMAKE_CXX_FLAGS_DEBUG}"
            "-DCMAKE_C_FLAGS_RELEASE=${CMAKE_C_FLAGS_RELEASE}"
            "-DCMAKE_C_FLAGS_DEBUG=${CMAKE_C_FLAGS_DEBUG}"
        )
    endif()

    if(CMAKE_TOOLCHAIN_FILE)
        list(APPEND _args -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE})
    endif()

    if(APPLE)
        list(APPEND _args
            -DCMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}
            -DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}
            -DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}
        )
        if(IOS)
            list(APPEND _args
                -DCMAKE_OSX_SYSROOT=${CMAKE_OSX_SYSROOT}
                -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=ON
                -DXCODE_ATTRIBUTE_CODE_SIGN_IDENTITY=
            )
        endif()
    endif()

    ExternalProject_Add(${NAME}
        SOURCE_DIR  ${_src}
        BINARY_DIR  ${_build}
        CONFIGURE_COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" ${TOOLS_PLATFORM_ARGS}
            -S ${_src}
            -B ${_build}
            ${_args}
        BUILD_COMMAND ${CMAKE_COMMAND} --build ${_build} --config $<CONFIG>
        INSTALL_COMMAND ""
        BUILD_ALWAYS ON
        LOG_CONFIGURE ON
        LOG_BUILD ON
        LOG_OUTPUT_ON_FAILURE ON
    )
endfunction()

set(_TOKENIZER_EXTRA_ARGS
    -DFETCHCONTENT_SOURCE_DIR_JSON=${CMAKE_SOURCE_DIR}/deps/JSON
)
if(GIT_EXECUTABLE)
    list(APPEND _TOKENIZER_EXTRA_ARGS -DGIT_EXECUTABLE=${GIT_EXECUTABLE})
endif()

cil_add_tool_external_project(tokenizer
    EXTRA_CMAKE_ARGS ${_TOKENIZER_EXTRA_ARGS}
)

cil_add_tool_external_project(imageio)

set(DEPS_PACKER_BUILD_DIR ${CMAKE_BINARY_DIR}/_deps/deps_packer)
if(WIN32)
    # Normalize the processor name to upper case
    string(TOUPPER ${CMAKE_SYSTEM_PROCESSOR} NORMALIZED_PROCESSOR)

    if(NORMALIZED_PROCESSOR MATCHES "AMD64")
        set(DEPS_PACKER_GENERATOR_PLATFORM_COMMAND "-A x64")
        set(DEPS_PACKER_GENERATOR_PLATFORM x64)
    elseif(NORMALIZED_PROCESSOR MATCHES "AARCH64|ARM64")
        set(DEPS_PACKER_GENERATOR_PLATFORM_COMMAND "-A ARM64")
        set(DEPS_PACKER_GENERATOR_PLATFORM ARM64)
    else()
        message(FATAL_ERROR "Unsupported system processor: ${CMAKE_SYSTEM_PROCESSOR}")
    endif()
elseif(APPLE) # Apple (macOS and iOS)
    # DepsPacker should run on the host macOS
	set(DEPS_PACKER_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../toolchain/macos_arm64.toolchain.cmake")
	set(DEPS_PACKER_GENERATOR_PLATFORM_COMMAND "-DCMAKE_TOOLCHAIN_FILE=${DEPS_PACKER_TOOLCHAIN_FILE}")
	set(DEPS_PACKER_GENERATOR_PLATFORM "arm64")
elseif(UNIX AND NOT APPLE)  # Linux and other Unix-like systems
    # We need to build for the build host, not for the target!
    set(DEPS_PACKER_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/toolchain/linux.host.cmake")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
	set(DEPS_PACKER_GENERATOR_PLATFORM_COMMAND "-DCMAKE_TOOLCHAIN_FILE=${DEPS_PACKER_TOOLCHAIN_FILE}")
        set(DEPS_PACKER_GENERATOR_PLATFORM x86_64)
    else()
        message(FATAL_ERROR "Unsupported system processor: ${CMAKE_SYSTEM_PROCESSOR}")
    endif()
endif()

if(WIN32)
    set(deps_packer_bin "${DEPS_PACKER_BUILD_DIR}/${DEPS_PACKER_GENERATOR_PLATFORM}/$<CONFIG>/DepsPacker")
elseif(UNIX AND NOT APPLE)
    set(deps_packer_bin "${DEPS_PACKER_BUILD_DIR}/DepsPacker")
else()
    set(deps_packer_bin "${DEPS_PACKER_BUILD_DIR}/${CMAKE_HOST_SYSTEM_PROCESSOR}/$<CONFIG>/DepsPacker")
endif()

set(DEPS_PACKER_RUNTIME_OUTPUT_DIRECTORY ${DEPS_PACKER_BUILD_DIR}/${DEPS_PACKER_GENERATOR_PLATFORM})

# Define the external project with custom commands using CMake variables
ExternalProject_Add(DepsPacker
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps_packer
    BINARY_DIR ${DEPS_PACKER_BUILD_DIR}
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" ${DEPS_PACKER_GENERATOR_PLATFORM_COMMAND} -S ${CMAKE_CURRENT_SOURCE_DIR}/deps_packer -B ${DEPS_PACKER_BUILD_DIR} -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=${DEPS_PACKER_RUNTIME_OUTPUT_DIRECTORY}
    BUILD_COMMAND ${CMAKE_COMMAND} --build ${DEPS_PACKER_BUILD_DIR} --config $<CONFIG>
    INSTALL_COMMAND ""
    BYPRODUCTS "${deps_packer_bin}"
)

add_subdirectory(common)

add_subdirectory(IHV)

if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
elseif(UNIX AND NOT APPLE)  # Linux specific
    # For Linux, we want to ensure position-independent code
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
    # Add Linux-specific compiler flags
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
endif()

# Define the source files for the library
file(GLOB_RECURSE SOURCES "*.cpp")

# Define the header files for the library
file(GLOB_RECURSE HEADERS "*.h")

if(APPLE)
    file(GLOB_RECURSE MM_SOURCES "*.m" "*.mm")
    list(APPEND SOURCES ${MM_SOURCES})
endif()

if(APPLE)
	list(FILTER HEADERS EXCLUDE REGEX "system_info_provider_windows.h")
	list(FILTER SOURCES EXCLUDE REGEX "system_info_provider_windows.cpp")
	list(FILTER HEADERS EXCLUDE REGEX "system_info_provider_linux.h")
	list(FILTER SOURCES EXCLUDE REGEX "system_info_provider_linux.cpp")
	list(FILTER HEADERS EXCLUDE REGEX "performance_query.h")
	list(FILTER SOURCES EXCLUDE REGEX "performance_query.cpp")
	list(FILTER HEADERS EXCLUDE REGEX "performance_counter_data.h")
	list(FILTER SOURCES EXCLUDE REGEX "performance_counter_data.cpp")
	list(FILTER HEADERS EXCLUDE REGEX "performance_counter_group.h")
	list(FILTER SOURCES EXCLUDE REGEX "performance_counter_group.cpp")
endif()
	
if(WIN32 OR IOS)
	list(FILTER HEADERS EXCLUDE REGEX "system_info_provider_macos.h")
	list(FILTER SOURCES EXCLUDE REGEX "system_info_provider_macos.cpp")
	list(FILTER HEADERS EXCLUDE REGEX "system_info_provider_linux.h")
	list(FILTER SOURCES EXCLUDE REGEX "system_info_provider_linux.cpp")
endif()
if (WIN32 OR IOS)
	list(FILTER HEADERS EXCLUDE REGEX "smc_reader.h")
	list(FILTER SOURCES EXCLUDE REGEX "smc_reader.cpp")
	list(FILTER HEADERS EXCLUDE REGEX "io_report_reader.h")
	list(FILTER SOURCES EXCLUDE REGEX "io_report_reader.cpp")	
endif()

if(UNIX AND NOT APPLE)  # Linux
	list(FILTER HEADERS EXCLUDE REGEX "system_info_provider_windows.h")
	list(FILTER SOURCES EXCLUDE REGEX "system_info_provider_windows.cpp")
	list(FILTER HEADERS EXCLUDE REGEX "performance_query.h")
	list(FILTER SOURCES EXCLUDE REGEX "performance_query.cpp")
	list(FILTER HEADERS EXCLUDE REGEX "performance_counter_data.h")
	list(FILTER SOURCES EXCLUDE REGEX "performance_counter_data.cpp")
	list(FILTER HEADERS EXCLUDE REGEX "performance_counter_group.h")
	list(FILTER SOURCES EXCLUDE REGEX "performance_counter_group.cpp")
	list(FILTER HEADERS EXCLUDE REGEX "system_info_provider_macos.h")
	list(FILTER SOURCES EXCLUDE REGEX "system_info_provider_macos.cpp")
	list(FILTER HEADERS EXCLUDE REGEX "io_report_reader.h")
	list(FILTER SOURCES EXCLUDE REGEX "io_report_reader.cpp")		
	list(FILTER HEADERS EXCLUDE REGEX "smc_reader.h")
	list(FILTER SOURCES EXCLUDE REGEX "smc_reader.cpp")
endif()

if(NOT IOS)
	list(FILTER HEADERS EXCLUDE REGEX "system_info_provider_ios.h")
	list(FILTER SOURCES EXCLUDE REGEX "system_info_provider_ios.mm")
endif()

# IHV subprocess IPC transport: compile exactly one platform backend.
if(WIN32)
	list(FILTER SOURCES EXCLUDE REGEX "api_handler_ipc_posix.cpp")
else()
	list(FILTER SOURCES EXCLUDE REGEX "api_handler_ipc_win.cpp")
endif()

if (NOT APPLE)
    list(FILTER SOURCES EXCLUDE REGEX "apple/")
    list(FILTER HEADERS EXCLUDE REGEX "apple/")
endif()

list(FILTER SOURCES EXCLUDE REGEX "deps_packer/")
list(FILTER HEADERS EXCLUDE REGEX "deps_packer/")

# tools/tokenizer/ is an ExternalProject — keep in IDE tree but don't compile as part of CIL.
foreach(SOURCE_FILE ${SOURCES})
    if(SOURCE_FILE MATCHES "tools/tokenizer/")
        set_source_files_properties(${SOURCE_FILE} PROPERTIES HEADER_FILE_ONLY TRUE)
    endif()
endforeach()

# tools/imageio/ is an ExternalProject — keep in IDE tree but don't compile as part of CIL.
foreach(SOURCE_FILE ${SOURCES})
    if(SOURCE_FILE MATCHES "tools/imageio/")
        set_source_files_properties(${SOURCE_FILE} PROPERTIES HEADER_FILE_ONLY TRUE)
    endif()
endforeach()

# tools/agentic/ are standalone executables (own ExternalProject) — keep in the
# IDE tree but never compile them into CIL (they each define their own main()).
foreach(SOURCE_FILE ${SOURCES})
    if(SOURCE_FILE MATCHES "tools/agentic/")
        set_source_files_properties(${SOURCE_FILE} PROPERTIES HEADER_FILE_ONLY TRUE)
    endif()
endforeach()

# Temporary to be able to see NativeOpenVINO in main project, 
# On long term, this will be excluded everytime
if(NOT MLPERF_IHV_NATIVE_OPENVINO)
list(FILTER SOURCES EXCLUDE REGEX "IHV/NativeOpenVINO")
list(FILTER HEADERS EXCLUDE REGEX "IHV/NativeOpenVINO")
else()
foreach(SOURCE_FILE ${SOURCES})
    if(SOURCE_FILE MATCHES "IHV/NativeOpenVINO")
        set_source_files_properties(${SOURCE_FILE} PROPERTIES HEADER_FILE_ONLY TRUE)
    endif()
endforeach()
endif()
if(NOT (MLPERF_IHV_ORT_GENAI))
list(FILTER SOURCES EXCLUDE REGEX "IHV/OrtGenAI")
list(FILTER HEADERS EXCLUDE REGEX "IHV/OrtGenAI")
else()
# Exclude files in the TMP directory from being compiled
foreach(SOURCE_FILE ${SOURCES})
    if(SOURCE_FILE MATCHES "IHV/OrtGenAI")
        set_source_files_properties(${SOURCE_FILE} PROPERTIES HEADER_FILE_ONLY TRUE)
    endif()
endforeach()
endif()
if(NOT MLPERF_IHV_ORT_GENAI_RYZENAI)
list(FILTER SOURCES EXCLUDE REGEX "IHV/OrtGenAI-RyzenAI")
list(FILTER HEADERS EXCLUDE REGEX "IHV/OrtGenAI-RyzenAI")
else()
# Exclude files in the TMP directory from being compiled
foreach(SOURCE_FILE ${SOURCES})
    if(SOURCE_FILE MATCHES "IHV/OrtGenAI-RyzenAI")
        set_source_files_properties(${SOURCE_FILE} PROPERTIES HEADER_FILE_ONLY TRUE)
    endif()
endforeach()
endif()
if(NOT MLPERF_IHV_GGML)
list(FILTER SOURCES EXCLUDE REGEX "IHV/GGML")
list(FILTER HEADERS EXCLUDE REGEX "IHV/GGML")
else()
# Exclude files in the TMP directory from being compiled
foreach(SOURCE_FILE ${SOURCES})
    if(SOURCE_FILE MATCHES "IHV/GGML")
        set_source_files_properties(${SOURCE_FILE} PROPERTIES HEADER_FILE_ONLY TRUE)
    endif()
endforeach()
endif()
if(NOT MLPERF_IHV_NATIVE_QNN)
list(FILTER SOURCES EXCLUDE REGEX "IHV/NativeQNN")
list(FILTER HEADERS EXCLUDE REGEX "IHV/NativeQNN")
else()
# Exclude files in the TMP directory from being compiled
foreach(SOURCE_FILE ${SOURCES})
    if(SOURCE_FILE MATCHES "IHV/NativeQNN")
        set_source_files_properties(${SOURCE_FILE} PROPERTIES HEADER_FILE_ONLY TRUE)
    endif()
endforeach()
endif()
if(NOT MLPERF_IHV_MLX)
list(FILTER SOURCES EXCLUDE REGEX "IHV/MLX")
list(FILTER HEADERS EXCLUDE REGEX "IHV/MLX")
else()
# Exclude files in the TMP directory from being compiled
foreach(SOURCE_FILE ${SOURCES})
    if(SOURCE_FILE MATCHES "IHV/MLX")
        set_source_files_properties(${SOURCE_FILE} PROPERTIES HEADER_FILE_ONLY TRUE)
    endif()
endforeach()
endif()
if(NOT MLPERF_IHV_WINDOWSML)
list(FILTER SOURCES EXCLUDE REGEX "IHV/WindowsML")
list(FILTER HEADERS EXCLUDE REGEX "IHV/WindowsML")
else()
# Exclude files in the TMP directory from being compiled
foreach(SOURCE_FILE ${SOURCES})
    if(SOURCE_FILE MATCHES "IHV/WindowsML")
        set_source_files_properties(${SOURCE_FILE} PROPERTIES HEADER_FILE_ONLY TRUE)
    endif()
endforeach()
endif()
# Diffusers is always built as an ExternalProject (has pybind11 dep) — never compile in CIL
list(FILTER SOURCES EXCLUDE REGEX "IHV/Diffusers")
list(FILTER HEADERS EXCLUDE REGEX "IHV/Diffusers")

list(FILTER SOURCES EXCLUDE REGEX "common/")
list(FILTER HEADERS EXCLUDE REGEX "common/")

# Define source groups
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Source files" FILES ${SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Header files" FILES ${HEADERS})

set(LOG4CXXCONFIG_XML "${CMAKE_SOURCE_DIR}/data/Log4CxxConfig.xml")
set(CONFIGSCHEMA_JSON "${CMAKE_SOURCE_DIR}/data/ConfigSchema.json")
set(LLM_INPUT_SCHEMA_JSON "${CMAKE_SOURCE_DIR}/data/LLMInputSchema.json")
set(IMAGE_INPUT_SCHEMA_JSON "${CMAKE_SOURCE_DIR}/data/ImageInputSchema.json")
set(OUTPUT_RESULTS_SCHEMA_JSON "${CMAKE_SOURCE_DIR}/data/OutputResultsSchema.json")
set(DATA_VERIFICATION_JSON "${CMAKE_SOURCE_DIR}/data/data_verification.json")
set(DATA_VERIFICATION_SCHEMA_JSON "${CMAKE_SOURCE_DIR}/data/DataVerificationSchema.json")

set(VENDORS_DEFAULT_CONFIGS_DIR "${CMAKE_SOURCE_DIR}/data/configs/vendors_default")

file(GLOB_RECURSE VENDORS_ALL_CONFIGS CONFIGURE_DEPENDS "${VENDORS_DEFAULT_CONFIGS_DIR}/*.json")
set(CONFIG_VERIFICATION_FILES "")
set(CONFIG_VERIFICATION_TARGETS "")

function(add_vendors_config_group CATEGORY)
    if("${CATEGORY}" STREQUAL "")
        # Special case: ALL
        set(OUTPUT_FILE "${CMAKE_BINARY_DIR}/config_verification.json")
        set(FILES "${VENDORS_ALL_CONFIGS}")
        set(TARGET_NAME compute_configs_hashes)
        set(MSG "Computing hashes for vendor default configs")
    else()
        set(OUTPUT_FILE "${CMAKE_BINARY_DIR}/config_${CATEGORY}_verification.json")
        set(FILES "${VENDORS_ALL_CONFIGS}")
        list(FILTER FILES INCLUDE REGEX "/vendors_default/[^/]+/${CATEGORY}/")
        set(TARGET_NAME compute_${CATEGORY}_configs_hashes)
        set(MSG "Computing hashes for vendor ${CATEGORY} configs")
    endif()
    add_custom_target(
        ${TARGET_NAME}
        ALL
        COMMAND ${CMAKE_COMMAND} -DFILES_TO_HASH="${FILES}" -DOUTPUT_FILE="${OUTPUT_FILE}" -P "${CMAKE_CURRENT_SOURCE_DIR}/compute_hashes.cmake"
	BYPRODUCTS "${OUTPUT_FILE}"
    )
    list(APPEND CONFIG_VERIFICATION_FILES "${OUTPUT_FILE}")
    list(APPEND CONFIG_VERIFICATION_TARGETS "${TARGET_NAME}")
    set(CONFIG_VERIFICATION_FILES "${CONFIG_VERIFICATION_FILES}" PARENT_SCOPE)
    set(CONFIG_VERIFICATION_TARGETS "${CONFIG_VERIFICATION_TARGETS}" PARENT_SCOPE)
endfunction()

add_vendors_config_group("")
add_vendors_config_group("experimental")
add_vendors_config_group("extended")

if(MLPERF_PACK_VENDORS_DEFAULT)
    set(VENDORS_DEFAULT_ZIP "${CMAKE_BINARY_DIR}/vendors_default.zip")
    set(VENDORS_DEFAULT_EXCLUDE_REGEX "")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -DVENDORS_DEFAULT_CONFIGS_DIR=${VENDORS_DEFAULT_CONFIGS_DIR} -DVENDORS_DEFAULT_ZIP=${VENDORS_DEFAULT_ZIP} -DVENDORS_DEFAULT_EXCLUDE_REGEX=${VENDORS_DEFAULT_EXCLUDE_REGEX} -P "${CMAKE_CURRENT_SOURCE_DIR}/pack_vendors_default.cmake"
        WORKING_DIRECTORY "${VENDORS_DEFAULT_CONFIGS_DIR}"
        RESULT_VARIABLE PACK_RESULT
    )
    if(NOT PACK_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to pack vendors default")
    endif()
endif()

# All platforms use .json.in templates so cloud URL prefixes per EP are
# centralized here and easy to flip per-release. Pass
# -DMLPERF_<EP>_PREFIX=<URL> (or file:// for local dev) to override any
# individually.
#
# URL convention:
#   https://client.mlcommons-storage.org/deps/<release>/binaries/<EP>/<arch>

# Pick the .json.in template per platform/arch and the generated filename.
if(WIN32)
    if(CMAKE_GENERATOR_PLATFORM MATCHES "x64")
        set(_ep_template_name "ep_dependencies_config_windows_x64")
        set(_arch_dir "x64")
    else()
        set(_ep_template_name "ep_dependencies_config_windows_ARM")
        set(_arch_dir "ARM")
    endif()
elseif(IOS)
    set(_ep_template_name "ep_dependencies_config_iOS")
elseif(APPLE)  # APPLE but not IOS
    set(_ep_template_name "ep_dependencies_config_macOS")
elseif(UNIX AND NOT APPLE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
        set(_ep_template_name "ep_dependencies_config_linux_x64")
    else()
        message(FATAL_ERROR "Unsupported system processor: ${CMAKE_SYSTEM_PROCESSOR}")
    endif()
else()
    message(FATAL_ERROR "Unsupported platform ${CMAKE_SYSTEM_PROCESSOR}")
endif()
set(_ep_template "${CMAKE_SOURCE_DIR}/data/${_ep_template_name}.json.in")
set(EP_DEPENDENCIES_CONFIG_SCHEMA_JSON
    "${CMAKE_SOURCE_DIR}/data/EPDependenciesConfigSchema.json")

# Per-EP prefix defaults — platform-aware. Only set the variables the
# selected template actually references; unused variables don't affect
# `configure_file(... @ONLY)`.
if(WIN32)
    # WindowsML: universal DLLs + NV-only extras share one folder, versioned
    # by the MML_WINML_VERSION pin. For offline / local-dev work, override
    # with -DMLPERF_WINML_PREFIX=file://IHV/WindowsML so the dep manager uses
    # the IHV's own POST_BUILD staging dir instead.
    if(NOT DEFINED MLPERF_WINML_PREFIX)
        set(MLPERF_WINML_PREFIX
            "https://client.mlcommons-storage.org/deps/2.0/binaries/WindowsML/${_arch_dir}")
    endif()
    if(NOT DEFINED MLPERF_NATIVE_OPENVINO_PREFIX)
        set(MLPERF_NATIVE_OPENVINO_PREFIX
            "https://client.mlcommons-storage.org/deps/2.0/binaries/NativeOpenVINO/Windows")
    endif()
    if(NOT DEFINED MLPERF_ORTGENAI_PREFIX)
        set(MLPERF_ORTGENAI_PREFIX
            "https://client.mlcommons-storage.org/deps/2.0/binaries/OrtGenAI/${_arch_dir}")
    endif()
    if(NOT DEFINED MLPERF_ORTGENAI_RYZENAI_PREFIX)
        set(MLPERF_ORTGENAI_RYZENAI_PREFIX
            "https://client.mlcommons-storage.org/deps/2.0/binaries/OGA-RyzenAI/${_arch_dir}")
    endif()
    if(NOT DEFINED MLPERF_VULKAN_PREFIX)
        set(MLPERF_VULKAN_PREFIX
            "https://client.mlcommons-storage.org/deps/2.0/binaries/GGML/Vulkan/Windows/${_arch_dir}")
    endif()
    if(NOT DEFINED MLPERF_CUDA_PREFIX)
        set(MLPERF_CUDA_PREFIX
            "https://client.mlcommons-storage.org/deps/2.0/binaries/GGML/CUDA/Windows/x64")
    endif()
    if(NOT DEFINED MLPERF_ROCM_PREFIX)
        set(MLPERF_ROCM_PREFIX
            "https://client.mlcommons-storage.org/deps/2.0/binaries/GGML/ROCm")
    endif()
    if(NOT DEFINED MLPERF_NATIVE_QNN_PREFIX)
        set(MLPERF_NATIVE_QNN_PREFIX
            "https://client.mlcommons-storage.org/deps/2.0/binaries/NativeQNN/${_arch_dir}")
    endif()
    if(NOT DEFINED MLPERF_DIFFUSERS_PREFIX)
        set(MLPERF_DIFFUSERS_PREFIX
            "https://client.mlcommons-storage.org/deps/2.0/binaries/Diffusers/Windows/x64")
    endif()
elseif(APPLE AND NOT IOS)
    if(NOT DEFINED MLPERF_MLX_PREFIX)
        set(MLPERF_MLX_PREFIX
            "https://client.mlcommons-storage.org/deps/2.0/binaries/MLX")
    endif()
    if(NOT DEFINED MLPERF_VULKAN_PREFIX)
        set(MLPERF_VULKAN_PREFIX
            "https://client.mlcommons-storage.org/deps/2.0/binaries/GGML/Vulkan/MacOS")
    endif()
    if(NOT DEFINED MLPERF_DIFFUSERS_PREFIX)
        set(MLPERF_DIFFUSERS_PREFIX
            "https://client.mlcommons-storage.org/deps/2.0/binaries/Diffusers/macOS")
    endif()
elseif(UNIX AND NOT APPLE)
    if(NOT DEFINED MLPERF_NATIVE_OPENVINO_PREFIX)
        set(MLPERF_NATIVE_OPENVINO_PREFIX
            "https://client.mlcommons-storage.org/deps/2.0/binaries/NativeOpenVINO/Linux")
    endif()
    # Default to the in-repo build deps. Override with -DMLPERF_ONNXRT_PREFIX=...
    # and -DMLPERF_ORTGENAI_PREFIX=... to point elsewhere.
    if(NOT DEFINED MLPERF_ONNXRT_PREFIX)
        set(MLPERF_ONNXRT_PREFIX
            "file://${CMAKE_SOURCE_DIR}/deps/ONNXRuntime/Linux/x64/lib")
    endif()
    if(NOT DEFINED MLPERF_ORTGENAI_PREFIX)
        set(MLPERF_ORTGENAI_PREFIX
            "file://${CMAKE_SOURCE_DIR}/deps/ONNXRuntimeGenAi/Linux/x64/lib")
    endif()
    if(NOT DEFINED MLPERF_VULKAN_PREFIX)
        set(MLPERF_VULKAN_PREFIX
            "https://client.mlcommons-storage.org/deps/2.0/binaries/GGML/Vulkan/Linux/x64")
    endif()
    if(NOT DEFINED MLPERF_CUDA_PREFIX)
        set(MLPERF_CUDA_PREFIX
            "https://client.mlcommons-storage.org/deps/2.0/binaries/GGML/CUDA/linux/x64")
    endif()
endif()
# --- Diffusers EP: per-vendor Files JSON for the EP-deps template ---
# Central list — adding a new vendor (INTEL, AMD, …) = append here, the
# helper + loop below pick it up. Same list also drives the compile defs
# block at the bottom of this CMakeLists (kept in sync via the variable).
set(_DIFFUSERS_KNOWN_VENDORS NVIDIA APPLE AMD)

# Compose one JSON Files-array entry for a Diffusers vendor.
# Single-vendor build = flat single entry, no Condition.
# Multi-vendor build = per-vendor entry, slash-prefixed Name (lands in
# <deps>/Diffusers/<vendor>/ subdir on disk after download + unpack) +
# Condition gating the download to the active config's vendor.
function(_diffusers_vendor_files_entry vendor_upper output_var)
    string(TOLOWER "${vendor_upper}" _vendor_lower)
    # NOTE: substitute ${MLPERF_DIFFUSERS_PREFIX} now (CMake-time) — the
    # downstream configure_file() does a single @VAR@ pass and won't
    # re-expand whatever lands inside @MLPERF_DIFFUSERS_FILES_JSON@.
    set(${output_var}
"{
          \"Name\": \"${_vendor_lower}/python_env.zip\",
          \"Path\": \"${MLPERF_DIFFUSERS_PREFIX}/${_vendor_lower}/python_env.zip\",
          \"Condition\": { \"device_vendor\": \"${vendor_upper}\" }
        }"
        PARENT_SCOPE)
endfunction()

# Build the enabled-vendor list (same pattern as the compile-defs block
# below — keep them aligned by iterating _DIFFUSERS_KNOWN_VENDORS).
set(_diffusers_enabled_for_deps "")
foreach(_v IN LISTS _DIFFUSERS_KNOWN_VENDORS)
    if(MLPERF_IHV_DIFFUSERS_${_v})
        list(APPEND _diffusers_enabled_for_deps ${_v})
    endif()
endforeach()
list(LENGTH _diffusers_enabled_for_deps _diffusers_enabled_for_deps_count)

if(_diffusers_enabled_for_deps_count GREATER 1)
    set(_entries "")
    foreach(_v IN LISTS _diffusers_enabled_for_deps)
        _diffusers_vendor_files_entry(${_v} _e)
        list(APPEND _entries "${_e}")
    endforeach()
    string(JOIN ",\n        " MLPERF_DIFFUSERS_FILES_JSON ${_entries})
elseif(_diffusers_enabled_for_deps_count EQUAL 1)
    # Single-vendor: remote path keeps a <vendor>/ subdir so a 2nd IHV is
    # additive; local Name stays flat so the unpacked tree lands at
    # <deps>/Diffusers/python/ next to the sub-EP DLL.
    list(GET _diffusers_enabled_for_deps 0 _only_vendor)
    string(TOLOWER "${_only_vendor}" _only_vendor_lc)
    set(MLPERF_DIFFUSERS_FILES_JSON
        "{ \"Name\": \"python_env.zip\", \"Path\": \"${MLPERF_DIFFUSERS_PREFIX}/${_only_vendor_lc}/python_env.zip\" }")
else()
    # No Diffusers vendor on this platform (e.g. Linux).
    set(MLPERF_DIFFUSERS_FILES_JSON "")
endif()

# Generate. Platforms with a .json.in template (Windows, macOS, Linux) go
# through configure_file. Platforms whose deps are wholly framework-bundled
# (iOS) keep a static .json — no substitution needed.
if(EXISTS "${_ep_template}")
    set(EP_DEPENDENCIES_CONFIG_JSON
        "${CMAKE_BINARY_DIR}/data/${_ep_template_name}.json")
    configure_file("${_ep_template}" "${EP_DEPENDENCIES_CONFIG_JSON}" @ONLY)
    message(STATUS "Generated EP dependencies config: ${EP_DEPENDENCIES_CONFIG_JSON}")
    foreach(_v IN ITEMS
            MLPERF_WINML_PREFIX
            MLPERF_NATIVE_OPENVINO_PREFIX
            MLPERF_ORTGENAI_PREFIX
            MLPERF_ORTGENAI_RYZENAI_PREFIX
            MLPERF_VULKAN_PREFIX
            MLPERF_CUDA_PREFIX
            MLPERF_ROCM_PREFIX
            MLPERF_NATIVE_QNN_PREFIX
            MLPERF_MLX_PREFIX
            MLPERF_DIFFUSERS_PREFIX
            MLPERF_ONNXRT_PREFIX)
        if(DEFINED ${_v})
            message(STATUS "  ${_v} = ${${_v}}")
        endif()
    endforeach()
else()
    set(EP_DEPENDENCIES_CONFIG_JSON
        "${CMAKE_SOURCE_DIR}/data/${_ep_template_name}.json")
    if(NOT EXISTS "${EP_DEPENDENCIES_CONFIG_JSON}")
        message(FATAL_ERROR
            "Neither template nor static EP dependencies config found: "
            "expected ${_ep_template} or ${EP_DEPENDENCIES_CONFIG_JSON}")
    endif()
    message(STATUS "Using static EP dependencies config: ${EP_DEPENDENCIES_CONFIG_JSON}")
endif()

if(WIN32)
	set(EP_IHVS_NATIVE_OPENVINO_LIB "${CMAKE_BINARY_DIR}/IHV/NativeOpenVINO/bin/$<CONFIG>/IHV_NativeOpenVINO.dll")
else() # Linux
	set(EP_IHVS_NATIVE_OPENVINO_LIB "${CMAKE_BINARY_DIR}/IHV/NativeOpenVINO/Build/libIHV_NativeOpenVINO.so")
endif()

set(EP_IHVS_ORT_GENAI_LIB "${CMAKE_BINARY_DIR}/IHV/OrtGenAI/bin/$<CONFIG>/IHV_OrtGenAI.dll")
set(EP_IHVS_ORT_GENAI_RYZENAI_LIB "${CMAKE_BINARY_DIR}/IHV/OrtGenAI-RyzenAI/bin/$<CONFIG>/IHV_OrtGenAI_RyzenAI.dll")
set(EP_IHVS_WIN_ML_LIB "${CMAKE_BINARY_DIR}/IHV/WindowsML/bin/$<CONFIG>/IHV_WindowsML.dll")
if (IOS)
    set(EP_IHVS_GGML_LIB "${CMAKE_BINARY_DIR}/IHV/GGML/bin/$<CONFIG>/IHV_GGML_EPs.framework")
    set(EP_IHVS_GGML_METAL_LIB "${CMAKE_BINARY_DIR}/IHV/GGML/Metal/bin/$<CONFIG>/llama_cpp_Metal.framework")
else()
    set(EP_IHVS_GGML_LIB "${CMAKE_BINARY_DIR}/IHV/GGML/bin/$<CONFIG>/${CMAKE_SHARED_LIBRARY_PREFIX}IHV_GGML_EPs${CMAKE_SHARED_LIBRARY_SUFFIX}")
    set(EP_IHVS_GGML_METAL_LIB "${CMAKE_BINARY_DIR}/IHV/GGML/Metal/bin/$<CONFIG>/${CMAKE_SHARED_LIBRARY_PREFIX}llama_cpp_Metal${CMAKE_SHARED_LIBRARY_SUFFIX}")
endif()
set(EP_IHVS_GGML_VULKAN_LIB "${CMAKE_BINARY_DIR}/IHV/GGML/Vulkan/bin/$<CONFIG>/${CMAKE_SHARED_LIBRARY_PREFIX}llama_cpp_Vulkan${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(EP_IHVS_GGML_CUDA_LIB "${CMAKE_BINARY_DIR}/IHV/GGML/CUDA/bin/$<CONFIG>/${CMAKE_SHARED_LIBRARY_PREFIX}llama_cpp_CUDA${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(EP_IHVS_GGML_ROCM_LIB "${CMAKE_BINARY_DIR}/IHV/GGML/ROCm/bin/$<CONFIG>/${CMAKE_SHARED_LIBRARY_PREFIX}llama_cpp_ROCm${CMAKE_SHARED_LIBRARY_SUFFIX}")
set_property(GLOBAL PROPERTY  EP_IHVS_GGML_LIB ${EP_IHVS_GGML_LIB})
set_property(GLOBAL PROPERTY  EP_IHVS_GGML_METAL_LIB ${EP_IHVS_GGML_METAL_LIB})
set(EP_IHVS_NATIVE_QNN_LIB "${CMAKE_BINARY_DIR}/IHV/NativeQNN/bin/$<CONFIG>/${CMAKE_SHARED_LIBRARY_PREFIX}IHV_NativeQNN${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(EP_IHVS_DIFFUSERS_LIB "${CMAKE_BINARY_DIR}/IHV/Diffusers/bin/$<CONFIG>/${CMAKE_SHARED_LIBRARY_PREFIX}IHV_Diffusers${CMAKE_SHARED_LIBRARY_SUFFIX}")
set_property(GLOBAL PROPERTY  EP_IHVS_DIFFUSERS_LIB ${EP_IHVS_DIFFUSERS_LIB})
# In single-vendor mode IHV_Diffusers.dll IS the sub-EP renamed (today's
# build artifact). In multi-vendor mode IHV_Diffusers.dll is the
# dispatcher and the sub-EP is built separately as Diffusers_Vendor.dll
# (one binary, copied to each <vendor>/ subdir at install with a
# vendor-suffixed name). Asset embedding only needs the extra blob in
# multi-vendor mode.
set(EP_IHVS_DIFFUSERS_VENDOR_LIB "${CMAKE_BINARY_DIR}/IHV/Diffusers/bin/$<CONFIG>/${CMAKE_SHARED_LIBRARY_PREFIX}Diffusers_Vendor${CMAKE_SHARED_LIBRARY_SUFFIX}")
set_property(GLOBAL PROPERTY  EP_IHVS_DIFFUSERS_VENDOR_LIB ${EP_IHVS_DIFFUSERS_VENDOR_LIB})


# tokenizer shared library path
if(IOS)
    set(TOKENIZER_LIB "${CMAKE_BINARY_DIR}/tokenizer/bin/$<CONFIG>/tokenizer.framework")
else()
    set(TOKENIZER_LIB "${CMAKE_BINARY_DIR}/tokenizer/bin/$<CONFIG>/${CMAKE_SHARED_LIBRARY_PREFIX}tokenizer${CMAKE_SHARED_LIBRARY_SUFFIX}")
endif()

# imageio shared library path
if(IOS)
    set(IMAGEIO_LIB "${CMAKE_BINARY_DIR}/imageio/bin/$<CONFIG>/imageio.framework")
else()
    set(IMAGEIO_LIB "${CMAKE_BINARY_DIR}/imageio/bin/$<CONFIG>/${CMAKE_SHARED_LIBRARY_PREFIX}imageio${CMAKE_SHARED_LIBRARY_SUFFIX}")
endif()

include(deps_packer/pack_as_binary.cmake)
set (IHV_FILES_TO_PUBLISH "")

set(ASSETS_LIST 
	${LOG4CXXCONFIG_XML}
	${CONFIGSCHEMA_JSON}
	${LLM_INPUT_SCHEMA_JSON}
	${IMAGE_INPUT_SCHEMA_JSON}
	${OUTPUT_RESULTS_SCHEMA_JSON}
	${DATA_VERIFICATION_JSON}
	${DATA_VERIFICATION_SCHEMA_JSON}
	${EP_DEPENDENCIES_CONFIG_JSON}
	${EP_DEPENDENCIES_CONFIG_SCHEMA_JSON}
)

list(APPEND ASSETS_LIST ${CONFIG_VERIFICATION_FILES})

if(MLPERF_PACK_VENDORS_DEFAULT)
    list(APPEND ASSETS_LIST ${VENDORS_DEFAULT_ZIP})
endif()

include(minizip.cmake)

# Build minizip-ng (and its fetched deps) before the long IHV builds so
# fetch failures surface early instead of hours into the build.
foreach(IHV_TARGET IN ITEMS ihv_native_openvino ihv_native_qnn ihv_ort_genai
        ihv_ort_genai_ryzenai ihv_ggml ihv_mlx_source ihv_win_ml ihv_diffusers)
    if(TARGET ${IHV_TARGET})
        add_dependencies(${IHV_TARGET} minizip-ng)
    endif()
endforeach()

# tokenizer and imageio are always packed as assets (not conditional like IHVs)
list(APPEND ASSETS_LIST ${TOKENIZER_LIB})
list(APPEND ASSETS_LIST ${IMAGEIO_LIB})

# Place to add IHV libs if needed to be unpacked/included by the single executable build
if(MLPERF_IHV_MLX)
    if (IOS)
	    set(EP_IHVS_MLX_LIB "${CMAKE_BINARY_DIR}/IHV/MLX/bin/$<CONFIG>/IHV_MLX.framework")
		set(MLX_METALLIB "${CMAKE_BINARY_DIR}/IHV/MLX/build/Build/Products/${CMAKE_BUILD_TYPE}-iphoneos/mlx-swift_Cmlx.bundle/default.metallib")
	else()
        set(EP_IHVS_MLX_LIB "${CMAKE_BINARY_DIR}/IHV/MLX/bin/$<CONFIG>/${CMAKE_SHARED_LIBRARY_PREFIX}IHV_MLX${CMAKE_SHARED_LIBRARY_SUFFIX}")
		set(MLX_METALLIB "${CMAKE_BINARY_DIR}/IHV/MLX/build/Build/Products/${CMAKE_BUILD_TYPE}/mlx-swift_Cmlx.bundle/Contents/Resources/default.metallib")
	endif()
	set_property(GLOBAL PROPERTY  EP_IHVS_MLX_LIB ${EP_IHVS_MLX_LIB})
	set_property(GLOBAL PROPERTY  MLX_METALLIB ${MLX_METALLIB})
	
	list(APPEND ASSETS_LIST ${EP_IHVS_MLX_LIB})
endif()

if(MLPERF_IHV_NATIVE_OPENVINO)
	list(APPEND ASSETS_LIST ${EP_IHVS_NATIVE_OPENVINO_LIB})
endif()

if(MLPERF_IHV_ORT_GENAI)
	list(APPEND ASSETS_LIST ${EP_IHVS_ORT_GENAI_LIB})
endif()

if(MLPERF_IHV_ORT_GENAI_RYZENAI)
	list(APPEND ASSETS_LIST ${EP_IHVS_ORT_GENAI_RYZENAI_LIB})
endif()

if(MLPERF_IHV_WINDOWSML)
	list(APPEND ASSETS_LIST ${EP_IHVS_WIN_ML_LIB})
endif()

if(MLPERF_IHV_GGML)
	list(APPEND ASSETS_LIST ${EP_IHVS_GGML_LIB})
	if(MLPERF_IHV_GGML_VULKAN)
		list(APPEND ASSETS_LIST ${EP_IHVS_GGML_VULKAN_LIB})
	endif()
	if(MLPERF_IHV_GGML_CUDA)
		list(APPEND ASSETS_LIST ${EP_IHVS_GGML_CUDA_LIB})
	endif()
	if(MLPERF_IHV_GGML_METAL)
		list(APPEND ASSETS_LIST ${EP_IHVS_GGML_METAL_LIB})
	endif()
	if(MLPERF_IHV_GGML_ROCM)
		list(APPEND ASSETS_LIST ${EP_IHVS_GGML_ROCM_LIB})
	endif()
endif()

if(MLPERF_IHV_NATIVE_QNN)
	list(APPEND ASSETS_LIST ${EP_IHVS_NATIVE_QNN_LIB})
endif()

if(_diffusers_enabled_for_deps_count GREATER 1)
	set(_diffusers_dispatcher_on TRUE)
else()
	set(_diffusers_dispatcher_on FALSE)
endif()

if(MLPERF_IHV_DIFFUSERS)
	list(APPEND ASSETS_LIST ${EP_IHVS_DIFFUSERS_LIB})
	if(_diffusers_dispatcher_on)
		list(APPEND ASSETS_LIST ${EP_IHVS_DIFFUSERS_VENDOR_LIB})
	endif()
endif()

pack_as_binary(assets
    SOURCES
	${ASSETS_LIST}
)
add_dependencies(assets ${CONFIG_VERIFICATION_TARGETS})

# Ensure shared libs are built before DepsPacker embeds them into assets
add_dependencies(assets tokenizer)
add_dependencies(assets imageio)
if(MLPERF_IHV_NATIVE_OPENVINO)
	add_dependencies(assets ihv_native_openvino)
endif()
if(MLPERF_IHV_ORT_GENAI)
	add_dependencies(assets ihv_ort_genai)
endif()
if(MLPERF_IHV_ORT_GENAI_RYZENAI)
	add_dependencies(assets ihv_ort_genai_ryzenai)
endif()
if(MLPERF_IHV_WINDOWSML)
	add_dependencies(assets ihv_win_ml)
endif()
if(MLPERF_IHV_GGML)
	add_dependencies(assets ihv_ggml)
endif()
if(MLPERF_IHV_NATIVE_QNN)
	add_dependencies(assets ihv_native_qnn)
endif()
if(MLPERF_IHV_MLX)
	add_dependencies(assets ihv_mlx_source)
endif()
if(MLPERF_IHV_DIFFUSERS)
	add_dependencies(assets ihv_diffusers)
endif()

# Create a static library from the source files
add_library(${PROJECT_NAME} STATIC ${SOURCES} ${HEADERS})
add_dependencies(${PROJECT_NAME} tokenizer)
add_dependencies(${PROJECT_NAME} imageio)

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_COMMAND}")

target_compile_definitions(${PROJECT_NAME}
    PRIVATE
	LOG4CXX_STATIC
)

target_compile_options(${PROJECT_NAME}
	PRIVATE
	$<$<CXX_COMPILER_ID:MSVC>:/w45038 /w45205 /w44061 /EHsc>
	$<$<CXX_COMPILER_ID:Clang>:-Wreorder -fexceptions>
)

find_package(Log4Cxx REQUIRED)

# Include Log4Cxx Runtime headers
target_include_directories(${PROJECT_NAME} PRIVATE ${LOG4CXX_INCLUDE_DIR})
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/common")

set(LOG4CXX_LIBRARY ${LOG4CXX_LIB_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}log4cxx${CMAKE_STATIC_LIBRARY_SUFFIX})

# Include httplib Runtime headers
target_include_directories(${PROJECT_NAME} PRIVATE ${HTTPLIB_INCLUDE_DIR})

if(APPLE AND NOT IOS)
option(USE_OPENCL "Use Open Compute Language (OpenCL) for GPU information retrieval."  ON)
endif()
if(USE_OPENCL)
	target_link_libraries(${PROJECT_NAME} "-framework OpenCL")
	target_compile_definitions(${PROJECT_NAME} PUBLIC USE_OPENCL)
endif()
 
include(${CMAKE_CURRENT_SOURCE_DIR}/common/dylib.cmake)
target_link_libraries(${PROJECT_NAME} dylib)

add_dependencies(${PROJECT_NAME} IHVs)

add_dependencies(${PROJECT_NAME} assets)
target_link_libraries(${PROJECT_NAME} assets)

add_dependencies(${PROJECT_NAME} minizip-ng)
target_include_directories(${PROJECT_NAME} PRIVATE ${MINIZIP_INCLUDE_DIR})


target_compile_definitions(${PROJECT_NAME} PUBLIC "WITH_IHV_NATIVE_OPENVINO=$<BOOL:${MLPERF_IHV_NATIVE_OPENVINO}>")
target_compile_definitions(${PROJECT_NAME} PUBLIC "WITH_IHV_ORT_GENAI=$<BOOL:${MLPERF_IHV_ORT_GENAI}>")
target_compile_definitions(${PROJECT_NAME} PUBLIC "WITH_IHV_ORT_GENAI_RYZENAI=$<BOOL:${MLPERF_IHV_ORT_GENAI_RYZENAI}>")
target_compile_definitions(${PROJECT_NAME} PUBLIC "WITH_IHV_WIN_ML=$<BOOL:${MLPERF_IHV_WINDOWSML}>")
target_compile_definitions(${PROJECT_NAME} PUBLIC "WITH_IHV_GGML=$<BOOL:${MLPERF_IHV_GGML}>")
target_compile_definitions(${PROJECT_NAME} PUBLIC "WITH_IHV_GGML_VULKAN=$<BOOL:${MLPERF_IHV_GGML_VULKAN}>")
target_compile_definitions(${PROJECT_NAME} PUBLIC "WITH_IHV_GGML_CUDA=$<BOOL:${MLPERF_IHV_GGML_CUDA}>")
target_compile_definitions(${PROJECT_NAME} PUBLIC "WITH_IHV_GGML_METAL=$<BOOL:${MLPERF_IHV_GGML_METAL}>")
target_compile_definitions(${PROJECT_NAME} PUBLIC "WITH_IHV_GGML_ROCM=$<BOOL:${MLPERF_IHV_GGML_ROCM}>")
target_compile_definitions(${PROJECT_NAME} PUBLIC "WITH_IHV_MLX=$<BOOL:${MLPERF_IHV_MLX}>")
target_compile_definitions(${PROJECT_NAME} PUBLIC "WITH_IHV_NATIVE_QNN=$<BOOL:${MLPERF_IHV_NATIVE_QNN}>")
target_compile_definitions(${PROJECT_NAME} PUBLIC "WITH_IHV_DIFFUSERS=$<BOOL:${MLPERF_IHV_DIFFUSERS}>")

# Emit WITH_IHV_DIFFUSERS_<VENDOR>=0/1 per known vendor + collect the
# enabled set so we can compute "dispatcher needed?" from the count.
set(_diffusers_enabled_vendors "")
foreach(_v IN LISTS _DIFFUSERS_KNOWN_VENDORS)
    target_compile_definitions(${PROJECT_NAME} PUBLIC
        "WITH_IHV_DIFFUSERS_${_v}=$<BOOL:${MLPERF_IHV_DIFFUSERS_${_v}}>")
    if(MLPERF_IHV_DIFFUSERS_${_v})
        list(APPEND _diffusers_enabled_vendors ${_v})
    endif()
endforeach()
list(LENGTH _diffusers_enabled_vendors _diffusers_vendor_count)

# Dispatcher is built when 2+ vendors are enabled. Mirrors the
# per-vendor CMake computation done in the inner Diffusers/src
# CMakeLists. unpacker.cpp / preparation_stage.cpp gate on this to
# decide whether to unpack a vendor subdir or load the EP DLL flat.
if(_diffusers_vendor_count GREATER 1)
    set(_diffusers_dispatcher_on TRUE)
else()
    set(_diffusers_dispatcher_on FALSE)
endif()
target_compile_definitions(${PROJECT_NAME} PUBLIC
    "WITH_IHV_DIFFUSERS_DISPATCHER=$<BOOL:${_diffusers_dispatcher_on}>")
target_compile_definitions(${PROJECT_NAME} PUBLIC "IHV_SUBPROCESS=$<BOOL:${MLPERF_SUBPROCESS_IHV_ISOLATION}>")
target_compile_definitions(${PROJECT_NAME} PUBLIC "VENDORS_DEFAULT_PACKED=$<BOOL:${MLPERF_PACK_VENDORS_DEFAULT}>")

option(MLPERF_SKIP_DOWNLOAD_FOR_UNSUPPORTED_EPS "Ignores model overrides and ep dependencies download for EP not supported" ON)
target_compile_definitions(${PROJECT_NAME} PUBLIC "IGNORE_FILES_FROM_DISABLED_EPS=$<BOOL:${MLPERF_SKIP_DOWNLOAD_FOR_UNSUPPORTED_EPS}>")

# URL of the bundled portable Python zip used by agentic scenarios.
set(_portable_python_base_url "https://client.mlcommons-storage.org/deps/2.0/scenario_files/agentic/python")
set(_portable_python_version "3.13.13")
if(WIN32)
    if(CMAKE_GENERATOR_PLATFORM MATCHES "x64")
        set(_portable_python_url "${_portable_python_base_url}/python-${_portable_python_version}-win-x64.zip")
    else()
        set(_portable_python_url "${_portable_python_base_url}/python-${_portable_python_version}-win-arm64.zip")
    endif()
elseif(APPLE AND NOT IOS)
    set(_portable_python_url "${_portable_python_base_url}/python-${_portable_python_version}-macos.zip")
elseif(UNIX AND NOT APPLE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
        set(_portable_python_url "${_portable_python_base_url}/python-${_portable_python_version}-linux-x64.zip")
    else()
        set(_portable_python_url "")
    endif()
else()
    set(_portable_python_url "")
endif()
set(MLPERF_PORTABLE_PYTHON_URL "${_portable_python_url}"
    CACHE STRING "Full URL of the bundled portable Python zip for agentic scenarios")
target_compile_definitions(${PROJECT_NAME} PRIVATE "MLPERF_PORTABLE_PYTHON_URL=\"${MLPERF_PORTABLE_PYTHON_URL}\"")

if(WIN32)
set(EXTRA_LIBRARIES Ws2_32.lib Rpcrt4.lib wbemuuid.lib)
elseif(APPLE)
    find_package(Iconv REQUIRED)
    set(EXTRA_LIBRARIES "-framework CoreFoundation" "-framework Foundation" ${Iconv_LIBRARIES})

    if (IOS)
		list(APPEND EXTRA_LIBRARIES "-framework Metal")
    endif()
else() # Linux
	set(EXTRA_LIBRARIES dl rt pthread)
	set(APR_LIBRARIES ${LOG4CXX_LIB_DIR}/libaprutil-1.a ${LOG4CXX_LIB_DIR}/libapriconv-1.a ${LOG4CXX_LIB_DIR}/libapr-1.a ${LOG4CXX_LIB_DIR}/libexpat.a)
endif()


# Force enable exceptions for extern C functions
target_compile_options(${PROJECT_NAME} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/EHsc>)
target_compile_options(${PROJECT_NAME} PRIVATE $<$<CXX_COMPILER_ID:Clang>:-fexceptions>)
target_compile_options(${PROJECT_NAME} PRIVATE $<$<CXX_COMPILER_ID:GNU>:-fexceptions>)
target_compile_options(${PROJECT_NAME} PRIVATE $<$<CXX_COMPILER_ID:AppleClang>:-fexceptions>)
target_compile_options(${PROJECT_NAME} PRIVATE $<$<CXX_COMPILER_ID:Intel>:-fexceptions>)

target_link_libraries(${PROJECT_NAME} ${HTTPLIB_LIBRARY} ${LOG4CXX_LIBRARY} ${MINIZIP_LINK_LIBS} ${EXTRA_LIBRARIES} ${APR_LIBRARIES} nlohmann_json::nlohmann_json nlohmann_json_schema_validator::validator common_cil)

