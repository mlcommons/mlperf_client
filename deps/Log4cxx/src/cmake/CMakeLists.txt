cmake_minimum_required(VERSION 3.26)
project(log4cxx_static_lib)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FindPackageHandleStandardArgs)

message(STATUS "Building Log4cxx...")

set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
set(BUILD_SHARED_LIBS OFF)
set(APR_STATIC ON)
set(BUILD_TESTING OFF)
set(LOG4CXX_INSTALL_PDB OFF)

if(MSVC)
set(APR_INCLUDE_DIR ${APR_INSTALL_DIR}/include)
set(APR_UTIL_INCLUDE_DIR ${APR_UTIL_INSTALL_DIR}/include)
else()
set(APR_INCLUDE_DIR ${APR_INSTALL_DIR}/include/apr-1)
set(APR_UTIL_INCLUDE_DIR ${APR_UTIL_INSTALL_DIR}/include/apr-1)
endif()
#set(EXPAT_INCLUDE_DIR ${EXPAT_INSTALL_DIR}/include)
set(APR_FOUND ON)
set(APR_UTIL_FOUND ON)
set(APR_LIBRARIES_DIR "${APR_INSTALL_DIR}/lib")
set(APR_UTIL_LIBRARIES_DIR "${APR_UTIL_INSTALL_DIR}/lib")
if(APR_ICONV_INSTALL_DIR)
set(APR_ICONV_LIBRARIES_DIR "${APR_ICONV_INSTALL_DIR}/lib")
endif()
set(EXPAT_LIBRARIES_DIR "${EXPAT_INSTALL_DIR}/lib")
file(GLOB APR_LIBRARIES "${APR_LIBRARIES_DIR}/*${CMAKE_STATIC_LIBRARY_SUFFIX}")
file(GLOB APR_UTIL_LIBRARIES "${APR_UTIL_LIBRARIES_DIR}/*${CMAKE_STATIC_LIBRARY_SUFFIX}")
if(APR_ICONV_INSTALL_DIR)
file(GLOB APR_ICONV_LIBRARIES "${APR_ICONV_LIBRARIES_DIR}/*${CMAKE_STATIC_LIBRARY_SUFFIX}")
endif()
file(GLOB EXPAT_LIBRARIES "${EXPAT_LIBRARIES_DIR}/*${CMAKE_STATIC_LIBRARY_SUFFIX}")


function(filter_libraries input_var output_var)
    set(result_list)
    foreach(filepath ${input_var})
        get_filename_component(filename "${filepath}" NAME)
        if(NOT filename MATCHES "^lib.*")
            list(APPEND result_list "${filepath}")
        endif()
    endforeach()
    set(${output_var} ${result_list} PARENT_SCOPE)
endfunction()

#filter_libraries("${APR_LIBRARIES}" APR_LIBRARIES)
#filter_libraries("${APR_UTIL_LIBRARIES}" APR_UTIL_LIBRARIES)

set(APR_STATIC_LINK_LIBRARIES ${APR_LIBRARIES} ${APR_UTIL_LIBRARIES} ${APR_ICONV_LIBRARIES} ${EXPAT_LIBRARIES})

find_package_handle_standard_args(APR APR_INCLUDE_DIR APR_LIBRARIES)
find_package_handle_standard_args(APR-Util APR_UTIL_INCLUDE_DIR APR_UTIL_LIBRARIES)

add_subdirectory(${LOG4CXX_SOURCE_DIR} log4cxx EXCLUDE_FROM_ALL)

target_link_libraries(log4cxx PRIVATE ${APR_STATIC_LINK_LIBRARIES})

if(APPLE)
find_package(Iconv REQUIRED)
target_link_libraries(log4cxx PRIVATE ${Iconv_LIBRARIES})
endif()

message(STATUS "CMAKE_CXX_FLAGS: ${CMAKE_CXX_FLAGS}")

include(bundle_static_library.cmake)
bundle_static_library(${PROJECT_NAME} log4cxx)

install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/log4cxx/src/main/include/log4cxx
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.h"
  PATTERN "Private" EXCLUDE
)

install(DIRECTORY ${LOG4CXX_SOURCE_DIR}/src/main/include/log4cxx
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.h"
  PATTERN "Private" EXCLUDE
)

install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}${PROJECT_NAME}${CMAKE_STATIC_LIBRARY_SUFFIX}
    RENAME ${CMAKE_STATIC_LIBRARY_PREFIX}log4cxx${CMAKE_STATIC_LIBRARY_SUFFIX}
    TYPE LIB
)


