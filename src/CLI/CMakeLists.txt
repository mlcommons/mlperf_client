cmake_minimum_required(VERSION 3.15)



get_property(APP_VERSION_SHORT_STRING GLOBAL PROPERTY APP_VERSION_SHORT_STRING)

# Set the project name and version
project(CLI VERSION "${APP_VERSION_SHORT_STRING}")

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
    "../CIL/"
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
elseif(UNIX AND NOT APPLE)
set(EXTRA_LIBRARIES pthread)
else()
set(EXTRA_LIBRARIES "IOReport -framework Cocoa -framework IOKit")
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE CIL PUBLIC ${LOG4CXX_LIBRARY} ${EXTRA_LIBRARIES} ${APR_LIBRARIES} nlohmann_json::nlohmann_json ${MINIZIP_LINK_LIBS})


target_compile_definitions(${PROJECT_NAME} PUBLIC LOG4CXX_STATIC)

include_directories(include ../cil/)
target_include_directories(${PROJECT_NAME} PRIVATE ${LOG4CXX_INCLUDE_DIR})

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})

if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "mlperf-windows" SUFFIX ".exe")
elseif(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "mlperf-mac")
else()
    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "mlperf-linux")
endif()

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/power" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/tools/power"
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/power/mlperf_client_power_efficiency.py" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/mlperf_client_power_efficiency.py"
)
