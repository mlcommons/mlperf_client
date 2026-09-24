if(NOT DEFINED MLPERF_QT_TARGET_DIR OR NOT DEFINED MLPERF_QT_DEPLOY_DIR)
    message(FATAL_ERROR "MLPERF_QT_TARGET_DIR and MLPERF_QT_DEPLOY_DIR are required")
endif()

# windeployqt selects the dependency set and creates the plugin layout. Replace
# only those selected files with their target-architecture counterparts.
file(GLOB _qt_runtime_dlls "${MLPERF_QT_TARGET_DIR}/bin/*.dll")
foreach(_source IN LISTS _qt_runtime_dlls)
    get_filename_component(_name "${_source}" NAME)
    set(_destination "${MLPERF_QT_DEPLOY_DIR}/${_name}")
    if(EXISTS "${_destination}")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_source}" "${_destination}"
            COMMAND_ERROR_IS_FATAL ANY)
    endif()
endforeach()

file(GLOB_RECURSE _qt_plugin_dlls "${MLPERF_QT_TARGET_DIR}/plugins/*.dll")
foreach(_source IN LISTS _qt_plugin_dlls)
    file(RELATIVE_PATH _relative "${MLPERF_QT_TARGET_DIR}/plugins" "${_source}")
    set(_destination "${MLPERF_QT_DEPLOY_DIR}/${_relative}")
    if(EXISTS "${_destination}")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_source}" "${_destination}"
            COMMAND_ERROR_IS_FATAL ANY)
    endif()
endforeach()

# These are host-only additions from the x64 windeployqt package and are not
# present in the ARM64 Qt distribution.
foreach(_host_only IN ITEMS dxcompiler.dll dxil.dll icuuc.dll opengl32sw.dll)
    file(REMOVE "${MLPERF_QT_DEPLOY_DIR}/${_host_only}")
endforeach()
