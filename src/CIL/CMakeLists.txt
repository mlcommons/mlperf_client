cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0135 NEW)

# Set the project name and version
project(CIL VERSION 1.0)

set(CMAKE_INCLUDE_CURRENT_DIR ON)


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
endif()

set(DEPS_PACKER_RUNTIME_OUTPUT_DIRECTORY ${DEPS_PACKER_BUILD_DIR}/${DEPS_PACKER_GENERATOR_PLATFORM})

# Define the external project with custom commands using CMake variables
ExternalProject_Add(DepsPacker
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps_packer
    BINARY_DIR ${DEPS_PACKER_BUILD_DIR}
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" ${DEPS_PACKER_GENERATOR_PLATFORM_COMMAND} -S ${CMAKE_CURRENT_SOURCE_DIR}/deps_packer -B ${DEPS_PACKER_BUILD_DIR} -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=${DEPS_PACKER_RUNTIME_OUTPUT_DIRECTORY} -DCMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR}
    BUILD_COMMAND ${CMAKE_COMMAND} --build ${DEPS_PACKER_BUILD_DIR} --config $<CONFIG>
    INSTALL_COMMAND ""
)

add_subdirectory(common)

add_subdirectory(IHV)

# Force use of the release runtime library in all configurations
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:>")
endif()

# Define the source files for the library
file(GLOB_RECURSE SOURCES "*.cpp")

# Define the header files for the library
file(GLOB_RECURSE HEADERS "*.h")

if(APPLE)
	list(FILTER HEADERS EXCLUDE REGEX "system_info_provider_windows.h")
	list(FILTER SOURCES EXCLUDE REGEX "system_info_provider_windows.cpp")
else()
	list(FILTER HEADERS EXCLUDE REGEX "system_info_provider_macos.h")
	list(FILTER SOURCES EXCLUDE REGEX "system_info_provider_macos.cpp")
endif()

list(FILTER SOURCES EXCLUDE REGEX "deps_packer/")
list(FILTER HEADERS EXCLUDE REGEX "deps_packer/")

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
if(NOT MLPERF_IHV_ORT_GENAI)
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

list(FILTER SOURCES EXCLUDE REGEX "common/")
list(FILTER HEADERS EXCLUDE REGEX "common/")

# Define source groups
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Source files" FILES ${SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Header files" FILES ${HEADERS})

set(LOG4CXXCONFIG_XML "${CMAKE_SOURCE_DIR}/data/Log4CxxConfig.xml")
set(CONFIGSCHEMA_JSON "${CMAKE_SOURCE_DIR}/data/ConfigSchema.json")
set(LLAMA2_INPUT_SCHEMA_JSON "${CMAKE_SOURCE_DIR}/data/Llama2InputSchema.json")
set(OUTPUT_RESULTS_SCHEMA_JSON "${CMAKE_SOURCE_DIR}/data/OutputResultsSchema.json")
set(DATA_VERIFICATION_JSON "${CMAKE_SOURCE_DIR}/data/data_verification.json")
set(DATA_VERIFICATION_SCHEMA_JSON "${CMAKE_SOURCE_DIR}/data/DataVerificationSchema.json")

set(VENDOR_DEFAULTS_CONFIGS_DIR "${CMAKE_SOURCE_DIR}/data/configs/vendors_default")
set(CONFIG_VERIFICATION_JSON "${CMAKE_BINARY_DIR}/config_verification.json")
file(GLOB_RECURSE  VENDOR_DEFAULTS_CONFIGS CONFIGURE_DEPENDS  "${VENDOR_DEFAULTS_CONFIGS_DIR}/*.json")
set(TEMP_CONFIG_VERIFICATION_JSON "${CMAKE_BINARY_DIR}/temp_config.json")

message(STATUS "Computing hashes for vendor default configs")
add_custom_target(
	compute_hashes
	ALL
	COMMAND ${CMAKE_COMMAND} -DFILES_TO_HASH="${VENDOR_DEFAULTS_CONFIGS}" -DOUTPUT_FILE="${CONFIG_VERIFICATION_JSON}" -P "${CMAKE_CURRENT_SOURCE_DIR}/compute_hashes.cmake"
)


# Use separatw config file for the EP dependencies depending on platform
# Windows x64, Windows ARM64, MacOS
if(WIN32)
# Check if x64
    if(CMAKE_GENERATOR_PLATFORM MATCHES "x64")
        set(EP_DEPENDENCIES_CONFIG_JSON "${CMAKE_SOURCE_DIR}/data/ep_dependencies_config_windows_x64.json")
    else()
		set(EP_DEPENDENCIES_CONFIG_JSON "${CMAKE_SOURCE_DIR}/data/ep_dependencies_config_windows_ARM.json")
	endif()
elseif(APPLE)
    set(EP_DEPENDENCIES_CONFIG_JSON "${CMAKE_SOURCE_DIR}/data/ep_dependencies_config_macOS.json")
endif()
set(EP_DEPENDENCIES_CONFIG_SCHEMA_JSON "${CMAKE_SOURCE_DIR}/data/EPDependenciesConfigSchema.json")
set(EP_IHVS_NATIVE_OPENVINO_LIB "${CMAKE_BINARY_DIR}/IHV/NativeOpenVINO/bin/$<CONFIG>/IHV_NativeOpenVINO.dll")
set(EP_IHVS_ORT_GENAI_LIB "${CMAKE_BINARY_DIR}/IHV/OrtGenAI/bin/$<CONFIG>/IHV_OrtGenAI.dll")

include(deps_packer/pack_as_binary.cmake)
set (IHV_FILES_TO_SIGN "")

set(ASSETS_LIST 
	${LOG4CXXCONFIG_XML}
	${CONFIGSCHEMA_JSON}
	${LLAMA2_INPUT_SCHEMA_JSON}
	${OUTPUT_RESULTS_SCHEMA_JSON}
	${DATA_VERIFICATION_JSON}
	${DATA_VERIFICATION_SCHEMA_JSON}
	${EP_DEPENDENCIES_CONFIG_JSON}
	${EP_DEPENDENCIES_CONFIG_SCHEMA_JSON}
	${CONFIG_VERIFICATION_JSON}
)

include(minizip.cmake)

# Place to add IHV libs if needed to be unpacked/included by the single executable build
if(MLPERF_IHV_NATIVE_OPENVINO)
	if(NOT MLPERF_ENABLE_COLLECT_DEPENDENCIES)
		list(APPEND ASSETS_LIST ${EP_IHVS_NATIVE_OPENVINO_LIB})
	#else()
	#	list(APPEND IHV_FILES_TO_SIGN ${EP_IHVS_NATIVE_OPENVINO_LIB})
	endif()
endif()

if(MLPERF_IHV_ORT_GENAI)
	if(NOT MLPERF_ENABLE_COLLECT_DEPENDENCIES)
		list(APPEND ASSETS_LIST ${EP_IHVS_ORT_GENAI_LIB})
	endif()
endif()

if(NOT MLPERF_ENABLE_COLLECT_DEPENDENCIES)
pack_as_binary(assets
    SOURCES
	${ASSETS_LIST}
)
add_dependencies(assets compute_hashes)
endif()

set_property(GLOBAL PROPERTY IHV_FILES_TO_SIGN ${IHV_FILES_TO_SIGN})


# Place to make IHV libs dependent, so they are build before Assets
if(NOT MLPERF_ENABLE_COLLECT_DEPENDENCIES)
	if(MLPERF_IHV_NATIVE_OPENVINO)
		add_dependencies(assets ihv_native_openvino)
	endif()
	if(MLPERF_IHV_ORT_GENAI)
		add_dependencies(assets ihv_ort_genai)
	endif()
endif()

# Create a static library from the source files
add_library(${PROJECT_NAME} STATIC ${SOURCES} ${HEADERS})


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

if(APPLE)
option(USE_OPENCL "Use Open Compute Language (OpenCL) for GPU information retrieval."  ON)
endif()

if(USE_OPENCL)
	target_link_libraries(${PROJECT_NAME} "-framework OpenCL")
	target_compile_definitions(${PROJECT_NAME} PUBLIC USE_OPENCL)
endif()

FetchContent_Declare(dylib GIT_REPOSITORY "https://github.com/martin-olivier/dylib" GIT_TAG "v2.2.1")
FetchContent_MakeAvailable(dylib)
target_link_libraries(${PROJECT_NAME} dylib)

add_dependencies(${PROJECT_NAME} IHVs)
if (NOT MLPERF_ENABLE_COLLECT_DEPENDENCIES)
add_dependencies(${PROJECT_NAME} assets)

target_link_libraries(${PROJECT_NAME} assets)
else()
add_custom_command(TARGET ${PROJECT_NAME}
	POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/deps
	COMMAND ${CMAKE_COMMAND} -E copy ${ASSETS_LIST} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/deps
)
endif()

add_dependencies(${PROJECT_NAME} minizip-ng)
target_include_directories(${PROJECT_NAME} PRIVATE ${MINIZIP_INCLUDE_DIR})

target_compile_definitions(${PROJECT_NAME} PUBLIC "WITH_IHV_NATIVE_OPENVINO=$<BOOL:${MLPERF_IHV_NATIVE_OPENVINO}>")
target_compile_definitions(${PROJECT_NAME} PUBLIC "WITH_IHV_ORT_GENAI=$<BOOL:${MLPERF_IHV_ORT_GENAI}>")

option(MLPERF_SKIP_DOWNLOAD_FOR_UNSUPPORTED_EPS "Ignores model overrides and ep dependencies download for EP not supported" ON)
target_compile_definitions(${PROJECT_NAME} PUBLIC "IGNORE_FILES_FROM_DISABLED_EPS=$<BOOL:${MLPERF_SKIP_DOWNLOAD_FOR_UNSUPPORTED_EPS}>")

if(WIN32)
set(EXTRA_LIBRARIES Ws2_32.lib Rpcrt4.lib wbemuuid.lib)

else()
find_package(Iconv REQUIRED)
set(EXTRA_LIBRARIES "-framework CoreFoundation" "-framework Foundation" infoware ${Iconv_LIBRARIES})
endif()

# Force enable exceptions for extern C functions
target_compile_options(${PROJECT_NAME} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/EHsc>)
target_compile_options(${PROJECT_NAME} PRIVATE $<$<CXX_COMPILER_ID:Clang>:-fexceptions>)
target_compile_options(${PROJECT_NAME} PRIVATE $<$<CXX_COMPILER_ID:GNU>:-fexceptions>)
target_compile_options(${PROJECT_NAME} PRIVATE $<$<CXX_COMPILER_ID:AppleClang>:-fexceptions>)
target_compile_options(${PROJECT_NAME} PRIVATE $<$<CXX_COMPILER_ID:Intel>:-fexceptions>)

# Add Tokenizer
add_subdirectory("../../deps/gen-ai/tokenizer" tokenizer)
include_directories(
	"../../deps/gen-ai/"
	"../../deps/gen-ai/tokenizer"
)
target_link_libraries(${PROJECT_NAME} ${HTTPLIB_LIBRARY} ${LOG4CXX_LIBRARY} ${MINIZIB_LINK_LIBS} ${EXTRA_LIBRARIES} nlohmann_json::nlohmann_json nlohmann_json_schema_validator::validator tokenizer)
target_link_libraries(${PROJECT_NAME} ${MINIZIB_LINK_LIBS} ${EXTRA_LIBRARIES} common_cil)

