cmake_minimum_required(VERSION 3.15)

project(IHVs VERSION 1.0)

add_custom_target(${PROJECT_NAME} ALL
    COMMAND ${CMAKE_COMMAND} -E echo "Building IHVs"
)


if(MLPERF_IHV_NATIVE_OPENVINO)
add_subdirectory(NativeOpenVINO)
add_dependencies(${PROJECT_NAME} ihv_native_openvino)

add_custom_command(
	TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_BINARY_DIR}/IHV/NativeOpenVINO/bin/$<CONFIG> ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/IHV/NativeOpenVINO/
COMMENT "Copying DLLs from ihv_native_openvino..."
)
endif()

if(MLPERF_IHV_ORT_GENAI)
add_subdirectory(OrtGenAI)
add_dependencies(${PROJECT_NAME} ihv_ort_genai)

add_custom_command(
	TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_BINARY_DIR}/IHV/OrtGenAI/bin/$<CONFIG> ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/IHV/OrtGenAI/
COMMENT "Copying DLLs from ihv_ort_genai..."
)
endif()

