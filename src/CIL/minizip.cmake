include(ExternalProject)

set(MINIZIP_INSTALL_DIR ${CMAKE_BINARY_DIR}/minizip-ng-install)
set(MINIZIP_INCLUDE_DIR ${MINIZIP_INSTALL_DIR}/include)
set(MINIZIP_LIB_DIR ${MINIZIP_INSTALL_DIR}/lib)

if (NOT APPLE)
    set(CMAKE_C_FLAGS_RELEASE "/MT")
    set(CMAKE_CXX_FLAGS_RELEASE "/MT")
	set(MINIZIP_PLATFORM_COMMAND "-A ${CMAKE_GENERATOR_PLATFORM}")
else()
	set(MINIZIP_PLATFORM_COMMAND "")
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
)

if(APPLE)
    list(APPEND MINIZIB_LINK_LIBS 
        ${MINIZIP_LIB_DIR}/libbzip2.a
        ${MINIZIP_LIB_DIR}/liblzma.a
        ${MINIZIP_LIB_DIR}/libzstd.a
        ${MINIZIP_LIB_DIR}/libz-ng.a
        ${MINIZIP_LIB_DIR}/libminizip.a)
else()
    list(APPEND MINIZIB_LINK_LIBS 
        ${MINIZIP_LIB_DIR}/bzip2.lib
        ${MINIZIP_LIB_DIR}/lzma.lib
        ${MINIZIP_LIB_DIR}/zlibstatic-ng.lib
        ${MINIZIP_LIB_DIR}/zstd_static.lib
        ${MINIZIP_LIB_DIR}/minizip.lib)
endif()

message(STATUS "Minizip link libraries: ${MINIZIB_LINK_LIBS}")