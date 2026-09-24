cmake_minimum_required(VERSION 3.26)

project(IHVs VERSION 1.0)

add_custom_target(${PROJECT_NAME} ALL
    COMMAND ${CMAKE_COMMAND} -E echo "Building IHVs"
)


if(MLPERF_IHV_NATIVE_OPENVINO)
add_subdirectory(NativeOpenVINO)
add_dependencies(${PROJECT_NAME} ihv_native_openvino)
if(WIN32)
set(MLPERF_IHV_NATIVE_OPENVINO_BUILD_PATH "${CMAKE_BINARY_DIR}/IHV/NativeOpenVINO/bin/$<CONFIG>")
else() # Linux
set(MLPERF_IHV_NATIVE_OPENVINO_BUILD_PATH "${CMAKE_BINARY_DIR}/IHV/NativeOpenVINO/Build")
endif()

add_custom_command(
	TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${MLPERF_IHV_NATIVE_OPENVINO_BUILD_PATH} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/IHV/NativeOpenVINO/
    BYPRODUCTS ${MLPERF_IHV_NATIVE_OPENVINO_BUILD_PATH}/${CMAKE_SHARED_LIBRARY_PREFIX}IHV_NativeOpenVINO${CMAKE_SHARED_LIBRARY_SUFFIX}
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
    BYPRODUCTS ${CMAKE_BINARY_DIR}/IHV/OrtGenAI/bin/$<CONFIG>/IHV_OrtGenAI.dll
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
    BYPRODUCTS ${CMAKE_BINARY_DIR}/IHV/OrtGenAI-RyzenAI/bin/$<CONFIG>/IHV_OrtGenAI_RyzenAI.dll
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

if(MLPERF_IHV_GGML_CUDA)
    # The local-IHV configuration loads the GGML dispatcher directly from the
    # runtime tree. Stage its selected backend beside it so the dispatcher can
    # resolve the provider with the normal Windows DLL search rules.
    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_BINARY_DIR}/IHV/GGML/CUDA/bin/$<CONFIG> ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/IHV/GGML/
        COMMENT "Copying CUDA shared libraries from GGML..."
    )

    if(WIN32 AND (CMAKE_GENERATOR_PLATFORM MATCHES "ARM64" OR
                  CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|arm64|aarch64"))
        # The Windows ARM64 CUDA toolkit keeps target runtime DLLs separately
        # from the host tools. A local build must stage this closure beside the
        # GGML dispatcher and CUDA provider; it must never rely on PATH.
        set(_ggml_cuda_target_bin "${CUDAToolkit_ROOT}/bin/arm64")
        if(NOT CUDAToolkit_ROOT OR NOT EXISTS "${_ggml_cuda_target_bin}")
            message(FATAL_ERROR
                "Windows ARM64 GGML CUDA packaging requires CUDAToolkit_ROOT "
                "with ARM64 target runtime files under bin/arm64.")
        endif()

        set(_ggml_cuda_runtime_files
            cudart64_13.dll
            cublas64_13.dll
            cublasLt64_13.dll)
        foreach(_ggml_cuda_runtime IN LISTS _ggml_cuda_runtime_files)
            set(_ggml_cuda_runtime_path
                "${_ggml_cuda_target_bin}/${_ggml_cuda_runtime}")
            if(NOT EXISTS "${_ggml_cuda_runtime_path}")
                message(FATAL_ERROR
                    "Windows ARM64 GGML CUDA runtime was not found: "
                    "${_ggml_cuda_runtime_path}")
            endif()
            add_custom_command(
                TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_ggml_cuda_runtime_path}" ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/IHV/GGML/
                COMMENT "Copying ARM64 CUDA runtime for GGML..."
            )
        endforeach()

        # Prefer the Visual Studio developer-environment value. Derive the
        # same redist location from the selected compiler as a fallback.
        set(_ggml_openmp_runtime_candidates "")

        if(DEFINED ENV{VCToolsRedistDir} AND NOT "$ENV{VCToolsRedistDir}" STREQUAL "")
            file(GLOB _ggml_openmp_env_candidates
                "$ENV{VCToolsRedistDir}/debug_nonredist/arm64/Microsoft.VC*.OpenMP.LLVM/libomp140.aarch64.dll")
            list(APPEND _ggml_openmp_runtime_candidates ${_ggml_openmp_env_candidates})
        endif()

        file(TO_CMAKE_PATH "${CMAKE_CXX_COMPILER}" _ggml_cxx_compiler)

        if(_ggml_cxx_compiler MATCHES "^(.*)/VC/Tools/(MSVC|Llvm)/")
            # The compiler and redist directories can use different servicing
            # patch versions. Search the selected Visual Studio installation.
            file(GLOB _ggml_openmp_redist_candidates
                "${CMAKE_MATCH_1}/VC/Redist/MSVC/*/debug_nonredist/arm64/Microsoft.VC*.OpenMP.LLVM/libomp140.aarch64.dll")

            list(APPEND _ggml_openmp_runtime_candidates
                ${_ggml_openmp_redist_candidates})
        endif()

        foreach(_ggml_openmp_candidate IN LISTS _ggml_openmp_runtime_candidates)
            if(EXISTS "${_ggml_openmp_candidate}")
                set(_ggml_openmp_runtime "${_ggml_openmp_candidate}")
                break()
            endif()
        endforeach()

        if(NOT DEFINED _ggml_openmp_runtime)
            message(FATAL_ERROR
                "Windows ARM64 GGML CUDA packaging requires libomp140.aarch64.dll "
                "from the selected Visual Studio LLVM OpenMP runtime.")
        endif()
        add_custom_command(
            TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_ggml_openmp_runtime}" ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/IHV/GGML/
            COMMAND ${CMAKE_COMMAND}
                    -DGGML_RUNTIME_DIR=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/IHV/GGML
                    -P ${CMAKE_CURRENT_LIST_DIR}/verify_ggml_cuda_runtime.cmake
            COMMENT "Staging and verifying the ARM64 GGML CUDA runtime..."
        )

        # Smoke test; skips itself unless it runs natively with the NVIDIA driver.
        if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "ARM64|arm64|aarch64")
            find_program(_ggml_powershell_executable NAMES powershell pwsh REQUIRED)
            add_custom_command(
                TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${_ggml_powershell_executable} -NoProfile -NonInteractive
                        -ExecutionPolicy Bypass
                        -File ${CMAKE_CURRENT_LIST_DIR}/verify_ggml_cuda_load.ps1
                        -DllPath ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/IHV/GGML/llama_cpp_CUDA.dll
                COMMENT "Loading the native ARM64 GGML CUDA provider..."
            )
        endif()
    endif()
endif()
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
        BYPRODUCTS ${CMAKE_BINARY_DIR}/IHV/WindowsML/bin/$<CONFIG>/IHV_WindowsML.dll
        COMMENT "Copying shared libraries from ihv_windowsml..."
    )

endif()

if(MLPERF_IHV_DIFFUSERS)
    add_subdirectory(Diffusers)
    add_dependencies(${PROJECT_NAME} ihv_diffusers)
    # No POST_BUILD copy: the Diffusers sub-build installs the DLL +
    # python/ + standalone/ directly into
    # ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/IHV/Diffusers/ via cmake --install.
endif()

