cmake_minimum_required(VERSION 3.15)
project(GUI)

set(CMAKE_AUTOUIC ON)  
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 COMPONENTS Widgets Svg PdfWidgets REQUIRED)

if (APPLE)
    set(MLPERF_MAC_BUNDLE_NAME "MLPerf Client" CACHE STRING "Bundle name")
    set(MLPERF_MAC_BUNDLE_IDENTIFIER "org.mlcommons.mlperf" CACHE STRING "Bundle identifier")
    set(XCODE_SIGN_REQUIRED "NO")
    set(XCODE_SIGN_ALLOWED "NO")
    set(MLPERF_XCODE_SIGN_IDENTITY "")
    set(MLPERF_XCODE_DEVELOPMENT_TEAM "")
endif()

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
file(GLOB_RECURSE UI_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.ui")

if (NOT IOS)
    list(FILTER HEADERS EXCLUDE REGEX "ios_utils.h")
endif()

if(APPLE)
    # Include .m and .mm files only for Apple
    file(GLOB_RECURSE MM_SOURCES "*.m" "*.mm")
    if (NOT IOS)
        list(FILTER MM_SOURCES EXCLUDE REGEX "ios_utils.mm")
    endif()
    list(APPEND SOURCES ${MM_SOURCES})
endif()

qt_wrap_ui(UI_HEADERS ${UI_FILES})
qt6_wrap_cpp(MOC_SOURCES ${HEADERS})
qt6_add_resources(RESOURCES app_resources.qrc)

# Define source groups
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Source Files" FILES ${SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Header Files" FILES ${HEADERS})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Form Files" FILES ${UI_FILES})
source_group("Generated Files" FILES ${MOC_SOURCES} ${UI_HEADERS} ${RESOURCES})
set_property(GLOBAL PROPERTY AUTOGEN_SOURCE_GROUP "Generated Files")

# Add the executable
if(MSVC)
    set(app_icon_resource_windows "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.rc")
    add_executable(GUI WIN32 ${SOURCES} ${HEADERS} ${MOC_SOURCES} ${UI_HEADERS} ${RESOURCES} ${app_icon_resource_windows})
    set_target_properties(GUI PROPERTIES
        LINK_FLAGS_DEBUG "/SUBSYSTEM:CONSOLE"
        LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS"
        LINK_FLAGS_RELWITHDEBINFO "/SUBSYSTEM:WINDOWS"
    )
else()
    add_executable(GUI ${SOURCES} ${HEADERS} ${MOC_SOURCES} ${UI_HEADERS} ${RESOURCES})
endif()

# Link Qt libraries
target_link_libraries(GUI PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg Qt6::PdfWidgets CIL)
if(APPLE)
    target_link_libraries(GUI PRIVATE IOReport)
    if (IOS)
        set(FRAMEWORKS_LIST "")
        if(MLPERF_IHV_GGML_METAL)
            get_property(EP_IHVS_GGML_LIB GLOBAL PROPERTY EP_IHVS_GGML_LIB)
            string(REPLACE "$<CONFIG>" "$(CONFIGURATION)" EP_IHVS_GGML_LIB_XCODE_FMT "${EP_IHVS_GGML_LIB}")
            list(APPEND FRAMEWORKS_LIST ${EP_IHVS_GGML_LIB_XCODE_FMT})

            get_property(EP_IHVS_GGML_METAL_LIB GLOBAL PROPERTY EP_IHVS_GGML_METAL_LIB)
            string(REPLACE "$<CONFIG>" "$(CONFIGURATION)" EP_IHVS_GGML_METAL_LIB_XCODE_FMT "${EP_IHVS_GGML_METAL_LIB}")
            list(APPEND FRAMEWORKS_LIST ${EP_IHVS_GGML_METAL_LIB_XCODE_FMT})
        endif()
        if(MLPERF_IHV_MLX)
            get_property(EP_IHVS_MLX_LIB GLOBAL PROPERTY EP_IHVS_MLX_LIB)
            string(REPLACE "$<CONFIG>" "$(CONFIGURATION)" EP_IHVS_MLX_LIB_XCODE_FMT "${EP_IHVS_MLX_LIB}")
            list(APPEND FRAMEWORKS_LIST ${EP_IHVS_MLX_LIB_XCODE_FMT})

            get_property(MLX_METALLIB GLOBAL PROPERTY MLX_METALLIB)        
            set(MLX_METALLIB_DEST "$<TARGET_FILE_DIR:GUI>/Frameworks/IHV_MLX.framework/default.metallib")
            add_custom_command(
            TARGET GUI POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different 
            "${MLX_METALLIB}" "${MLX_METALLIB_DEST}" 
            COMMENT "Copying MLX metallib into the app bundle"
            )
        endif()

        list(JOIN FRAMEWORKS_LIST ";" XCODE_EMBED_FRAMEWORKS_LIST)
        set_target_properties(GUI PROPERTIES XCODE_EMBED_FRAMEWORKS_CODE_SIGN_ON_COPY ON XCODE_EMBED_FRAMEWORKS "${XCODE_EMBED_FRAMEWORKS_LIST}")
    endif()
endif()
target_compile_definitions(GUI PUBLIC "EULA_ENABLED=1")

include_directories(include ../cil/)
find_package(Log4Cxx REQUIRED)

# Clear diagnostics for Log4CXX
message(STATUS "LOG4CXX_INCLUDE_DIR: ${LOG4CXX_INCLUDE_DIR}")
message(STATUS "LOG4CXX_LIB_DIR: ${LOG4CXX_LIB_DIR}")

# Include Log4CXX headers
target_include_directories(GUI PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ui ${LOG4CXX_INCLUDE_DIR})

# Define the Log4CXX library path - using same pattern as CIL
set(LOG4CXX_LIBRARY ${LOG4CXX_LIB_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}log4cxx${CMAKE_STATIC_LIBRARY_SUFFIX})
message(STATUS "LOG4CXX_LIBRARY: ${LOG4CXX_LIBRARY}")

# Check if library exists
if(EXISTS "${LOG4CXX_LIBRARY}")
  message(STATUS "Log4CXX library file exists")
else()
  message(WARNING "Log4CXX library file DOES NOT exist: ${LOG4CXX_LIBRARY}")
endif()

# Add static definition to use Log4CXX as a static library
target_compile_definitions(GUI PRIVATE LOG4CXX_STATIC)

# Don't link log4cxx directly since it's already linked through CIL
# target_link_libraries(GUI PRIVATE ${LOG4CXX_LIBRARY})

# Special handling for Windows ARM64 build
if(MSVC AND CMAKE_GENERATOR_PLATFORM MATCHES "ARM64")
  message(STATUS "Adding special handling for Windows ARM64 build")
  
  # Don't use WHOLEARCHIVE for log4cxx since it's already linked via CIL
  # target_link_options(GUI PRIVATE "/WHOLEARCHIVE:${LOG4CXX_LIBRARY}")
  
  # Enable verbose linking to debug issues
  set_property(TARGET GUI PROPERTY LINK_FLAGS_RELEASE "/VERBOSE:LIB")
  
  # Make sure we're linking statically with the C runtime
  set_property(TARGET GUI PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

if (WIN32)
    set_target_properties(GUI PROPERTIES OUTPUT_NAME "mlperf-windows-gui")
    # Use windeployqt to copy the required Qt libraries
    add_custom_command(TARGET GUI POST_BUILD
        COMMAND ${WINDEPLOYQT_EXECUTABLE} --release --no-translations $<TARGET_FILE:GUI>)
else()
    # https://doc.qt.io/qt-6/macos-deployment.html
    # https://cmake.org/cmake/help/latest/prop_tgt/MACOSX_BUNDLE_INFO_PLIST.html
    set(app_icon_macos "${CMAKE_CURRENT_SOURCE_DIR}/resources/AppIcon.icns")
    set_source_files_properties(${app_icon_macos} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")

    get_property(APP_VERSION_STRING GLOBAL PROPERTY APP_VERSION_STRING)

    if(IOS)
        file(READ "${CMAKE_SOURCE_DIR}/version.json" VERSION_JSON)
        string(JSON MAJOR_VERSION GET ${VERSION_JSON} major)
        string(JSON MINOR_VERSION GET ${VERSION_JSON} minor)
        string(JSON PATCH_VERSION GET ${VERSION_JSON} patch)
        string(JSON VERSION_SUFFIX GET ${VERSION_JSON} suffix)
        
        set(IOS_SHORT_VERSION_STRING "${MAJOR_VERSION}.${MINOR_VERSION}.${PATCH_VERSION}")
        set(IOS_BUNDLE_VERSION "${MAJOR_VERSION}.${MINOR_VERSION}.${PATCH_VERSION}")
        
        set(GITHUB_RUN_ID "$ENV{GITHUB_RUN_ID}")

        if(NOT GITHUB_RUN_ID STREQUAL "")
            set(IOS_BUNDLE_VERSION "${GITHUB_RUN_ID}")
        endif()
        
        message(STATUS "iOS Short Version String: ${IOS_SHORT_VERSION_STRING}")
        message(STATUS "iOS Bundle Version: ${IOS_BUNDLE_VERSION}")
    endif()
    
    set_target_properties(GUI PROPERTIES 
        OUTPUT_NAME "mlperf-macos-gui" 
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_BUNDLE_NAME "${MLPERF_MAC_BUNDLE_NAME}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "${MLPERF_MAC_BUNDLE_IDENTIFIER}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${APP_VERSION_STRING}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${APP_VERSION_STRING}"
        MACOSX_BUNDLE_LONG_VERSION_STRING "${APP_VERSION_STRING}"
        MACOSX_BUNDLE_ICON_FILE "AppIcon.icns"
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${MLPERF_XCODE_DEVELOPMENT_TEAM}"
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "${MLPERF_XCODE_SIGN_IDENTITY}"
        XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "${XCODE_SIGN_REQUIRED}"
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "${XCODE_SIGN_ALLOWED}"
    )

    if(IOS)
        set(ios_assets_catalog "${CMAKE_CURRENT_SOURCE_DIR}/ios/Assets.xcassets")
        set(ios_launch_screen "${CMAKE_CURRENT_SOURCE_DIR}/ios/LaunchScreen.storyboard")
        
        set_source_files_properties(${ios_assets_catalog} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
        set_source_files_properties(${ios_launch_screen} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
        
        set_target_properties(GUI PROPERTIES 
            OUTPUT_NAME "mlperf_ios_gui"
            XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "2"
            MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/ios/Info.plist.app.in
            XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS ${CMAKE_CURRENT_SOURCE_DIR}/ios/mlperf-ios.entitlements
            XCODE_ATTRIBUTE_INSTALL_PATH "/Applications"
            MACOSX_BUNDLE_BUNDLE_VERSION "${IOS_BUNDLE_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${IOS_SHORT_VERSION_STRING}"
            MACOSX_BUNDLE_LONG_VERSION_STRING "${IOS_SHORT_VERSION_STRING}"
            MACOSX_BUNDLE_ICON_FILE "AppIcon"
            XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
        )
        
        target_sources(GUI PRIVATE ${ios_assets_catalog} ${ios_launch_screen})
        install(TARGETS GUI DESTINATION Applications)
    endif()
    target_sources(GUI PRIVATE ${app_icon_macos})
endif()

