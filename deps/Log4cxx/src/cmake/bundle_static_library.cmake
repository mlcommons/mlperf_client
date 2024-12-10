function(bundle_static_library bundled_target_name)
    set(extension_blocklist
        .dylib
        .so
        .dll
        .tbd
    )

    function(recursively_collect_static_dependencies input_target)
        if(NOT TARGET ${input_target})
            if(EXISTS ${input_target})
                get_filename_component(extension "${input_target}" EXT)
                if(NOT extension IN_LIST extension_blocklist)
                    list(APPEND static_libs "${input_target}")
                    set(static_libs ${static_libs} PARENT_SCOPE)
                endif()
            endif()

            return()
        endif()

        get_target_property(alias ${input_target} ALIASED_TARGET)
		
        if(TARGET ${alias})
            set(input_target ${alias})
        endif()

        get_property(library_already_added GLOBAL PROPERTY ${target_name}_static_bundle_${input_target})

        if(library_already_added)
            return()
        endif()

        set_property(GLOBAL PROPERTY ${target_name}_static_bundle_${input_target} ON)

        get_target_property(input_type ${input_target} TYPE)

        if(${input_type} STREQUAL "STATIC_LIBRARY")
            list(APPEND static_libs "$<TARGET_FILE:${input_target}>")
            get_target_property(dependencies ${input_target} LINK_LIBRARIES)
        elseif(${input_type} STREQUAL "INTERFACE_LIBRARY")
            get_target_property(dependencies ${input_target} INTERFACE_LINK_LIBRARIES)
        else()
            get_target_property(dependencies ${input_target} LINK_LIBRARIES)
        endif()
	
        if(dependencies)
            foreach(dependency IN LISTS dependencies)
                recursively_collect_static_dependencies(${dependency})
            endforeach()
        endif()

        set(static_libs ${static_libs} PARENT_SCOPE)
    endfunction()

    foreach(target_name IN ITEMS ${ARGN})
        recursively_collect_static_dependencies(${target_name})
    endforeach()

    list(REMOVE_DUPLICATES static_libs)
    set(static_libs ${static_libs} PARENT_SCOPE)
	
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/static_libs.txt.in)

    foreach(static_lib IN LISTS static_libs)
        file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/static_libs.txt.in "${static_lib}\n")
    endforeach()

    file(GENERATE
        OUTPUT static_libs.txt
        INPUT ${CMAKE_CURRENT_BINARY_DIR}/static_libs.txt.in)

    set(bundled_target_full_name
        ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}${bundled_target_name}${CMAKE_STATIC_LIBRARY_SUFFIX})
		
    if(MSVC)
        set(lib ${CMAKE_AR})
        add_custom_command(
            COMMAND ${lib} /NOLOGO /OUT:${bundled_target_full_name} ${static_libs}
            OUTPUT ${bundled_target_full_name}
            COMMENT "Bundling ${bundled_target_name}"
            VERBATIM)
    elseif(APPLE)
        find_program(libtool libtool)
        add_custom_command(
            COMMAND ${libtool} -static -o ${bundled_target_full_name} ${static_libs}
            OUTPUT ${bundled_target_full_name}
            COMMENT "Bundling ${bundled_target_name}"
            VERBATIM)
    endif()

    add_custom_target(bundling_${bundled_target_name} ALL DEPENDS ${bundled_target_full_name})

    foreach(target_name IN ITEMS ${ARGN})
        if(TARGET ${target_name})
            add_dependencies(bundling_${bundled_target_name} ${target_name})
        endif()
    endforeach()

    add_library(${bundled_target_name} STATIC IMPORTED GLOBAL)
    set_target_properties(${bundled_target_name}
        PROPERTIES
        IMPORTED_LOCATION ${bundled_target_full_name})

    foreach(target_name IN ITEMS ${ARGN})
        set_property(TARGET ${bundled_target_name} APPEND
            PROPERTY INTERFACE_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:${target_name},INTERFACE_INCLUDE_DIRECTORIES>)
        set_property(TARGET ${bundled_target_name} APPEND
            PROPERTY INTERFACE_COMPILE_DEFINITIONS $<TARGET_PROPERTY:${target_name},INTERFACE_COMPILE_DEFINITIONS>)
    endforeach()

    add_dependencies(${bundled_target_name} bundling_${bundled_target_name})
endfunction()
