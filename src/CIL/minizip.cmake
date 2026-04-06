include(ExternalProject)

set(MINIZIP_INSTALL_DIR ${CMAKE_BINARY_DIR}/minizip-ng-install)
set(MINIZIP_INCLUDE_DIR ${MINIZIP_INSTALL_DIR}/include)
set(MINIZIP_LIB_DIR ${MINIZIP_INSTALL_DIR}/lib)

set(MINIZIP_PATCH_FILE ${CMAKE_CURRENT_LIST_DIR}/minizip-patch.diff)
set(MINIZIP_SOURCE_DIR ${CMAKE_SOURCE_DIR}/deps/minizip-ng)


find_package(Git QUIET REQUIRED)
    
execute_process(
     COMMAND ${GIT_EXECUTABLE} apply --check ${MINIZIP_PATCH_FILE}
     WORKING_DIRECTORY ${MINIZIP_SOURCE_DIR}
     RESULT_VARIABLE PATCH_CHECK_RESULT
     ERROR_QUIET
)
    
if(PATCH_CHECK_RESULT EQUAL 0)
     execute_process(
       COMMAND ${GIT_EXECUTABLE} apply ${MINIZIP_PATCH_FILE}
        WORKING_DIRECTORY ${MINIZIP_SOURCE_DIR}
        RESULT_VARIABLE PATCH_RESULT
    )
    if(PATCH_RESULT EQUAL 0)
        message(STATUS "Minizip-ng patch applied successfully")
    else()
        message(FATAL_ERROR "Failed to apply minizip-ng patch")
    endif()
else()
    message(STATUS "Minizip-ng patch already applied (skipping)")
endif()

# =============================================================================

if (NOT APPLE AND NOT UNIX)
    set(CMAKE_C_FLAGS_RELEASE "/MT")
    set(CMAKE_CXX_FLAGS_RELEASE "/MT")
	set(MINIZIP_PLATFORM_COMMAND "-A ${CMAKE_GENERATOR_PLATFORM}")
elseif(UNIX)
    set(CMAKE_C_FLAGS_RELEASE "-fPIC -O3")
    set(CMAKE_CXX_FLAGS_RELEASE "-fPIC -O3")
else()
	set(MINIZIP_PLATFORM_COMMAND "")
endif()

if(WIN32)
    list(APPEND MINIZIB_LINK_LIBS 
        ${MINIZIP_LIB_DIR}/bzip2.lib
        ${MINIZIP_LIB_DIR}/lzma.lib
        ${MINIZIP_LIB_DIR}/zlibstatic-ng.lib
        ${MINIZIP_LIB_DIR}/zstd_static.lib
        ${MINIZIP_LIB_DIR}/minizip.lib)
else()
    list(APPEND MINIZIB_LINK_LIBS 
        ${MINIZIP_LIB_DIR}/libbzip2.a
        ${MINIZIP_LIB_DIR}/liblzma.a
        ${MINIZIP_LIB_DIR}/libzstd.a
        ${MINIZIP_LIB_DIR}/libz-ng.a
        ${MINIZIP_LIB_DIR}/libminizip.a)
endif()

ExternalProject_Add(minizip-ng
    PREFIX ${CMAKE_BINARY_DIR}/minizip-ng
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/deps/minizip-ng
    BINARY_DIR ${CMAKE_BINARY_DIR}/minizip-ng-build
    INSTALL_DIR ${MINIZIP_INSTALL_DIR}
    BUILD_ALWAYS OFF
    CONFIGURE_COMMAND ${CMAKE_COMMAND}
        -G "${CMAKE_GENERATOR}"
        ${MINIZIP_PLATFORM_COMMAND}
        -S ${CMAKE_SOURCE_DIR}/deps/minizip-ng
        -B ${CMAKE_BINARY_DIR}/minizip-ng-build
        -DCMAKE_INSTALL_PREFIX=${MINIZIP_INSTALL_DIR}
        -DCMAKE_BUILD_TYPE=Release
        -DBUILD_SHARED_LIBS=OFF
        -DMZ_FETCH_LIBS=ON
        -DMZ_FORCE_FETCH_LIBS=ON
        -DMZ_LIBCOMP=OFF
        -DCMAKE_C_FLAGS_RELEASE=${CMAKE_C_FLAGS_RELEASE}
        -DCMAKE_CXX_FLAGS_RELEASE=${CMAKE_CXX_FLAGS_RELEASE}
    BUILD_COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}/minizip-ng-build --config Release
    INSTALL_COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR}/minizip-ng-build --config Release
    BYPRODUCTS ${MINIZIB_LINK_LIBS}
)
message(STATUS "Minizip link libraries: ${MINIZIB_LINK_LIBS}")
