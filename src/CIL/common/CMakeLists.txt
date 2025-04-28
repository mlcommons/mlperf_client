cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0135 NEW)

# Set the project name and version
project(common_cil VERSION 1.0)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Define the source files for the library
file(GLOB_RECURSE SOURCES "*.cpp")

# Define the header files for the library
file(GLOB_RECURSE HEADERS "*.h")

# Exclude files located in the directml subdirectory
list(FILTER SOURCES EXCLUDE REGEX ".*/directml/.*")
list(FILTER HEADERS EXCLUDE REGEX ".*/directml/.*")

# Define source groups
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Source files" FILES ${SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Header files" FILES ${HEADERS})

# Create a static library from the source files
add_library(${PROJECT_NAME} STATIC ${SOURCES} ${HEADERS})

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_COMMAND}")

target_compile_options(${PROJECT_NAME}
	PRIVATE
	$<$<CXX_COMPILER_ID:MSVC>:/w45038 /w45205 /w44061 /EHsc>
	$<$<CXX_COMPILER_ID:Clang>:-Wreorder -fexceptions>
)

# Force enable exceptions for extern C functions
target_compile_options(${PROJECT_NAME} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/EHsc>)
target_compile_options(${PROJECT_NAME} PRIVATE $<$<CXX_COMPILER_ID:Clang>:-fexceptions>)
target_compile_options(${PROJECT_NAME} PRIVATE $<$<CXX_COMPILER_ID:GNU>:-fexceptions>)
target_compile_options(${PROJECT_NAME} PRIVATE $<$<CXX_COMPILER_ID:AppleClang>:-fexceptions>)
target_compile_options(${PROJECT_NAME} PRIVATE $<$<CXX_COMPILER_ID:Intel>:-fexceptions>)

if(NOT TARGET nlohmann_json::nlohmann_json)
add_subdirectory(../../../deps/JSON JSON)
endif()

if(NOT TARGET nlohmann_json_schema_validator::validator)
add_subdirectory(../../../deps/JSONSchema JSONSchema)
endif()

target_link_libraries(${PROJECT_NAME} ${HTTPLIB_LIBRARY} ${LOG4CXX_LIBRARY} ${EXTRA_LIBRARIES} nlohmann_json::nlohmann_json nlohmann_json_schema_validator::validator)

