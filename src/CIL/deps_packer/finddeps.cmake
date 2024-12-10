cmake_minimum_required(VERSION 3.15)

message(STATUS "Looking for ${deps_suffix} deps in: ${deps_dir}")

file(GLOB DEPS_FILES "${deps_dir}/*${deps_suffix}")

foreach(file ${extra_deps})
    if(EXISTS "${file}")
        list(APPEND DEPS_FILES "${file}")
    else()
        message(FATAL_ERROR "File ${file} does not exist and can't be packed!")
    endif()
endforeach()

if(DEPS_FILES)

execute_process(
	COMMAND ${CMAKE_COMMAND} -E tar cfv "${zip_path}" --format=zip ${DEPS_FILES}
	RESULT_VARIABLE zip_result
	OUTPUT_VARIABLE zip_output
	ERROR_VARIABLE zip_error
	WORKING_DIRECTORY "${deps_dir}"
)

if(NOT zip_result EQUAL "0")
	message(FATAL_ERROR "Failed to create zip:${zip_cmd} \n Error: ${zip_result}")
else()
	message(STATUS "Deps zipped successfully: ${zip_path}")
endif()
else()
get_filename_component(parentDir "${zip_path}" DIRECTORY)
set(EMPTY_FILE_PATH "${parentDir}/no_deps.txt")
file(TOUCH "${EMPTY_FILE_PATH}")
file(ARCHIVE_CREATE OUTPUT "${zip_path}" PATHS "${EMPTY_FILE_PATH}" FORMAT zip)
file(REMOVE "${EMPTY_FILE_PATH}")

message(WARNING "No deps files found in: ${deps_dir}")
endif()