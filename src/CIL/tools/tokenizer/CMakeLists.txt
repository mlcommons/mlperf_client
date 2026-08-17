cmake_minimum_required(VERSION 3.15)
project(tokenizer VERSION 1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# --- minja (chat template engine) ---
set(MINJA_TEST_ENABLED OFF CACHE BOOL "" FORCE)
set(MINJA_EXAMPLE_ENABLED OFF CACHE BOOL "" FORCE)
set(MINJA_FUZZTEST_ENABLED OFF CACHE BOOL "" FORCE)

FetchContent_Declare(minja
    GIT_REPOSITORY https://github.com/google/minja
    GIT_TAG        3e4c61c616eda133cfb1e440fc7a14bf1729bbee
)
FetchContent_MakeAvailable(minja)

# --- onnxruntime-extensions (tokenizer backend) ---
if(NOT CMAKE_POLICY_VERSION_MINIMUM)
    set(CMAKE_POLICY_VERSION_MINIMUM "3.5" CACHE STRING "" FORCE)
endif()

set(OCOS_BUILD_PRESET           "" CACHE STRING "" FORCE)

set(OCOS_ENABLE_GPT2_TOKENIZER  ON  CACHE BOOL "" FORCE)
set(OCOS_ENABLE_SPM_TOKENIZER   ON  CACHE BOOL "" FORCE)
set(OCOS_ENABLE_C_API           ON  CACHE BOOL "" FORCE)
set(OCOS_BUILD_SHARED_LIB       OFF CACHE BOOL "" FORCE)

set(OCOS_ENABLE_BERT_TOKENIZER      OFF CACHE BOOL "" FORCE)
set(OCOS_ENABLE_TRIE_TOKENIZER      OFF CACHE BOOL "" FORCE)
set(OCOS_ENABLE_WORDPIECE_TOKENIZER OFF CACHE BOOL "" FORCE)
set(OCOS_ENABLE_BLINGFIRE           OFF CACHE BOOL "" FORCE)
set(OCOS_ENABLE_TF_STRING           OFF CACHE BOOL "" FORCE)
set(OCOS_ENABLE_RE2_REGEX           OFF CACHE BOOL "" FORCE)
set(OCOS_ENABLE_MATH                OFF CACHE BOOL "" FORCE)
set(OCOS_ENABLE_DLIB                OFF CACHE BOOL "" FORCE)
set(OCOS_ENABLE_AUDIO               OFF CACHE BOOL "" FORCE)
set(OCOS_ENABLE_VISION              OFF CACHE BOOL "" FORCE)
set(OCOS_ENABLE_CV2                 OFF CACHE BOOL "" FORCE)
set(OCOS_ENABLE_OPENCV_CODECS       OFF CACHE BOOL "" FORCE)
set(OCOS_ENABLE_AZURE               OFF CACHE BOOL "" FORCE)
set(OCOS_ENABLE_VENDOR_IMAGE_CODECS OFF CACHE BOOL "" FORCE)
set(OCOS_ENABLE_SELECTED_OPLIST     OFF CACHE BOOL "" FORCE)
set(OCOS_ENABLE_CTEST               OFF CACHE BOOL "" FORCE)
set(OCOS_BUILD_PYTHON               OFF CACHE BOOL "" FORCE)
set(OCOS_BUILD_JAVA                 OFF CACHE BOOL "" FORCE)
set(OCOS_BUILD_ANDROID              OFF CACHE BOOL "" FORCE)
set(OCOS_BUILD_APPLE_FRAMEWORK      OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    ort_extensions
    GIT_REPOSITORY https://github.com/microsoft/onnxruntime-extensions.git
    GIT_TAG        e094cc816679d0b2b5fe2b4fd7f73e5b1844b425
)
FetchContent_MakeAvailable(ort_extensions)

if(TARGET onnxruntime_extensions)
    set(MLPERF_ORT_EXTENSIONS_TARGET onnxruntime_extensions)
elseif(TARGET ortcustomops)
    set(MLPERF_ORT_EXTENSIONS_TARGET ortcustomops)
elseif(TARGET ocos_operators)
    set(MLPERF_ORT_EXTENSIONS_TARGET ocos_operators)
else()
    message(FATAL_ERROR
        "ort_extensions: could not locate static library target after FetchContent. "
        "Check ${ort_extensions_SOURCE_DIR}/CMakeLists.txt for the target name.")
endif()

# ort_extensions' bundled minja crashes at -O0 on clang/arm64; bump just the
# ort_extensions targets to -O1 in Debug. Our tokenizer code stays at -O0.
if(APPLE)
    foreach(_ortx_target IN ITEMS ortcustomops ocos_operators noexcep_operators)
        if(TARGET ${_ortx_target})
            get_target_property(_ortx_kind ${_ortx_target} TYPE)
            if(NOT _ortx_kind STREQUAL "INTERFACE_LIBRARY")
                target_compile_options(${_ortx_target} PRIVATE
                    $<$<CONFIG:Debug>:-O1>)
            endif()
        endif()
    endforeach()
endif()

set(MLPERF_ORT_EXTENSIONS_INCLUDE_DIRS "${ort_extensions_SOURCE_DIR}/include")

set(SOURCES
    tokenizer.cpp
    chat_template.cpp
)
set(HEADERS
    tokenizer.h
    chat_template.h
)

add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME}
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(${PROJECT_NAME} PRIVATE TOKENIZER_BUILDING)
target_link_libraries(${PROJECT_NAME} PRIVATE minja)
target_include_directories(${PROJECT_NAME} PRIVATE ${MLPERF_ORT_EXTENSIONS_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${MLPERF_ORT_EXTENSIONS_TARGET})

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
        MACOSX_FRAMEWORK_IDENTIFIER org.mlcommons.tokenizer
        MACOSX_FRAMEWORK_SHORT_VERSION_STRING 1.0
        MACOSX_FRAMEWORK_BUNDLE_VERSION 1.0
    )
endif()

if(TOKENIZER_OUTPUT_DIR)
    set(_OUT "${TOKENIZER_OUTPUT_DIR}/$<CONFIG>")
else()
    set(_OUT "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${_OUT}"
    LIBRARY_OUTPUT_DIRECTORY "${_OUT}"
    ARCHIVE_OUTPUT_DIRECTORY "${_OUT}"
)
