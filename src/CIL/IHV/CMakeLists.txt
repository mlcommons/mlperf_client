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

if(MLPERF_IHV_NATIVE_QNN)
add_subdirectory(NativeQNN)
add_dependencies(${PROJECT_NAME} ihv_native_qnn)

#keep the output directory, same as where the executable resides for Genie
add_custom_command(
	TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_BINARY_DIR}/IHV/NativeQNN/bin/$<CONFIG> ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/IHV/NativeQNN/
COMMENT "Copying DLLs from ihv_native_qnn..."
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

if(MLPERF_IHV_ORT_GENAI_RYZENAI)
add_subdirectory(OrtGenAI-RyzenAI)
add_dependencies(${PROJECT_NAME} ihv_ort_genai_ryzenai)

add_custom_command(
	TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_BINARY_DIR}/IHV/OrtGenAI-RyzenAI/bin/$<CONFIG> ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/IHV/OrtGenAI-RyzenAI/
COMMENT "Copying DLLs from ihv_ort_genai_ryzenai..."
)
endif()

if(MLPERF_IHV_GGML)
add_subdirectory(GGML)
add_dependencies(${PROJECT_NAME} ihv_ggml)

add_custom_command(
	TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_BINARY_DIR}/IHV/GGML/bin/$<CONFIG> ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/IHV/GGML/
COMMENT "Copying shared libraries from GGML..."
)
endif()

if(MLPERF_IHV_MLX)
    add_subdirectory(MLX)
    add_dependencies(${PROJECT_NAME} ihv_mlx_source)

    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_BINARY_DIR}/IHV/MLX/bin/$<CONFIG> ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/IHV/MLX/
        COMMENT "Copying shared libraries from ihv_mlx..."
    )
endif()

if(MLPERF_IHV_WINDOWSML)
    add_subdirectory(WindowsML)
    add_dependencies(${PROJECT_NAME} ihv_win_ml)

    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_BINARY_DIR}/IHV/WindowsML/bin/$<CONFIG> ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/IHV/WindowsML/
        COMMENT "Copying shared libraries from ihv_windowsml..."
    )
endif()

