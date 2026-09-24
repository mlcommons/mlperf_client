if(NOT DEFINED GGML_RUNTIME_DIR OR GGML_RUNTIME_DIR STREQUAL "")
    message(FATAL_ERROR "GGML_RUNTIME_DIR must be supplied when verifying the CUDA runtime.")
endif()

set(_ggml_required_files
    IHV_GGML_EPs.dll
    llama_cpp_CUDA.dll
    cudart64_13.dll
    cublas64_13.dll
    cublasLt64_13.dll
    libomp140.aarch64.dll)

foreach(_ggml_required_file IN LISTS _ggml_required_files)
    if(NOT EXISTS "${GGML_RUNTIME_DIR}/${_ggml_required_file}")
        message(FATAL_ERROR
            "Windows ARM64 GGML CUDA runtime is incomplete: missing "
            "${GGML_RUNTIME_DIR}/${_ggml_required_file}")
    endif()
endforeach()
