cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0135 NEW)

project(IHV_Diffusers VERSION 1.0)

if(APPLE)
  message(STATUS "Diffusers IHV: macOS dev build uses python-build-standalone.")
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

message(STATUS "Platform: ${PLATFORM}")
message(STATUS "Architecture: ${CMAKE_SYSTEM_PROCESSOR}")

set(CMAKE_INTERMEDIATE_DIR "${CMAKE_BINARY_DIR}/Build")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_INTERMEDIATE_DIR}")
message(STATUS "Output directory: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

set(CMAKE_CONFIGURATION_TYPES "Release;Debug;RelWithDebInfo" CACHE STRING "List of build configurations" FORCE)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()

add_compile_definitions(
  $<$<CONFIG:Debug>:DEBUG_BUILD>
  $<$<CONFIG:Release>:RELEASE_BUILD>
  $<$<CONFIG:RelWithDebInfo>:RELWITHDEBINFO_BUILD>
)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(MSVC)
    # PBS Python + wheels are built with /MD. Match in every config (including
    # Debug) -- mixing /MDd with /MD crashes on cross-CRT free().
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()

# Top-level CMakeLists declares this; redeclared here as a fallback when
# the sub-CMakeLists is configured standalone.
option(MLPERF_IHV_DIFFUSERS_INSTALL_PYTHON "Download embedded Python and install packages (dev)" OFF)

# --- Download embedded Python (always needed for headers/pybind11 linking) ---
include("${CMAKE_CURRENT_SOURCE_DIR}/python.cmake")

# Find Python3 BEFORE pybind11 so pybind11 doesn't pick up a system Python.
find_package(Python3 COMPONENTS Interpreter Development.Embed REQUIRED)
message(STATUS "Python3 found: ${Python3_EXECUTABLE} (${Python3_VERSION})")
message(STATUS "Python3 include: ${Python3_INCLUDE_DIRS}")
message(STATUS "Python3 libraries: ${Python3_LIBRARIES}")

set(PYTHON_EXECUTABLE   "${Python3_EXECUTABLE}" CACHE FILEPATH "" FORCE)
set(Python_EXECUTABLE   "${Python3_EXECUTABLE}" CACHE FILEPATH "" FORCE)
set(pybind11_PYTHON_VERSION "${Python3_VERSION}" CACHE STRING "" FORCE)

# --- pybind11 ---
include(FetchContent)
FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG        v3.0.4
)
FetchContent_MakeAvailable(pybind11)

# Embed scripts/*.py as C++ string literals; python_bridge.cpp installs
# a meta-path finder over the resulting manifest. Excluded files are
# CLI tools the DLL never imports.
set(EPP_PACKAGE_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/scripts")
set(EPP_OUTPUT_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/inference_package_manifest.h")
set(EPP_NAMESPACE     "cil::IHV::embedded")
set(EPP_EXCLUDE       "standalone_runner.py;quantize_model.py")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_python_package.cmake")

# Diffusers EP targets:
#   * "Diffusers"     — EP library, links pybind11 + embedded Python.
#   * "IHV_Diffusers" — dispatcher, pure C++; only built for 2+ vendors.
# Single-vendor builds skip the dispatcher: the EP library takes the
# IHV_Diffusers name and installs flat at the EP root.

# --- Vendor enablement ---
# Bootstrap defaults for a standalone inner build (no outer forwarding, or
# an empty -D... from an unset top-level var). Per-vendor, platform-gated.
if(NOT DEFINED MLPERF_IHV_DIFFUSERS_NVIDIA OR
   "${MLPERF_IHV_DIFFUSERS_NVIDIA}" STREQUAL "")
    if(WIN32)
        set(MLPERF_IHV_DIFFUSERS_NVIDIA ON CACHE BOOL
            "Enable NVIDIA / CUDA Diffusers EP library" FORCE)
    else()
        set(MLPERF_IHV_DIFFUSERS_NVIDIA OFF CACHE INTERNAL "")
    endif()
endif()
if(NOT DEFINED MLPERF_IHV_DIFFUSERS_APPLE OR
   "${MLPERF_IHV_DIFFUSERS_APPLE}" STREQUAL "")
    if(APPLE)
        set(MLPERF_IHV_DIFFUSERS_APPLE ON CACHE BOOL
            "Enable Apple / MPS Diffusers EP library" FORCE)
    else()
        set(MLPERF_IHV_DIFFUSERS_APPLE OFF CACHE INTERNAL "")
    endif()
endif()
if(NOT DEFINED MLPERF_IHV_DIFFUSERS_AMD OR
   "${MLPERF_IHV_DIFFUSERS_AMD}" STREQUAL "")
    set(MLPERF_IHV_DIFFUSERS_AMD OFF CACHE BOOL
        "Enable AMD / RyzenAI Diffusers EP library" FORCE)
endif()


# Reject a vendor enabled outside its supported platform.
if(MLPERF_IHV_DIFFUSERS_APPLE AND NOT APPLE)
    message(FATAL_ERROR
        "MLPERF_IHV_DIFFUSERS_APPLE is enabled but this is not a macOS build. "
        "The Apple EP library needs MPS-capable torch which only ships on "
        "Apple Silicon. Disable the option or build on macOS.")
endif()
if(MLPERF_IHV_DIFFUSERS_NVIDIA AND NOT WIN32 AND NOT (UNIX AND NOT APPLE))
    message(FATAL_ERROR
        "MLPERF_IHV_DIFFUSERS_NVIDIA is enabled but this is not a "
        "Windows / Linux build. CUDA torch wheels target those platforms.")
endif()
if(MLPERF_IHV_DIFFUSERS_AMD AND NOT WIN32)
    message(FATAL_ERROR
        "MLPERF_IHV_DIFFUSERS_AMD is enabled but this is not a Windows build. "
        "RyzenAI ONNX Diffusers is Windows x64 only.")
endif()

set(_diffusers_vendors "")
if(MLPERF_IHV_DIFFUSERS_NVIDIA)
    list(APPEND _diffusers_vendors NVIDIA)
endif()
if(MLPERF_IHV_DIFFUSERS_APPLE)
    list(APPEND _diffusers_vendors APPLE)
endif()
if(MLPERF_IHV_DIFFUSERS_AMD)
    list(APPEND _diffusers_vendors AMD)
endif()
list(LENGTH _diffusers_vendors _n_diffusers_vendors)
if(_n_diffusers_vendors EQUAL 0)
    message(FATAL_ERROR
        "Diffusers EP: no vendor EP library enabled. Set at least one of "
        "MLPERF_IHV_DIFFUSERS_NVIDIA / MLPERF_IHV_DIFFUSERS_APPLE / "
        "MLPERF_IHV_DIFFUSERS_AMD.")
endif()
message(STATUS "Diffusers EP enabled vendors: ${_diffusers_vendors}")

# --- Partition sources: dispatcher vs EP library ---
file(GLOB_RECURSE _all_sources "*.cpp")
file(GLOB_RECURSE _all_headers "*.h")
set(_dispatcher_sources
    "${CMAKE_CURRENT_SOURCE_DIR}/diffusers_dispatcher.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/diffusers_dispatcher.h")
set(_ep_library_sources ${_all_sources} ${_all_headers})
list(REMOVE_ITEM _ep_library_sources ${_dispatcher_sources})

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Source files"
    FILES ${_ep_library_sources} ${_dispatcher_sources})


# --- Shared deps (JSON / common_cil are linked by both targets) ---
add_subdirectory(../../../../../deps/JSON JSON)
add_subdirectory(../../../../../deps/JSONSchema JSONSchema)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../common" common_cil)

# --- EP-library target ("Diffusers") ---
add_library(Diffusers SHARED ${_ep_library_sources})

target_include_directories(Diffusers
    PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../common"
    PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../.."
)
set_target_properties(Diffusers PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_COMMAND}")
target_link_libraries(Diffusers
    PRIVATE
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator::validator
    common_cil
    pybind11::embed
)

# Fix libpython install name from python-build-standalone on macOS.
if(APPLE)
    set(_libpython_name
        "libpython${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}.dylib")
    add_custom_command(TARGET Diffusers POST_BUILD
        COMMAND install_name_tool -change
            "/install/lib/${_libpython_name}"
            "@loader_path/${_libpython_name}"
            "$<TARGET_FILE:Diffusers>"
        COMMAND install_name_tool -change
            "@rpath/${_libpython_name}"
            "@loader_path/${_libpython_name}"
            "$<TARGET_FILE:Diffusers>"
        COMMAND codesign --force --sign - "$<TARGET_FILE:Diffusers>"
        COMMENT "Rewriting libpython install name to @loader_path"
        VERBATIM
    )
endif()

# --- Install: EP-library placement depends on enabled-vendor count ---
if(_n_diffusers_vendors EQUAL 1)
    # Single-vendor mode: skip dispatcher. The EP library takes the
    # IHV_Diffusers name and installs flat at the EP root.
    list(GET _diffusers_vendors 0 _only_vendor)
    set_target_properties(Diffusers PROPERTIES OUTPUT_NAME "IHV_Diffusers")
    install(TARGETS Diffusers
        RUNTIME DESTINATION .
        LIBRARY DESTINATION .)
    message(STATUS "Diffusers EP single-vendor mode (${_only_vendor}): "
                   "flat install at EP root, no dispatcher")
else()
    # Multi-vendor mode: one EP-library binary installed once per vendor into
    # its subdir with a vendor-suffixed name. OUTPUT_NAME=Diffusers_Vendor
    # keeps the asset-compiler symbol (Diffusers_Vendor_dll) vendor-neutral.
    set_target_properties(Diffusers PROPERTIES OUTPUT_NAME "Diffusers_Vendor")
    foreach(_v IN LISTS _diffusers_vendors)
        string(TOLOWER "${_v}" _v_lc)
        install(FILES $<TARGET_FILE:Diffusers>
            DESTINATION "${_v_lc}"
            RENAME
            "${CMAKE_SHARED_LIBRARY_PREFIX}Diffusers_${_v}${CMAKE_SHARED_LIBRARY_SUFFIX}")
    endforeach()

    # Dispatcher target — pure C++, loaded by the harness, forwards to
    # the right EP library under <vendor>/Diffusers_<VENDOR>.<ext>.
    include("${CMAKE_CURRENT_SOURCE_DIR}/../../../common/dylib.cmake")
    add_library(IHV_Diffusers SHARED ${_dispatcher_sources})
    target_include_directories(IHV_Diffusers
        PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../common")
    target_link_libraries(IHV_Diffusers
        PRIVATE
        dylib
        nlohmann_json::nlohmann_json
        common_cil)
    set_target_properties(IHV_Diffusers PROPERTIES
        CXX_CLANG_TIDY "${CLANG_TIDY_COMMAND}")
    install(TARGETS IHV_Diffusers
        RUNTIME DESTINATION .
        LIBRARY DESTINATION .)

    # NOTE: WITH_IHV_DIFFUSERS_DISPATCHER is defined for CIL consumers
    # (unpacker.cpp, preparation_stage.cpp) in src/CIL/CMakeLists.txt
    # alongside the other WITH_IHV_* defines — not here, because this
    # inner build doesn't compile those files.

    message(STATUS "Diffusers EP multi-vendor mode (${_diffusers_vendors}): "
                   "dispatcher at EP root, EP libraries in vendor subdirs")
endif()

if(APPLE)
    install(CODE "execute_process(COMMAND codesign --force --sign - \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_SHARED_LIBRARY_PREFIX}IHV_Diffusers${CMAKE_SHARED_LIBRARY_SUFFIX}\")")
endif()

# --- Per-vendor install prefix list ---
# N=1: ["."] (flat at EP root); N≥2: one subdir per vendor. Used by the
# PBS install, install_python_deps, and standalone loops below.
if(_n_diffusers_vendors EQUAL 1)
    set(_vendor_install_subdirs ".")
else()
    set(_vendor_install_subdirs "")
    foreach(_v IN LISTS _diffusers_vendors)
        string(TOLOWER "${_v}" _v_lc)
        list(APPEND _vendor_install_subdirs "${_v_lc}")
    endforeach()
endif()

# --- Install: Embedded Python runtime (per vendor) ---
if(WIN32 AND MLPERF_IHV_DIFFUSERS_INSTALL_PYTHON)
    file(GLOB PBS_ROOT_DLLS "${PBS_PYTHON_ROOT}/*.dll")
    foreach(_v IN LISTS _vendor_install_subdirs)
        if(_v STREQUAL ".")
            set(_root_dest ".")
            set(_py_dest "python")
        else()
            set(_root_dest "${_v}")
            set(_py_dest "${_v}/python")
        endif()
        # Mirror python.dll + vcruntime alongside the EP-library DLL so the
        # Windows DLL loader finds them next to whatever loads python.
        install(FILES ${PBS_ROOT_DLLS} DESTINATION "${_root_dest}")
        # Embedded interpreter itself + its Lib/DLLs.
        install(FILES "${PBS_PYTHON_ROOT}/python.exe" DESTINATION "${_py_dest}")
        install(FILES ${PBS_ROOT_DLLS} DESTINATION "${_py_dest}")
        install(DIRECTORY "${PBS_PYTHON_ROOT}/Lib" DESTINATION "${_py_dest}")
        install(DIRECTORY "${PBS_PYTHON_ROOT}/DLLs" DESTINATION "${_py_dest}")
    endforeach()
elseif(APPLE AND MLPERF_IHV_DIFFUSERS_INSTALL_PYTHON)
    file(GLOB PBS_ROOT_DYLIBS "${PBS_PYTHON_ROOT}/lib/libpython*.dylib")
    foreach(_v IN LISTS _vendor_install_subdirs)
        if(_v STREQUAL ".")
            set(_root_dest ".")
            set(_py_dest "python")
        else()
            set(_root_dest "${_v}")
            set(_py_dest "${_v}/python")
        endif()
        install(FILES ${PBS_ROOT_DYLIBS} DESTINATION "${_root_dest}")
        install(DIRECTORY "${PBS_PYTHON_ROOT}/" DESTINATION "${_py_dest}"
                USE_SOURCE_PERMISSIONS)
    endforeach()
endif()

# --- Install: pip packages ---
if(WIN32)
    set(REQUIREMENTS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/assets/requirements-nvidia.txt")
elseif(APPLE)
    set(REQUIREMENTS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/assets/requirements-apple.txt")
else()
    message(FATAL_ERROR "IHV Diffusers: no requirements file for this platform.")
endif()

set(INSTALL_DEPS_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/../tools/install_python_deps.py")
set(COMPILE_ENV_SCRIPT  "${CMAKE_CURRENT_SOURCE_DIR}/../tools/compile_env.py")
if(WIN32 AND MLPERF_IHV_DIFFUSERS_INSTALL_PYTHON)
    foreach(_v IN LISTS _vendor_install_subdirs)
        if(_v STREQUAL ".")
            set(_py_subdir "python")
            set(_sentinel_rel ".packages_installed")
            set(_freeze_rel "pip_freeze.txt")
            string(TOLOWER "${_only_vendor}" _vendor_lc)
        else()
            set(_py_subdir "${_v}/python")
            set(_sentinel_rel "${_v}/.packages_installed")
            set(_freeze_rel "${_v}/pip_freeze.txt")
            set(_vendor_lc "${_v}")
        endif()
        install(CODE "
            set(_python_exe \"\${CMAKE_INSTALL_PREFIX}/${_py_subdir}/python.exe\")
            set(_sentinel \"\${CMAKE_INSTALL_PREFIX}/${_sentinel_rel}\")
            set(_install_script \"${INSTALL_DEPS_SCRIPT}\")
            if(NOT EXISTS \${_sentinel})
                execute_process(
                    COMMAND \${_python_exe} -I \${_install_script} --vendor ${_vendor_lc}
                    RESULT_VARIABLE _pip_result)
                if(NOT _pip_result EQUAL 0)
                    message(FATAL_ERROR \"install_python_deps.py failed: \${_pip_result}\")
                endif()
                execute_process(
                    COMMAND \${_python_exe} -I -m pip freeze
                    OUTPUT_FILE \"\${CMAKE_INSTALL_PREFIX}/${_freeze_rel}\")
                # Freeze the env to unchecked-hash bytecode (compile_env.py)
                # so the installed env matches what python_env.zip ships.
                execute_process(
                    COMMAND \${_python_exe} -I \"${COMPILE_ENV_SCRIPT}\"
                            \"\${CMAKE_INSTALL_PREFIX}/${_py_subdir}\")
                file(WRITE \${_sentinel} \"ok\")
            endif()
        ")
    endforeach()
elseif(APPLE AND MLPERF_IHV_DIFFUSERS_INSTALL_PYTHON)
    foreach(_v IN LISTS _vendor_install_subdirs)
        if(_v STREQUAL ".")
            set(_py_subdir "python")
            set(_sentinel_rel ".packages_installed")
            set(_freeze_rel "pip_freeze.txt")
            string(TOLOWER "${_only_vendor}" _vendor_lc)
        else()
            set(_py_subdir "${_v}/python")
            set(_sentinel_rel "${_v}/.packages_installed")
            set(_freeze_rel "${_v}/pip_freeze.txt")
            set(_vendor_lc "${_v}")
        endif()
        install(CODE "
            set(_python_exe \"\${CMAKE_INSTALL_PREFIX}/${_py_subdir}/bin/python3\")
            set(_sentinel \"\${CMAKE_INSTALL_PREFIX}/${_sentinel_rel}\")
            set(_install_script \"${INSTALL_DEPS_SCRIPT}\")
            if(NOT EXISTS \${_sentinel})
                execute_process(
                    COMMAND \${_python_exe} -I \${_install_script} --vendor ${_vendor_lc}
                    RESULT_VARIABLE _pip_result)
                if(NOT _pip_result EQUAL 0)
                    message(FATAL_ERROR \"install_python_deps.py failed: \${_pip_result}\")
                endif()
                execute_process(
                    COMMAND \${_python_exe} -I -m pip freeze
                    OUTPUT_FILE \"\${CMAKE_INSTALL_PREFIX}/${_freeze_rel}\")
                # Freeze the env to unchecked-hash bytecode (compile_env.py)
                # so the installed env matches what python_env.zip ships.
                execute_process(
                    COMMAND \${_python_exe} -I \"${COMPILE_ENV_SCRIPT}\"
                            \"\${CMAKE_INSTALL_PREFIX}/${_py_subdir}\")
                file(WRITE \${_sentinel} \"ok\")
            endif()
        ")
    endforeach()
endif()

# diffusers_standalone target: PyInstaller-frozen bundle of
# standalone_runner.py. On-demand only; not driven by cmake --install.
option(MLPERF_IHV_DIFFUSERS_BUILD_STANDALONE
    "Provide the diffusers_standalone build target (dev only)" ON)

set(STANDALONE_SPEC        "${CMAKE_CURRENT_SOURCE_DIR}/../tools/standalone.spec")
set(STANDALONE_TOOLS_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/../tools")
set(STANDALONE_SCRIPTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/scripts")

if((WIN32 OR APPLE) AND MLPERF_IHV_DIFFUSERS_INSTALL_PYTHON)
    set(_standalone_supported ON)
else()
    set(_standalone_supported OFF)
endif()

if(MLPERF_IHV_DIFFUSERS_BUILD_STANDALONE AND _standalone_supported)
    # Per-vendor standalone targets — one PyInstaller bundle per enabled
    # vendor, each invoking its own vendor's embedded python so the right
    # site-packages get bundled. In single-vendor mode `_vendor_install_subdirs`
    # is ["."] and we produce one target named diffusers_standalone; in
    # multi-vendor mode we produce one per vendor and an umbrella that
    # depends on all of them.
    set(_standalone_subtargets "")
    foreach(_v IN LISTS _vendor_install_subdirs)
        if(_v STREQUAL ".")
            set(_target_suffix "")
            set(_vendor_prefix "${CMAKE_INSTALL_PREFIX}")
        else()
            set(_target_suffix "_${_v}")
            set(_vendor_prefix "${CMAKE_INSTALL_PREFIX}/${_v}")
        endif()

        # Path of the embedded python and where PyInstaller stages the
        # onedir bundle for this vendor.
        if(WIN32)
            set(_PY_EXE "${_vendor_prefix}/python/python.exe")
        else()
            set(_PY_EXE "${_vendor_prefix}/python/bin/python3")
        endif()
        # build_standalone.cmake.in templates this into _output_dir and
        # _sentinel — needs to be the vendor's install root, no trailing slash.
        set(INSTALL_PREFIX "${_vendor_prefix}")
        set(STANDALONE_WORKPATH "${STANDALONE_TOOLS_DIR}/build${_target_suffix}")

        # configure_file outputs a per-vendor cmake driver (different
        # _python_exe + _output_dir + _sentinel per vendor).
        set(_driver_out
            "${CMAKE_CURRENT_BINARY_DIR}/build_standalone${_target_suffix}.cmake")
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/build_standalone.cmake.in"
            "${_driver_out}"
            @ONLY)

        set(_target_name "diffusers_standalone${_target_suffix}")
        add_custom_target(${_target_name}
            COMMAND ${CMAKE_COMMAND} -P "${_driver_out}"
            WORKING_DIRECTORY "${STANDALONE_TOOLS_DIR}"
            COMMENT "Building/refreshing Diffusers PyInstaller standalone bundle (${_v})"
            VERBATIM)
        set_target_properties(${_target_name} PROPERTIES FOLDER "IHV/Diffusers")
        list(APPEND _standalone_subtargets ${_target_name})
    endforeach()

    # Umbrella target — always present so outer cmake / IDE keeps working.
    # In single-vendor mode the per-vendor target IS named diffusers_standalone
    # (empty _target_suffix), so we skip the umbrella to avoid the name clash.
    list(LENGTH _vendor_install_subdirs _n_install_subdirs)
    if(_n_install_subdirs GREATER 1)
        add_custom_target(diffusers_standalone DEPENDS ${_standalone_subtargets})
        set_target_properties(diffusers_standalone PROPERTIES FOLDER "IHV/Diffusers")
    endif()
endif()
