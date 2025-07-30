cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0135 NEW)

project(${PROJECT_NAME} VERSION 1.0)

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

set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Force use of the release runtime library in all configurations
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL$<$<CONFIG:Debug>:>")
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

if (IOS)
    string(REPLACE "_" "" PROJECT_NAME_NO_UNDERSCORE ${PROJECT_NAME})
    string(TOLOWER ${PROJECT_NAME_NO_UNDERSCORE} PROJECT_NAME_LOWER)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        FRAMEWORK TRUE
        FRAMEWORK_VERSION A
        MACOSX_FRAMEWORK_IDENTIFIER "org.mlcommons.mlperfihv${PROJECT_NAME_LOWER}"
        MACOSX_FRAMEWORK_SHORT_VERSION_STRING 1.0
        MACOSX_FRAMEWORK_BUNDLE_VERSION 1.0
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Apple Development"
    )
endif()

set(BUILD_SHARED_LIBS OFF)
add_subdirectory(../../../../../../deps/JSON JSON)
add_subdirectory(../../../../../../deps/JSONSchema JSONSchema)

set(GIT_EXE ${GIT_EXECUTABLE})

find_program(GIT_EXE NAMES git git.exe REQUIRED NO_CMAKE_FIND_ROOT_PATH)

# Add common code from CIL
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../../common" common_cil)

add_subdirectory(../../../../../../deps/llama.cpp GGML)
set_target_properties(llama PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)

target_include_directories(${PROJECT_NAME}
    PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../../common"
)

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_COMMAND}")

if(APPLE)
  set_target_properties(${PROJECT_NAME} PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH_USE_LINK_PATH TRUE
    INSTALL_RPATH "@loader_path"
    MACOSX_RPATH TRUE)
endif()

target_link_libraries(${PROJECT_NAME}
    PRIVATE
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator::validator
	common_cil
	llama
)