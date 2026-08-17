# python.cmake — Download python-build-standalone and configure for embedding
#
# Sets Python3_ROOT_DIR so that find_package(Python3) picks up the embedded
# distribution rather than any system Python.

set(PBS_VERSION "3.13.13")
set(PBS_TAG "20260510")

if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(PBS_TRIPLET "x86_64-pc-windows-msvc")
    else()
        set(PBS_TRIPLET "i686-pc-windows-msvc")
    endif()
elseif(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64" OR CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
        set(PBS_TRIPLET "aarch64-apple-darwin")
    else()
        set(PBS_TRIPLET "x86_64-apple-darwin")
    endif()
else()
    message(FATAL_ERROR "IHV Diffusers: supported platforms are Windows and Apple.")
endif()

set(PBS_FILENAME "cpython-${PBS_VERSION}+${PBS_TAG}-${PBS_TRIPLET}-install_only.tar.gz")
set(PBS_URL "https://github.com/astral-sh/python-build-standalone/releases/download/${PBS_TAG}/${PBS_FILENAME}")
set(PBS_DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/_python_standalone")
set(PBS_ARCHIVE "${PBS_DOWNLOAD_DIR}/${PBS_FILENAME}")
set(PBS_EXTRACT_DIR "${PBS_DOWNLOAD_DIR}/extracted")
set(PBS_PYTHON_DIR "${PBS_EXTRACT_DIR}/python")

if(NOT EXISTS "${PBS_PYTHON_DIR}")
    message(STATUS "Downloading python-build-standalone: ${PBS_URL}")
    file(DOWNLOAD "${PBS_URL}" "${PBS_ARCHIVE}"
        STATUS PBS_DL_STATUS
        SHOW_PROGRESS
    )
    list(GET PBS_DL_STATUS 0 PBS_DL_CODE)
    if(NOT PBS_DL_CODE EQUAL 0)
        list(GET PBS_DL_STATUS 1 PBS_DL_MSG)
        message(FATAL_ERROR "Failed to download python-build-standalone: ${PBS_DL_MSG}")
    endif()

    message(STATUS "Extracting python-build-standalone...")
    file(MAKE_DIRECTORY "${PBS_EXTRACT_DIR}")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xzf "${PBS_ARCHIVE}"
        WORKING_DIRECTORY "${PBS_EXTRACT_DIR}"
        RESULT_VARIABLE PBS_EXTRACT_RESULT
    )
    if(NOT PBS_EXTRACT_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to extract python-build-standalone archive")
    endif()

    if(NOT EXISTS "${PBS_PYTHON_DIR}")
        message(FATAL_ERROR "Expected python directory not found after extraction: ${PBS_PYTHON_DIR}")
    endif()

    message(STATUS "python-build-standalone extracted to: ${PBS_PYTHON_DIR}")
else()
    message(STATUS "python-build-standalone already available: ${PBS_PYTHON_DIR}")
endif()

set(Python3_ROOT_DIR "${PBS_PYTHON_DIR}" CACHE PATH "Embedded Python root" FORCE)
set(Python3_FIND_REGISTRY "NEVER" CACHE STRING "" FORCE)
set(Python3_FIND_STRATEGY "LOCATION" CACHE STRING "" FORCE)
set(Python3_FIND_VIRTUALENV "STANDARD" CACHE STRING "" FORCE)

set(PBS_PYTHON_ROOT "${PBS_PYTHON_DIR}" CACHE PATH "Path to embedded python-build-standalone tree" FORCE)
