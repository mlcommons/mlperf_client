include(ExternalProject)

set(MINIZIP_INSTALL_DIR ${CMAKE_BINARY_DIR}/minizip-ng-install)
set(MINIZIP_INCLUDE_DIR ${MINIZIP_INSTALL_DIR}/include)
set(MINIZIP_LIB_DIR ${MINIZIP_INSTALL_DIR}/lib)


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
    list(APPEND MINIZIP_LINK_LIBS
        ${MINIZIP_LIB_DIR}/bzip2.lib
        ${MINIZIP_LIB_DIR}/lzma.lib
        ${MINIZIP_LIB_DIR}/zlibstatic-ng.lib
        ${MINIZIP_LIB_DIR}/zstd_static.lib
        ${MINIZIP_LIB_DIR}/ppmd.lib
        ${MINIZIP_LIB_DIR}/minizip.lib)
else()
    list(APPEND MINIZIP_LINK_LIBS
        ${MINIZIP_LIB_DIR}/libminizip.a
        ${MINIZIP_LIB_DIR}/libbzip2.a
        ${MINIZIP_LIB_DIR}/liblzma.a
        ${MINIZIP_LIB_DIR}/libzstd.a
        ${MINIZIP_LIB_DIR}/libz-ng.a
        ${MINIZIP_LIB_DIR}/libppmd.a)
endif()

# Cross-compile-only tweaks: tell minizip the target arch and drop its
# OpenSSL crypto backend (no target libssl in the cross sysroot; the app
# uses unencrypted zips). Native builds keep minizip's defaults.
set(MINIZIP_CROSS_ARGS "")
if(NOT "${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "${CMAKE_HOST_SYSTEM_PROCESSOR}")
    list(APPEND MINIZIP_CROSS_ARGS
        -DCMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR}
        -DMZ_OPENSSL=OFF)
    # Forward the cross toolchain, else minizip compiles with the host compiler.
    if(CMAKE_TOOLCHAIN_FILE)
        list(APPEND MINIZIP_CROSS_ARGS -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE})
    endif()
endif()

# iOS runs on Apple Silicon (arm64) runners, so the processor check above
# can't detect the cross-compile (host and target CPU match; only the OS/SDK
# differ) and CMAKE_TOOLCHAIN_FILE may not get forwarded. The fetched zlib-ng
# does its own arch detection in a separate nested configure step and only
# looks at CMAKE_OSX_ARCHITECTURES; if that isn't visible there it silently
# assumes x86 and fails to compile under the iOS SDK.
# See https://github.com/zlib-ng/zlib-ng/issues/1989.
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    list(APPEND MINIZIP_CROSS_ARGS -DMZ_OPENSSL=OFF)
    # Belt-and-suspenders: also force CMAKE_SYSTEM_PROCESSOR directly, since
    # zlib-ng's detect-arch.cmake falls back to it verbatim if its own arch
    # detection comes up empty.
    if(CMAKE_SYSTEM_PROCESSOR)
        list(APPEND MINIZIP_CROSS_ARGS -DCMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR})
    endif()
    if(CMAKE_OSX_ARCHITECTURES)
        list(APPEND MINIZIP_CROSS_ARGS -DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES})
    endif()
    if(CMAKE_OSX_SYSROOT)
        list(APPEND MINIZIP_CROSS_ARGS -DCMAKE_OSX_SYSROOT=${CMAKE_OSX_SYSROOT})
    endif()
    if(CMAKE_TOOLCHAIN_FILE)
        list(APPEND MINIZIP_CROSS_ARGS -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE})
    endif()
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
        ${MINIZIP_CROSS_ARGS}
        -DBUILD_SHARED_LIBS=OFF
        -DMZ_FETCH_LIBS=ON
        -DMZ_FORCE_FETCH_LIBS=ON
        # Vendored bzip2 (sourceware.org git rate-limits CI clones with 429)
        -DFETCHCONTENT_SOURCE_DIR_BZIP2=${CMAKE_SOURCE_DIR}/deps/bzip2
        -DMZ_LIBCOMP=OFF
        -DCMAKE_C_FLAGS_RELEASE=${CMAKE_C_FLAGS_RELEASE}
        -DCMAKE_CXX_FLAGS_RELEASE=${CMAKE_CXX_FLAGS_RELEASE}
    BUILD_COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}/minizip-ng-build --config Release
    INSTALL_COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR}/minizip-ng-build --config Release
    BYPRODUCTS ${MINIZIP_LINK_LIBS}
)
message(STATUS "Minizip link libraries: ${MINIZIP_LINK_LIBS}")
