if(NOT DEFINED SOURCE_DIR OR NOT DEFINED DEST_DIR)
    message(FATAL_ERROR "SOURCE_DIR and DEST_DIR are required")
endif()

file(REMOVE_RECURSE "${DEST_DIR}")
file(MAKE_DIRECTORY "${DEST_DIR}")
file(COPY "${SOURCE_DIR}/" DESTINATION "${DEST_DIR}")

file(GLOB_RECURSE _vendor_configs "${DEST_DIR}/*.json")

function(add_local_library_path ep_name library_path enabled)
    if(NOT enabled)
        return()
    endif()

    set(_needle "          \"Name\": \"${ep_name}\",")
    set(_replacement
        "${_needle}\n          \"LibraryPath\": \"${library_path}\",")
    set(_changed 0)

    foreach(_config IN LISTS _vendor_configs)
        file(READ "${_config}" _contents)
        string(FIND "${_contents}" "${_needle}" _position)
        if(NOT _position EQUAL -1)
            string(REPLACE "${_needle}" "${_replacement}" _contents "${_contents}")
            file(WRITE "${_config}" "${_contents}")
            math(EXPR _changed "${_changed} + 1")
        endif()
    endforeach()

    message(STATUS
        "Generated ${_changed} local-IHV config(s) for ${ep_name}: ${library_path}")
endfunction()

add_local_library_path("llama-cpp" "IHV/GGML/IHV_GGML_EPs.dll" "${ENABLE_GGML}")
add_local_library_path("WindowsML" "IHV/WindowsML/IHV_WindowsML.dll" "${ENABLE_WINDOWSML}")
add_local_library_path("Diffusers" "IHV/Diffusers/IHV_Diffusers.dll" "${ENABLE_DIFFUSERS}")
add_local_library_path("NativeQNN" "IHV/NativeQNN/IHV_NativeQNN.dll" "${ENABLE_NATIVE_QNN}")
add_local_library_path("OrtGenAI" "IHV/OrtGenAI/IHV_OrtGenAI.dll" "${ENABLE_ORT_GENAI}")
add_local_library_path("OrtGenAI-RyzenAI" "IHV/OrtGenAI-RyzenAI/IHV_OrtGenAI_RyzenAI.dll" "${ENABLE_ORT_GENAI_RYZENAI}")
