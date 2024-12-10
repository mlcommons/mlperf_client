# FindOpenSSL.cmake

if(WIN32 AND MSVC)
    if(CMAKE_GENERATOR_PLATFORM MATCHES "ARM64")
        set(OpenSSL_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/Windows/MSVC/ARM")
    else()
        set(OpenSSL_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/Windows/MSVC/x64")
    endif()
elseif(APPLE)
    # Check the processor architecture
    if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "arm" OR CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "aarch64")
        set(OpenSSL_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/MacOS/ARM")
    elseif(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "x86_64")
        set(OpenSSL_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/MacOS/x86_64")
    else()
        message(FATAL_ERROR "Unsupported macOS processor architecture.")
    endif()
else()
    message(FATAL_ERROR "Unsupported platform.")
endif()

set(OpenSSL_INCLUDE_DIR "${OpenSSL_ROOT_DIR}/include")

set(OpenSSL_CRYPTO_LIBRARY "${OpenSSL_ROOT_DIR}/lib/libcrypto${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(OpenSSL_SSL_LIBRARY "${OpenSSL_ROOT_DIR}/lib/libssl${CMAKE_STATIC_LIBRARY_SUFFIX}")

set(OpenSSL_FOUND TRUE)
set(OpenSSL_VERSION "3.0.12")

# For the find_package command
set(OpenSSL_LIBRARIES ${OpenSSL_SSL_LIBRARY} ${OpenSSL_CRYPTO_LIBRARY})
set(OpenSSL_INCLUDE_DIRS ${OpenSSL_INCLUDE_DIR})

# Mark components as found
set(OpenSSL_Crypto_FOUND TRUE)
set(OpenSSL_SSL_FOUND TRUE)

# Make the variables found by this script available to the parent script
mark_as_advanced(OpenSSL_INCLUDE_DIR OpenSSL_CRYPTO_LIBRARY OpenSSL_SSL_LIBRARY)

# Include directories for targets
include_directories(${OpenSSL_INCLUDE_DIRS})

# Create imported target for OpenSSL::Crypto if not already defined
if(NOT TARGET OpenSSL::Crypto)
	add_library(OpenSSL::Crypto STATIC IMPORTED)
	set_target_properties(OpenSSL::Crypto PROPERTIES
		IMPORTED_LOCATION "${OpenSSL_CRYPTO_LIBRARY}"
		INTERFACE_INCLUDE_DIRECTORIES "${OpenSSL_INCLUDE_DIR}")
endif()

# Create imported target for OpenSSL::SSL if not already defined
if(NOT TARGET OpenSSL::SSL)
	add_library(OpenSSL::SSL STATIC IMPORTED)
	set_target_properties(OpenSSL::SSL PROPERTIES
		IMPORTED_LOCATION "${OpenSSL_SSL_LIBRARY}"
		INTERFACE_INCLUDE_DIRECTORIES "${OpenSSL_INCLUDE_DIR}")
endif()
