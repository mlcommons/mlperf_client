function(pack_as_binary target)
    set(one_value_args NAMESPACE HEADER_NAME)
    set(multi_value_args SOURCES)
    cmake_parse_arguments(DEPS_ARG "" "${one_value_args}" "${multi_value_args}" ${ARGN})

    list(LENGTH DEPS_ARG_SOURCES num_binary_files)

    if(${num_binary_files} LESS 1)
        message(FATAL_ERROR "At least one file is required to pack")
    endif()

    add_library(${target} STATIC)

    set(DEPS_NAMESPACE ${target})

    set(deps_dest_folder "${CMAKE_CURRENT_BINARY_DIR}/${target}")

    file(MAKE_DIRECTORY ${deps_dest_folder})
    
	set(number_of_subdiv 100)
	
    # Assuming we'll get multiple cpp files due to division, adjust this to handle multiple files
    set(binary_file_names)
    foreach(index RANGE 1 ${number_of_subdiv})
        list(APPEND binary_file_names "${deps_dest_folder}/${DEPS_NAMESPACE}${index}.cpp")
    endforeach()
    list(APPEND binary_file_names "${deps_dest_folder}/${DEPS_NAMESPACE}.h")

    add_custom_command(OUTPUT ${binary_file_names}
        COMMAND ${DEPS_PACKER_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/DepsPacker --divide ${number_of_subdiv} ${deps_dest_folder} "${DEPS_NAMESPACE}" 
             ${DEPS_ARG_SOURCES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
        DEPENDS ${DEPS_ARG_SOURCES} DepsPacker
        VERBATIM)

    target_sources(${target} PRIVATE "${binary_file_names}")
    target_include_directories(${target} INTERFACE ${deps_dest_folder})
    target_compile_features(${target} PRIVATE cxx_std_20)

    # This fixes an issue where Xcode is unable to find binary data during archive.
    if(CMAKE_GENERATOR STREQUAL "Xcode")
        set_target_properties(${target} PROPERTIES ARCHIVE_OUTPUT_DIRECTORY "./")
    endif()
endfunction()