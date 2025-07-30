set(TOKENIZER_ROOT ${CMAKE_CURRENT_SOURCE_DIR} PARENT_SCOPE)

set(CMAKE_CXX_STANDARD 17)
if(MSVC)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++17")
endif(MSVC)

file(GLOB tokenizer_srcs CONFIGURE_DEPENDS
   "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
   "${CMAKE_CURRENT_SOURCE_DIR}/utils/*.cc"
   "${CMAKE_CURRENT_SOURCE_DIR}/c_api/*.cc"
)

FetchContent_Declare(GSL URL https://github.com/microsoft/GSL/archive/refs/tags/v4.0.0.zip)
FetchContent_MakeAvailable(GSL)

FetchContent_Declare(simdjson URL https://github.com/simdjson/simdjson/archive/refs/tags/v3.12.3.zip URL_HASH SHA1=5e123e1b65546f7a65fe8030722a4c2ca975c9ce)
FetchContent_MakeAvailable(simdjson)

add_library(tokenizer STATIC ${tokenizer_srcs})

message(STATUS "GSL_SOURCE_DIR: ${GSL_SOURCE_DIR}")
target_include_directories(tokenizer PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}
   ${GSL_SOURCE_DIR}/include
   ${simdjson_SOURCE_DIR}/singleheader/
)
target_compile_definitions(tokenizer PRIVATE _SILENCE_ALL_CXX20_DEPRECATION_WARNINGS)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(tokenizer PRIVATE stdc++fs)
  set_target_properties(tokenizer PROPERTIES POSITION_INDEPENDENT_CODE ON)
  set_target_properties(simdjson PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

target_link_libraries(tokenizer PUBLIC simdjson)