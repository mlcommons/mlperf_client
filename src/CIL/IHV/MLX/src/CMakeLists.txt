cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0135 NEW)

set(CMAKE_Swift_COMPILER "/usr/bin/swiftc")

include(ExternalProject)

find_program(GIT_EXECUTABLE NAMES git NO_CMAKE_FIND_ROOT_PATH)

project(IHV_MLX VERSION 1.0 LANGUAGES CXX Swift)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_INTERMEDIATE_DIR "${CMAKE_BINARY_DIR}/Build")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_INTERMEDIATE_DIR}")
message(STATUS "Output directory: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

set(CMAKE_CONFIGURATION_TYPES "Release;Debug;RelWithDebInfo" CACHE STRING "List of build configurations" FORCE)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build, options are: Debug Release." FORCE)
endif()

add_compile_definitions(
    $<$<CONFIG:Debug>:DEBUG_BUILD>
    $<$<CONFIG:Release>:RELEASE_BUILD>
    $<$<CONFIG:RelWithDebInfo>:RELWITHDEBINFO_BUILD>
)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Define the source files for the library
file(GLOB_RECURSE SOURCES "*.cpp")

# Define the header files for the library
file(GLOB_RECURSE HEADERS "*.h")

# Define source groups
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Source files" FILES ${SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Header files" FILES ${HEADERS})

message("sources ${SOURCES}")

# Create a shared library from the source files
add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS})

if (IOS)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        FRAMEWORK TRUE
        FRAMEWORK_VERSION A
        MACOSX_FRAMEWORK_IDENTIFIER "org.mlcommons.mlperfihvmlx"
        MACOSX_FRAMEWORK_SHORT_VERSION_STRING 1.0
        MACOSX_FRAMEWORK_BUNDLE_VERSION 1.0
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Apple Development"
    )
endif()

set(BUILD_SHARED_LIBS OFF)
add_subdirectory(../../../../../deps/JSON JSON)
add_subdirectory(../../../../../deps/JSONSchema JSONSchema)

# Add common code from CIL, used by ONNX too
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../common" common_cil)

target_include_directories(${PROJECT_NAME}
    PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../common"
)

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_COMMAND}")

target_link_libraries(${PROJECT_NAME}
    PRIVATE
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator::validator
    common_cil
)

function(apply_git_patch patch_file working_dir)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} apply --ignore-whitespace --check ${patch_file}
        WORKING_DIRECTORY ${working_dir}
        RESULT_VARIABLE PATCH_APPLY_CHECK_RESULT
        OUTPUT_VARIABLE PATCH_APPLY_OUTPUT
        ERROR_VARIABLE PATCH_APPLY_ERROR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE
    )
    message(STATUS "git apply --check returned: ${PATCH_APPLY_CHECK_RESULT}")
    message(STATUS "stdout: ${PATCH_APPLY_OUTPUT}")
    message(WARNING "stderr: ${PATCH_APPLY_ERROR}")
    if (PATCH_APPLY_CHECK_RESULT EQUAL 0)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} apply --ignore-whitespace --allow-empty --reject ${patch_file}
            WORKING_DIRECTORY ${working_dir}
            RESULT_VARIABLE PATCH_APPLY_RESULT
        )
        if (PATCH_APPLY_RESULT EQUAL 0)
            message(STATUS "Patch ${patch_file} applied successfully")
        else ()
            message(STATUS "Patch ${patch_file} could not be applied. Review reject files")
        endif ()
    else ()
        message(STATUS "Patch ${patch_file} already applied or not applicable")
    endif ()
endfunction()

apply_git_patch(${CMAKE_CURRENT_SOURCE_DIR}/mlx.patch ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../deps/mlx-swift/Source/Cmlx/mlx)

if(IOS)
    set(LLAMA_MLX_DESTINATION "generic/platform=iOS")
    set(DERIVED_DATA_DIR "${CMAKE_BINARY_DIR}/build/Build/Products/${CMAKE_BUILD_TYPE}-iphoneos")
else()
    set(LLAMA_MLX_DESTINATION "platform=OS X,arch=arm64")
    set(DERIVED_DATA_DIR "${CMAKE_BINARY_DIR}/build/Build/Products/${CMAKE_BUILD_TYPE}")
endif()
set(LIB_LLAMA_MLX "${CMAKE_BINARY_DIR}/libLlamaMLX.a")

add_custom_target(llama_mlx
    COMMAND xcodebuild -scheme LlamaMLX -derivedDataPath ${CMAKE_BINARY_DIR}/build -configuration ${CMAKE_BUILD_TYPE} -destination ${LLAMA_MLX_DESTINATION} build
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/swift
    COMMENT "Building llama MLX library via xcodebuild"
)

add_custom_command(
    OUTPUT ${LIB_LLAMA_MLX}
    COMMAND libtool -static -o ${LIB_LLAMA_MLX} *.o
    WORKING_DIRECTORY ${DERIVED_DATA_DIR}
    DEPENDS llama_mlx
    COMMENT "Creating static library libLlamaMLX.a"
)

add_custom_target(create_static_lib ALL DEPENDS ${LIB_LLAMA_MLX})

add_dependencies(${PROJECT_NAME} create_static_lib)

find_library(ACCELERATE Accelerate)
find_library(COREFOUNDATION CoreFoundation)

target_link_libraries(${PROJECT_NAME} PRIVATE ${ACCELERATE} ${COREFOUNDATION} ${LIB_LLAMA_MLX})

set_target_properties(${PROJECT_NAME} PROPERTIES
    XCODE_ATTRIBUTE_EMBED_ASSET_PACKS_IN_PRODUCT_BUNDLE "YES"
    XCODE_ATTRIBUTE_ENABLE_ON_DEMAND_RESOURCES "NO"
    XCODE_ATTRIBUTE_ALWAYS_EMBED_SWIFT_STANDARD_LIBRARIES "YES"
    XCODE_ATTRIBUTE_BUILD_LIBRARY_FOR_DISRIBUTION "NO"
    XCODE_ATTRIBUTE_OTHER_LDFLAGS "-fprofile-instr-generate"
    XCODE_ATTRIBUTE_SWIFT_VERSION "5.6"
)

set(INTERMEDIATES_DIR "${CMAKE_BINARY_DIR}/build/Build/Intermediates.noindex")

target_include_directories(${PROJECT_NAME} PRIVATE
"${INTERMEDIATES_DIR}/mlx-swift.build/${CMAKE_BUILD_TYPE}/Cmlx.build/Objects-normal/arm64"
"${INTERMEDIATES_DIR}/mlx-swift.build/${CMAKE_BUILD_TYPE}/MLX.build/Objects-normal/arm64"
"${INTERMEDIATES_DIR}/mlx-swift.build/${CMAKE_BUILD_TYPE}/MLXFFT.build/Objects-normal/arm64"
"${INTERMEDIATES_DIR}/mlx-swift.build/${CMAKE_BUILD_TYPE}/MLXFast.build/Objects-normal/arm64"
"${INTERMEDIATES_DIR}/mlx-swift.build/${CMAKE_BUILD_TYPE}/MLXOptimizers.build/Objects-normal/arm64"
"${INTERMEDIATES_DIR}/mlx-swift.build/${CMAKE_BUILD_TYPE}/MLXNN.build/Objects-normal/arm64"
"${INTERMEDIATES_DIR}/mlx-swift.build/${CMAKE_BUILD_TYPE}/MLXRandom.build/Objects-normal/arm64"
"${INTERMEDIATES_DIR}/LlamaMLX.build/${CMAKE_BUILD_TYPE}/LlamaMLX.build/Objects-normal/arm64"
)

if(IOS)
    set(SWIFT_LIBS_DIR "/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/swift/iphoneos")
else()
    set(SWIFT_LIBS_DIR "/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/swift/macosx")
endif()

target_link_directories(${PROJECT_NAME} PRIVATE ${DERIVED_DATA_DIR} ${SWIFT_LIBS_DIR})

