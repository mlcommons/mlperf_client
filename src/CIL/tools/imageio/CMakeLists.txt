cmake_minimum_required(VERSION 3.15)
project(imageio VERSION 1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
)
FetchContent_MakeAvailable(stb)

set(SOURCES imageio.cpp)
set(HEADERS imageio.h)

add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME}
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${stb_SOURCE_DIR}
)

target_compile_definitions(${PROJECT_NAME} PRIVATE IMAGEIO_BUILDING)

if(NOT WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        C_VISIBILITY_PRESET hidden
    )
endif()

if(IOS)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        FRAMEWORK TRUE
        FRAMEWORK_VERSION A
        MACOSX_FRAMEWORK_IDENTIFIER org.mlcommons.imageio
        MACOSX_FRAMEWORK_SHORT_VERSION_STRING 1.0
        MACOSX_FRAMEWORK_BUNDLE_VERSION 1.0
    )
endif()

if(IMAGEIO_OUTPUT_DIR)
    set(_OUT "${IMAGEIO_OUTPUT_DIR}/$<CONFIG>")
else()
    set(_OUT "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${_OUT}"
    LIBRARY_OUTPUT_DIRECTORY "${_OUT}"
    ARCHIVE_OUTPUT_DIRECTORY "${_OUT}"
)
