include(FetchContent)

FetchContent_Declare(
    dylib
    GIT_REPOSITORY "https://github.com/martin-olivier/dylib.git"
    GIT_TAG "v2.2.1"
)

FetchContent_GetProperties(dylib)
if(NOT dylib_POPULATED)
    FetchContent_MakeAvailable(dylib)
endif()