cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0135 NEW)

# Set the project name and version
project(IHV_MLX VERSION 1.0)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include(ExternalProject)

message(STATUS "Building with IHV MLX support...")

# Define directories for MLX
set(IHV_MLX_BINARY_DIR ${CMAKE_BINARY_DIR}/IHV/MLX)
set(IHV_MLX_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(IHV_MLX_INSTALL_DIR ${CMAKE_BINARY_DIR}/IHV/MLX/bin)

ExternalProject_Add(ihv_mlx_source
    SOURCE_DIR ${IHV_MLX_SOURCE_DIR}
    BINARY_DIR ${IHV_MLX_BINARY_DIR}
    INSTALL_DIR ${IHV_MLX_INSTALL_DIR}
    BUILD_ALWAYS ON
    
    # Custom configuration command
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E echo "Configuring IHV MLX for ${CMAKE_BUILD_TYPE}"
    COMMAND ${CMAKE_COMMAND} -B ${IHV_MLX_BINARY_DIR} -S ${IHV_MLX_SOURCE_DIR}
        -G "${CMAKE_GENERATOR}"
        -DCMAKE_INSTALL_PREFIX=${IHV_MLX_INSTALL_DIR}
        -DCMAKE_CXX_STANDARD=20
        -DENV_PATH=${ENV_PATH}
        -DIOS=${IOS}
        -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
        $<$<BOOL:${IOS}>:
        -DXCODE_ATTRIBUTE_CODE_SIGN_IDENTITY=
        -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=ON
        >
    
    # Custom build command
    BUILD_COMMAND ${CMAKE_COMMAND} -E echo "Starting IHV MLX build for ${CMAKE_BUILD_TYPE}..."
    COMMAND ${CMAKE_COMMAND} --build ${IHV_MLX_BINARY_DIR} --config $<CONFIG>
    
    # Custom install command
    INSTALL_COMMAND ${CMAKE_COMMAND} --install ${IHV_MLX_BINARY_DIR} --config $<CONFIG> --prefix ${IHV_MLX_INSTALL_DIR}/$<CONFIG>
    LOG_CONFIGURE ON
    LOG_BUILD ON
    LOG_INSTALL ON
)

set_target_properties(ihv_mlx_source PROPERTIES FOLDER "IHV")