string(REPLACE " " ";" FILES_TO_HASH "${FILES_TO_HASH}")

set(temp_file "${OUTPUT_FILE}.tmp")
file(WRITE "${temp_file}" "{\n")

list(LENGTH FILES_TO_HASH LIST_LENGTH)
math(EXPR LAST_INDEX "${LIST_LENGTH} - 1")

foreach(INDEX RANGE ${LAST_INDEX})
    list(GET FILES_TO_HASH ${INDEX} file_to_hash)
    file(SHA256 ${file_to_hash} HASH)
    get_filename_component(FILE_NAME ${file_to_hash} NAME)
    if(NOT ${INDEX} EQUAL ${LAST_INDEX})
        file(APPEND "${temp_file}" " \"${HASH}\": \"${FILE_NAME}\",\n")
    else()
        file(APPEND "${temp_file}" " \"${HASH}\": \"${FILE_NAME}\"\n")
    endif()
endforeach()

file(APPEND "${temp_file}" "}\n")


# Only update if content changed
message(STATUS "Output file: ${OUTPUT_FILE}")

if(EXISTS "${OUTPUT_FILE}")
    file(READ "${temp_file}" TEMP_CONTENTS UTF-8)
    file(READ "${OUTPUT_FILE}" CURRENT_CONTENTS UTF-8)
    if(NOT "${TEMP_CONTENTS}" STREQUAL "${CURRENT_CONTENTS}")
        message(STATUS "Changes detected in the generated hash file")
        file(RENAME "${temp_file}" "${OUTPUT_FILE}")
    else()
        message(STATUS "No changes detected in the generated hash file")
        file(REMOVE "${temp_file}")
    endif()
else()
    message(STATUS "No existing hash file found, creating a new one")
    file(RENAME "${temp_file}" "${OUTPUT_FILE}")
endif()