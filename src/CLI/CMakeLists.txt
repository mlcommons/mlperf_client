cmake_minimum_required(VERSION 3.15)

# support executable versioning
file(READ "${CMAKE_SOURCE_DIR}/version.json" VERSION_JSON)
string(JSON MAJOR_VERSION GET ${VERSION_JSON} major)
string(JSON MINOR_VERSION GET ${VERSION_JSON} minor)
string(JSON PATCH_VERSION GET ${VERSION_JSON} patch)
string(JSON VERSION_SUFFIX GET ${VERSION_JSON} suffix)

# calculate the build number
execute_process(
        COMMAND git rev-parse --short HEAD
        OUTPUT_VARIABLE GIT_COMMIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(STRIP "${VERSION_SUFFIX}" STRIPPED_SUFFIX)
if(STRIPPED_SUFFIX STREQUAL "")
    set(VERSION_STRING "${MAJOR_VERSION}.${MINOR_VERSION}.${PATCH_VERSION}.${GIT_COMMIT_HASH}")
else()
    set(VERSION_STRING "${MAJOR_VERSION}.${MINOR_VERSION}.${PATCH_VERSION}-${VERSION_SUFFIX}.${GIT_COMMIT_HASH}")
endif()
message(STATUS "Git commit hash: ${GIT_COMMIT_HASH}")
configure_file(./version.h.in "${CMAKE_CURRENT_BINARY_DIR}/version.h" @ONLY)
#  add custom command to make sure cmake will track the changes in version.json
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/version.json")

set_property(GLOBAL PROPERTY APP_VERSION_STRING ${VERSION_STRING})

# read the content of the version.h and display it
file(READ "${CMAKE_CURRENT_BINARY_DIR}/version.h" VERSION_H)
message(STATUS "version file content: ${VERSION_H}")
# Set the project name and version
project(CLI VERSION "${MAJOR_VERSION}.${MINOR_VERSION}.${PATCH_VERSION}")

if(NOT IOS)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Force use of the release runtime library in all configurations
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:>")
endif()

# Define the source files for the library
file(GLOB_RECURSE SOURCES "*.cpp")

if(APPLE)
    # Include .m and .mm files only for Apple
    file(GLOB_RECURSE MM_SOURCES "*.m" "*.mm")
    list(APPEND SOURCES ${MM_SOURCES})
endif()

# Define the header files for the library
file(GLOB_RECURSE HEADERS "*.h")

# Define source groups
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Source files" FILES ${SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Header files" FILES ${HEADERS})


# Create the console executable
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_COMMAND}")

target_include_directories(${PROJECT_NAME}
    PRIVATE
# Add possible include directories here
)

target_compile_definitions(${PROJECT_NAME}
    PRIVATE
# Add additional compile definitions here
)

target_compile_options(${PROJECT_NAME}
	PRIVATE
	$<$<CXX_COMPILER_ID:MSVC>:/w45038 /w45205 /w44061 /EHsc>
	$<$<CXX_COMPILER_ID:Clang>:-Wreorder -fexceptions>
)

find_package(Log4Cxx REQUIRED)

set(LOG4CXX_LIBRARY ${LOG4CXX_LIB_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}log4cxx${CMAKE_STATIC_LIBRARY_SUFFIX})

if(WIN32)
set(EXTRA_LIBRARIES Ws2_32.lib Rpcrt4.lib dxcore.lib)
target_link_options(${PROJECT_NAME} PRIVATE "/INCREMENTAL:NO")
else()
set(EXTRA_LIBRARIES "IOReport -framework Cocoa -framework IOKit")
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE CIL PUBLIC ${LOG4CXX_LIBRARY} ${EXTRA_LIBRARIES} nlohmann_json::nlohmann_json)


target_compile_definitions(${PROJECT_NAME} PUBLIC LOG4CXX_STATIC)

include_directories(include ../cil/)
target_include_directories(${PROJECT_NAME} PRIVATE ${LOG4CXX_INCLUDE_DIR})

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})

if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "mlperf-windows" SUFFIX ".exe")
else()
    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "mlperf-mac")
endif()

endif()# NOT IOS