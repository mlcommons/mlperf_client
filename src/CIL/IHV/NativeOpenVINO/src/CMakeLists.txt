cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0135 NEW)

project(IHV_NativeOpenVINO VERSION 1.0)

if(APPLE)
  message(FATAL_ERROR "Unsupported platform.")
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(WIN32)
    set(CMAKE_GENERATOR "Visual Studio 16 2019" CACHE STRING "CMake Generator")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT WIN32)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set a separate directory for intermediate build files
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

set(OPENVINO_INSTALL_DIR "" CACHE PATH "Path to the OpenVINO installation directory")
set(ONETBB_INSTALL_DIR "${OPENVINO_INSTALL_DIR}/runtime/3rdparty/tbb/")

if(NOT OPENVINO_INSTALL_DIR)
    message(FATAL_ERROR "OPENVINO_INSTALL_DIR is not set. Please set OPENVINO_INSTALL_DIR to the OpenVINO installation directory.")
else()
  if(WIN32)
    find_file(OPENVINO_SETUPVARS_PATH NAMES "setupvars.bat" PATHS ${OPENVINO_INSTALL_DIR} NO_DEFAULT_PATH REQUIRED)
  else() # Linux
    set(OpenVINO_DIR "${OPENVINO_INSTALL_DIR}/runtime/cmake")
    find_package(OpenVINO REQUIRED PATHS ${OpenVINO_DIR} NO_CMAKE_FIND_ROOT_PATH)
    find_package(OpenVINOGenAI REQUIRED PATHS ${OpenVINO_DIR} NO_CMAKE_FIND_ROOT_PATH)
    set(TBB_DIR "${OPENVINO_INSTALL_DIR}/runtime/3rdparty/tbb/lib/cmake/TBB")
  endif()
endif()

# Force use of the release runtime library in all configurations
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:>DLL")
endif()

# Define the source files for the library
file(GLOB_RECURSE SOURCES "*.cpp")

# Define the header files for the library
file(GLOB_RECURSE HEADERS "*.h")

# Define source groups
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Source files" FILES ${SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Header files" FILES ${HEADERS})

# Create a shared library from the source files
add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS})
set_target_properties(${PROJECT_NAME} PROPERTIES BUILD_RPATH "$ORIGIN/;$ORIGIN/../IHV/NativeOpenVINO/")
set_target_properties(${PROJECT_NAME} PROPERTIES BUILD_RPATH_USE_ORIGIN TRUE)

# Assuming you have the find_package for OpenVINO configured
find_package(OpenVINO CONFIG REQUIRED COMPONENTS Runtime)
find_package(OpenVINOGenAI CONFIG REQUIRED)
find_package(TBB REQUIRED)

add_subdirectory(../../../../../deps/JSON JSON)
add_subdirectory(../../../../../deps/JSONSchema JSONSchema)

# Add common code from CIL, used by ONNX too
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../common" common_cil)

target_include_directories(${PROJECT_NAME}
    PUBLIC
    "${OPENVINO_INSTALL_DIR}/runtime/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../common"
)

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_COMMAND}")

# Force linking with OpenVINO Release always to fix issue with VC Runtime Debug

find_package(OpenVINO REQUIRED COMPONENTS Runtime)
find_package(OpenVINOGenAI REQUIRED)

if(WIN32)
  get_target_property(OPENVINO_RUNTIME_LINK_IMPLIB openvino::runtime IMPORTED_IMPLIB_RELEASE)
  get_target_property(OPENVINO_GENAI_LINK_IMPLIB openvino::genai IMPORTED_IMPLIB_RELEASE)
else() # Linux
  get_target_property(OPENVINO_RUNTIME_LINK_IMPLIB openvino::runtime IMPORTED_LOCATION_RELEASE)
  get_target_property(OPENVINO_GENAI_LINK_IMPLIB openvino::genai IMPORTED_LOCATION_RELEASE)
endif()

target_link_libraries(${PROJECT_NAME}
    PRIVATE
    ${OPENVINO_RUNTIME_LINK_IMPLIB}
    ${OPENVINO_GENAI_LINK_IMPLIB}
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator::validator
	common_cil
)

get_target_property(OPENVINO_RUNTIME_INCLUDE_DIRS openvino::runtime INTERFACE_INCLUDE_DIRECTORIES)

target_include_directories(${PROJECT_NAME} PUBLIC ${OPENVINO_RUNTIME_INCLUDE_DIRS})

list(APPEND OPENVINO_EXTRA_DEPS "openvino")
list(APPEND OPENVINO_EXTRA_DEPS "openvino_onnx_frontend")
list(APPEND OPENVINO_EXTRA_DEPS "openvino_intel_cpu_plugin")
list(APPEND OPENVINO_EXTRA_DEPS "openvino_intel_gpu_plugin")
list(APPEND OPENVINO_EXTRA_DEPS "openvino_onnx_frontend")
list(APPEND OPENVINO_EXTRA_DEPS "openvino_pytorch_frontend")
if(WIN32)
  list(APPEND OPENVINO_EXTRA_DEPS "icudt70")
  list(APPEND OPENVINO_EXTRA_DEPS "icuuc70")
  list(APPEND OPENVINO_EXTRA_DEPS "openvino_ir_frontend")
endif()
list(APPEND OPENVINO_EXTRA_DEPS "openvino_auto_batch_plugin")
list(APPEND OPENVINO_EXTRA_DEPS "openvino_auto_plugin")
list(APPEND OPENVINO_EXTRA_DEPS "openvino_c")
list(APPEND OPENVINO_EXTRA_DEPS "openvino_genai")
list(APPEND OPENVINO_EXTRA_DEPS "openvino_hetero_plugin")
list(APPEND OPENVINO_EXTRA_DEPS "openvino_intel_npu_plugin")
list(APPEND OPENVINO_EXTRA_DEPS "openvino_tokenizers")
list(APPEND OPENVINO_EXTRA_DEPS "openvino_paddle_frontend")
list(APPEND OPENVINO_EXTRA_DEPS "openvino_tensorflow_frontend")
list(APPEND OPENVINO_EXTRA_DEPS "openvino_tensorflow_lite_frontend")

if(WIN32)
  list(APPEND TBB_EXTRA_DEPS "tbb12")
else() # Linux
  list(APPEND TBB_EXTRA_DEPS "tbb")
  list(APPEND TBB_EXTRA_DEPS_RELEASE "libtbb.so.12")
  list(APPEND OPENVINO_EXTRA_DEPS_RELEASE libopenvino.so.2530)
  list(APPEND OPENVINO_EXTRA_DEPS_RELEASE libopenvino_genai.so.2530)
  list(APPEND OPENVINO_EXTRA_DEPS_RELEASE libopenvino_ir_frontend.so.2530)
endif()
list(APPEND TBB_EXTRA_DEPS "tbbmalloc")

foreach(LIB ${OPENVINO_EXTRA_DEPS})
  list(APPEND OPENVINO_EXTRA_DEPS_RELEASE ${CMAKE_SHARED_LIBRARY_PREFIX}${LIB}${CMAKE_SHARED_LIBRARY_SUFFIX})
endforeach()

list(JOIN OPENVINO_EXTRA_DEPS_RELEASE ";" OPENVINO_EXTRA_DEPS_RELEASE)

foreach(LIB ${TBB_EXTRA_DEPS})
  list(APPEND TBB_EXTRA_DEPS_RELEASE ${CMAKE_SHARED_LIBRARY_PREFIX}${LIB}${CMAKE_SHARED_LIBRARY_SUFFIX})
endforeach()

list(JOIN TBB_EXTRA_DEPS_RELEASE ";" TBB_EXTRA_DEPS_RELEASE)

string(APPEND _cmd1 "${CMAKE_COMMAND};-E;copy_if_different;${OPENVINO_EXTRA_DEPS_RELEASE};$<TARGET_FILE_DIR:${PROJECT_NAME}>")
string(APPEND _cmd2 "${CMAKE_COMMAND};-E;copy_if_different;${TBB_EXTRA_DEPS_RELEASE};$<TARGET_FILE_DIR:${PROJECT_NAME}>")
string(APPEND _cmd3 "${CMAKE_COMMAND};-E;copy_if_different;plugins.xml;$<TARGET_FILE_DIR:${PROJECT_NAME}>")

if(WIN32)
  set(OPENVINO_LIB_DIR "${OPENVINO_INSTALL_DIR}/runtime/bin/intel64/Release")
  set(ONETBB_LIB_DIR "${ONETBB_INSTALL_DIR}/bin/")
else() # Linux
  set(OPENVINO_LIB_DIR "${OPENVINO_INSTALL_DIR}/runtime/lib/intel64")
  set(ONETBB_LIB_DIR "${ONETBB_INSTALL_DIR}/lib/")
endif()

# Copy dependent libraries
add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    # Copy the AUBIO library
    COMMAND "${_cmd1}"
    COMMENT "Copying OpenVINO DLLs for IHV"
	WORKING_DIRECTORY ${OPENVINO_LIB_DIR}
	COMMAND_EXPAND_LISTS
)

#  Copy dependent libraries
add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND "${_cmd2}"
    COMMENT "Copying TBB DLLs for IHV"
	WORKING_DIRECTORY ${ONETBB_LIB_DIR}
	COMMAND_EXPAND_LISTS
)

# Copy dependent plugin.xml
add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND "${_cmd3}"
    COMMENT "Copying OpenVINO XML Plugin config for IHV"
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	COMMAND_EXPAND_LISTS
)
