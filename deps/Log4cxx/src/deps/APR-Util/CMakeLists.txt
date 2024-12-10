cmake_minimum_required(VERSION 3.26)
project(APR-Util)

set(APR_UTIL_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(NOT APPLE)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
set(BUILD_SHARED_LIBS OFF)
set(APR_STATIC ON)

set(APR_INCLUDE_DIR ${APR_INSTALL_DIR}/include)
set(APR_LIBRARIES ${APR_INSTALL_DIR}lib/${CMAKE_STATIC_LIBRARY_PREFIX}apr-1${CMAKE_STATIC_LIBRARY_SUFFIX})

set(BUILD_SHARED_LIBS OFF)
set(APU_HAVE_ODBC OFF)
set(INSTALL_PDB OFF)
set(APR_BUILD_TESTAPR OFF)
set(APR_HAS_LDAP OFF)

add_subdirectory(${APR_UTIL_SOURCE_DIR} apr-util)

# Fix Importing of APR as shared, given we arel lingking with the static version or APR
# Get the existing compile definitions for libaprutil-1
get_target_property(libaprutil-1-compile_defs libaprutil-1 COMPILE_DEFINITIONS)

# Check if the property was found and is not "NOTFOUND"
if(libaprutil-1-compile_defs AND NOT libaprutil-1-compile_defs STREQUAL "NOTFOUND")
    # Remove the specific definition (e.g., "APU_DECLARE_EXPORT")
    list(REMOVE_ITEM libaprutil-1-compile_defs "APR_DECLARE_IMPORT")
	list(APPEND libaprutil-1-compile_defs APR_DECLARE_STATIC)

    # Set the updated compile definitions back on the target
    set_target_properties(libaprutil-1 PROPERTIES COMPILE_DEFINITIONS "${libaprutil-1-compile_defs}")
endif()

# Fix unresolved symbols
TARGET_LINK_LIBRARIES(libaprutil-1 Ws2_32.lib Rpcrt4.lib)

else()

execute_process(
   COMMAND ${CMAKE_COMMAND} -E env "PATH=${ENV_PATH}:$ENV{PATH}" ./buildconf --with-apr=${APR_SOURCE_DIR}/src --with-apr-iconv=${APR_ICONV_SOURCE_DIR}/src
    WORKING_DIRECTORY ${APR_UTIL_SOURCE_DIR}
    RESULT_VARIABLE BUILDCONF_RESULT
)

if(NOT BUILDCONF_RESULT EQUAL "0")
    message(FATAL_ERROR "buildconf failed")
endif()

# Define your full path and the base directory
set(FullPath )
set(BaseDir )

# Calculate the relative path of FullPath with respect to BaseDir
file(RELATIVE_PATH RelativePath ${APR_UTIL_SOURCE_DIR} ${APR_ICONV_SOURCE_DIR}/src)

# Output the relative path
message(STATUS "Relative path: ${RelativePath}")


execute_process(
    COMMAND ${CMAKE_COMMAND} -E env "CC=${CMAKE_C_COMPILER}"  ./configure --with-apr=${APR_INSTALL_DIR} --with-apr-iconv=${RelativePath} --prefix=${CMAKE_INSTALL_PREFIX}
    WORKING_DIRECTORY ${APR_UTIL_SOURCE_DIR}
    RESULT_VARIABLE CONFIGURE_RESULT
)


if(NOT CONFIGURE_RESULT EQUAL "0")
    message(FATAL_ERROR "Configuration failed")
endif()

add_custom_target(BuildExternal
    COMMAND make
    WORKING_DIRECTORY ${APR_UTIL_SOURCE_DIR}
    COMMENT "Building APR-Util..."
)

add_custom_target(InstallExternal ALL
    COMMAND make install
    WORKING_DIRECTORY ${APR_UTIL_SOURCE_DIR}
    COMMENT "Installing APR-Util..."
    DEPENDS BuildExternal
)


endif()


