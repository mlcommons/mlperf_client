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
elseif(IOS)
    # DepsPacker should run on the host macOS
	set(DEPS_PACKER_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../toolchain/macos_arm64.toolchain.cmake")
	set(DEPS_PACKER_GENERATOR_PLATFORM_COMMAND "-DCMAKE_TOOLCHAIN_FILE=${DEPS_PACKER_TOOLCHAIN_FILE}")
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
    file(GLOB_RECURSE MM_SOURCES "*.m" "*.mm")
    list(APPEND SOURCES ${MM_SOURCES})
endif()

if(APPLE)
	list(FILTER HEADERS EXCLUDE REGEX "system_info_provider_windows.h")
	list(FILTER SOURCES EXCLUDE REGEX "system_info_provider_windows.cpp")
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
	list(FILTER HEADERS EXCLUDE REGEX "smc_reader.h")
	list(FILTER SOURCES EXCLUDE REGEX "smc_reader.cpp")
	list(FILTER HEADERS EXCLUDE REGEX "io_report_reader.h")
	list(FILTER SOURCES EXCLUDE REGEX "io_report_reader.cpp")	
endif()

if(NOT IOS)
	list(FILTER HEADERS EXCLUDE REGEX "system_info_provider_ios.h")
	list(FILTER SOURCES EXCLUDE REGEX "system_info_provider_ios.mm")
    list(FILTER SOURCES EXCLUDE REGEX "ios/")
    list(FILTER HEADERS EXCLUDE REGEX "ios/")
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

list(FILTER SOURCES EXCLUDE REGEX "common/")
list(FILTER HEADERS EXCLUDE REGEX "common/")

# Define source groups
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Source files" FILES ${SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Header files" FILES ${HEADERS})

set(LOG4CXXCONFIG_XML "${CMAKE_SOURCE_DIR}/data/Log4CxxConfig.xml")
set(CONFIGSCHEMA_JSON "${CMAKE_SOURCE_DIR}/data/ConfigSchema.json")
set(LLM_INPUT_SCHEMA_JSON "${CMAKE_SOURCE_DIR}/data/LLMInputSchema.json")
set(OUTPUT_RESULTS_SCHEMA_JSON "${CMAKE_SOURCE_DIR}/data/OutputResultsSchema.json")
set(DATA_VERIFICATION_JSON "${CMAKE_SOURCE_DIR}/data/data_verification.json")
set(DATA_VERIFICATION_SCHEMA_JSON "${CMAKE_SOURCE_DIR}/data/DataVerificationSchema.json")

set(VENDORS_DEFAULT_CONFIGS_DIR "${CMAKE_SOURCE_DIR}/data/configs/vendors_default")

set(CONFIG_VERIFICATION_JSON "${CMAKE_BINARY_DIR}/config_verification.json")
set(CONFIG_EXPERIMENTAL_VERIFICATION_JSON "${CMAKE_BINARY_DIR}/config_experimental_verification.json")

file(GLOB_RECURSE VENDORS_ALL_CONFIGS CONFIGURE_DEPENDS "${VENDORS_DEFAULT_CONFIGS_DIR}/*.json")

set(VENDORS_DEFAULT_CONFIGS "")
set(VENDORS_EXPERIMENTAL_CONFIGS "")

foreach(CONFIG_FILE ${VENDORS_ALL_CONFIGS})
	list(APPEND VENDORS_DEFAULT_CONFIGS "${CONFIG_FILE}")
    if(CONFIG_FILE MATCHES "vendors_default/experimental")
        list(APPEND VENDORS_EXPERIMENTAL_CONFIGS "${CONFIG_FILE}")
    endif()
endforeach()

message(STATUS "Computing hashes for vendor default configs")
add_custom_target(
	compute_configs_hashes
	ALL
	COMMAND ${CMAKE_COMMAND} -DFILES_TO_HASH="${VENDORS_DEFAULT_CONFIGS}" -DOUTPUT_FILE="${CONFIG_VERIFICATION_JSON}" -P "${CMAKE_CURRENT_SOURCE_DIR}/compute_hashes.cmake"
)

message(STATUS "Computing hashes for vendor experimental configs")
add_custom_target(
    compute_experimental_configs_hashes
    ALL
    COMMAND ${CMAKE_COMMAND} -DFILES_TO_HASH="${VENDORS_EXPERIMENTAL_CONFIGS}" -DOUTPUT_FILE="${CONFIG_EXPERIMENTAL_VERIFICATION_JSON}" -P "${CMAKE_CURRENT_SOURCE_DIR}/compute_hashes.cmake"
)

if(MLPERF_PACK_VENDORS_DEFAULT)
    set(VENDORS_DEFAULT_ZIP "${CMAKE_BINARY_DIR}/vendors_default.zip")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -DVENDORS_DEFAULT_CONFIGS_DIR=${VENDORS_DEFAULT_CONFIGS_DIR} -DVENDORS_DEFAULT_ZIP=${VENDORS_DEFAULT_ZIP} -P "${CMAKE_CURRENT_SOURCE_DIR}/pack_vendors_default.cmake"
        WORKING_DIRECTORY "${VENDORS_DEFAULT_CONFIGS_DIR}"
        RESULT_VARIABLE PACK_RESULT
    )
    if(NOT PACK_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to pack vendors default")
    endif()
endif()

# Use separate config file for the EP dependencies depending on platform
# Windows x64, Windows ARM64, MacOS
if(WIN32)
# Check if x64
    if(CMAKE_GENERATOR_PLATFORM MATCHES "x64")
        set(EP_DEPENDENCIES_CONFIG_JSON_FILENAME "ep_dependencies_config_windows_x64.json")
    else()
		set(EP_DEPENDENCIES_CONFIG_JSON_FILENAME "ep_dependencies_config_windows_ARM.json")
	endif()
elseif(IOS)
    set(EP_DEPENDENCIES_CONFIG_JSON_FILENAME "ep_dependencies_config_iOS.json")
elseif(APPLE)#APPLE but not IOS
    set(EP_DEPENDENCIES_CONFIG_JSON_FILENAME "ep_dependencies_config_macOS.json")
endif()
set(EP_DEPENDENCIES_CONFIG_JSON "${CMAKE_SOURCE_DIR}/data/${EP_DEPENDENCIES_CONFIG_JSON_FILENAME}")
set(EP_DEPENDENCIES_CONFIG_SCHEMA_JSON "${CMAKE_SOURCE_DIR}/data/EPDependenciesConfigSchema.json")
set(EP_IHVS_NATIVE_OPENVINO_LIB "${CMAKE_BINARY_DIR}/IHV/NativeOpenVINO/bin/$<CONFIG>/IHV_NativeOpenVINO.dll")
set(EP_IHVS_ORT_GENAI_LIB "${CMAKE_BINARY_DIR}/IHV/OrtGenAI/bin/$<CONFIG>/IHV_OrtGenAI.dll")
set(EP_IHVS_ORT_GENAI_RYZENAI_LIB "${CMAKE_BINARY_DIR}/IHV/OrtGenAI-RyzenAI/bin/$<CONFIG>/IHV_OrtGenAI_RyzenAI.dll")
set(EP_IHVS_WIN_ML_LIB "${CMAKE_BINARY_DIR}/IHV/WindowsML/bin/$<CONFIG>/IHV_WindowsML.dll")
if (IOS)
    set(EP_IHVS_GGML_LIB "${CMAKE_BINARY_DIR}/IHV/GGML/bin/$<CONFIG>/IHV_GGML_EPs.framework")
    set(EP_IHVS_GGML_METAL_LIB "${CMAKE_BINARY_DIR}/IHV/GGML/Metal/bin/$<CONFIG>/GGML_Metal.framework")
else()
    set(EP_IHVS_GGML_LIB "${CMAKE_BINARY_DIR}/IHV/GGML/bin/$<CONFIG>/${CMAKE_SHARED_LIBRARY_PREFIX}IHV_GGML_EPs${CMAKE_SHARED_LIBRARY_SUFFIX}")
    set(EP_IHVS_GGML_METAL_LIB "${CMAKE_BINARY_DIR}/IHV/GGML/Metal/bin/$<CONFIG>/${CMAKE_SHARED_LIBRARY_PREFIX}GGML_Metal${CMAKE_SHARED_LIBRARY_SUFFIX}")
endif()
set(EP_IHVS_GGML_VULKAN_LIB "${CMAKE_BINARY_DIR}/IHV/GGML/Vulkan/bin/$<CONFIG>/${CMAKE_SHARED_LIBRARY_PREFIX}GGML_Vulkan${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(EP_IHVS_GGML_CUDA_LIB "${CMAKE_BINARY_DIR}/IHV/GGML/CUDA/bin/$<CONFIG>/${CMAKE_SHARED_LIBRARY_PREFIX}GGML_CUDA${CMAKE_SHARED_LIBRARY_SUFFIX}")
set_property(GLOBAL PROPERTY  EP_IHVS_GGML_LIB ${EP_IHVS_GGML_LIB})
set_property(GLOBAL PROPERTY  EP_IHVS_GGML_METAL_LIB ${EP_IHVS_GGML_METAL_LIB})
set(EP_IHVS_NATIVE_QNN_LIB "${CMAKE_BINARY_DIR}/IHV/NativeQNN/bin/$<CONFIG>/${CMAKE_SHARED_LIBRARY_PREFIX}IHV_NativeQNN${CMAKE_SHARED_LIBRARY_SUFFIX}")

include(deps_packer/pack_as_binary.cmake)
set (IHV_FILES_TO_SIGN "")

set(ASSETS_LIST 
	${LOG4CXXCONFIG_XML}
	${CONFIGSCHEMA_JSON}
	${LLM_INPUT_SCHEMA_JSON}
	${OUTPUT_RESULTS_SCHEMA_JSON}
	${DATA_VERIFICATION_JSON}
	${DATA_VERIFICATION_SCHEMA_JSON}
	${EP_DEPENDENCIES_CONFIG_JSON}
	${EP_DEPENDENCIES_CONFIG_SCHEMA_JSON}
	${CONFIG_VERIFICATION_JSON}
	${CONFIG_EXPERIMENTAL_VERIFICATION_JSON}
)

if(MLPERF_PACK_VENDORS_DEFAULT)
    list(APPEND ASSETS_LIST ${VENDORS_DEFAULT_ZIP})
endif()

include(minizip.cmake)

# Place to add IHV libs if needed to be unpacked/included by the single executable build
if(MLPERF_IHV_MLX)
    if (IOS)
	    set(EP_IHVS_MLX_LIB "${CMAKE_BINARY_DIR}/IHV/MLX/bin/$<CONFIG>/IHV_MLX.framework")
		set(MLX_METALLIB "${CMAKE_BINARY_DIR}/IHV/MLX/build/Build/Products/${CMAKE_BUILD_TYPE}-iphoneos/mlx-swift_Cmlx.bundle/default.metallib")
		set_property(GLOBAL PROPERTY  MLX_METALLIB ${MLX_METALLIB})
	else()
        set(EP_IHVS_MLX_LIB "${CMAKE_BINARY_DIR}/IHV/MLX/bin/$<CONFIG>/${CMAKE_SHARED_LIBRARY_PREFIX}IHV_MLX${CMAKE_SHARED_LIBRARY_SUFFIX}")
	endif()
	set_property(GLOBAL PROPERTY  EP_IHVS_MLX_LIB ${EP_IHVS_MLX_LIB})
	
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
endif()

if(MLPERF_IHV_NATIVE_QNN)
	list(APPEND ASSETS_LIST ${EP_IHVS_NATIVE_QNN_LIB})
endif() 

pack_as_binary(assets
    SOURCES
	${ASSETS_LIST}
)
add_dependencies(assets compute_configs_hashes)
add_dependencies(assets compute_experimental_configs_hashes)

set_property(GLOBAL PROPERTY IHV_FILES_TO_SIGN ${IHV_FILES_TO_SIGN})

if(MLPERF_IHV_NATIVE_OPENVINO)
	add_dependencies(assets ihv_native_openvino)
endif()
if(MLPERF_IHV_ORT_GENAI)
	add_dependencies(assets ihv_ort_genai)
endif()
if(MLPERF_IHV_ORT_GENAI_RYZENAI)
	add_dependencies(assets ihv_ort_genai_ryzenai)
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
target_compile_definitions(${PROJECT_NAME} PUBLIC "WITH_IHV_MLX=$<BOOL:${MLPERF_IHV_MLX}>")
target_compile_definitions(${PROJECT_NAME} PUBLIC "WITH_IHV_NATIVE_QNN=$<BOOL:${MLPERF_IHV_NATIVE_QNN}>")
target_compile_definitions(${PROJECT_NAME} PUBLIC "VENDORS_DEFAULT_PACKED=$<BOOL:${MLPERF_PACK_VENDORS_DEFAULT}>")

option(MLPERF_SKIP_DOWNLOAD_FOR_UNSUPPORTED_EPS "Ignores model overrides and ep dependencies download for EP not supported" ON)
target_compile_definitions(${PROJECT_NAME} PUBLIC "IGNORE_FILES_FROM_DISABLED_EPS=$<BOOL:${MLPERF_SKIP_DOWNLOAD_FOR_UNSUPPORTED_EPS}>")

if(WIN32)
set(EXTRA_LIBRARIES Ws2_32.lib Rpcrt4.lib wbemuuid.lib)

else()
find_package(Iconv REQUIRED)
set(EXTRA_LIBRARIES "-framework CoreFoundation" "-framework Foundation" ${Iconv_LIBRARIES})

if (IOS)
	list(APPEND EXTRA_LIBRARIES "-framework Metal")
else()
    list(APPEND EXTRA_LIBRARIES infoware)
endif()
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

