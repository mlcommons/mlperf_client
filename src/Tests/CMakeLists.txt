cmake_minimum_required(VERSION 3.15)

# Set the project name and version
project(Tests VERSION 1.0)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Define the source files for the library
set(SOURCES
	main.cpp
)

# Define the header files for the library
file(GLOB_RECURSE HEADERS "*.h")

# Define source groups
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Source files" FILES ${SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Header files" FILES ${HEADERS})

# Create a static library from the source files
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_COMMAND}")

target_compile_options(${PROJECT_NAME}
	PRIVATE
	$<$<CXX_COMPILER_ID:MSVC>:/w45038 /w45205 /w44061 /EHsc>
	$<$<CXX_COMPILER_ID:Clang>:-Wreorder -fexceptions>
)

add_dependencies(${PROJECT_NAME} CIL)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
	CIL
)

# Define a function to add a test to the project
function(add_test name)
	# Add the test to the project
	add_executable(${name} ${name}.cpp)
	include_directories(../CIL)
	include_directories(../../deps/gen-ai/)
	target_compile_options(${name}
		PRIVATE
		$<$<CXX_COMPILER_ID:MSVC>:/w45038 /w45205 /w44061 /EHsc>
		$<$<CXX_COMPILER_ID:Clang>:-Wreorder -fexceptions>
	)
	target_compile_features(${name} PRIVATE cxx_std_17)
	target_link_libraries(${name} PRIVATE CIL)
endfunction()

# Add the tests to the project
add_test(test_tfm_tokenizer)