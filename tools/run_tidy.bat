@echo off
setlocal EnableDelayedExpansion
:: This script runs clang-tidy on all source files in the project.
set INITIAL_DIR=%CD%

set BUILD_DIR=./build-ninja
set SRC_DIR=./src
set ALL_FILES=no
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"


set CMAKE_FLAGS=-DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DMLPERF_ONNX_USE_OPENVINO:BOOL="0" -DVERBOSE_CONFIGURE=ON -DHTTPLIB_USE_ZLIB_IF_AVAILABLE:BOOL="1"
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd "%INITIAL_DIR%"
echo Configuring the project with CMake for Ninja build system...
cmake -G Ninja -S "%INITIAL_DIR%" -B "%BUILD_DIR%" "%CMAKE_FLAGS%"



:: Check if CMake configuration succeeded
if %ERRORLEVEL% neq 0 (
    echo CMake configuration failed with error %ERRORLEVEL%.
    exit /b %ERRORLEVEL%
)

:: display the configuration
echo Build directory: %BUILD_DIR%
echo Source directory: %SRC_DIR%
echo All files option enabled: %ALL_FILES%

:: delete clang-tidy output file
if exist clang-tidy-output.txt del /f clang-tidy-output.txt
:: Ensure that assets.h file exists in the specified path
set TARGET_ASSETS_DIR=%BUILD_DIR%/src/CIL/assets
set MAIN_ASSETS_HEADER_FILE_PATH=%TARGET_ASSETS_DIR%/assets.h

if not exist %MAIN_ASSETS_HEADER_FILE_PATH% (
    echo Adding missing assets files to : %TARGET_ASSETS_DIR%
    echo Extracting assets file...
    tar -xzvf  "./tools/resources/assets.tar.gz" -C  "%TARGET_ASSETS_DIR%"
    :: Check if the tar command failed
    if errorlevel 1 (
        echo Failed to extract the assets file. Please check the tar command.
        exit /b 1
    )
)
:: get changed files
set "FILES="
if "%ALL_FILES%" == "yes" (
    for /r "%SRC_DIR%" %%i in (*.cpp) do (
        set "FILES=!FILES! "%%i""
    )
    echo Formatting all files...
    for /r "%SRC_DIR%" %%i in (*.cpp, *.c, *.h, *.hpp) do (
        clang-format -style=Google -i "%%i"
    )
) else (
    for /f "delims=" %%i in ('git diff --name-only HEAD -- src\*.cpp src\*.h src\*.hpp' ) do (
        if "%%~xi"==".cpp" (
            set "FILES=!FILES! "%%i""
        )
        clang-format -style=Google -i "%%i"
    )
    
)
:: run clang-tidy
echo.
echo Running clang-tidy...
set ERROR_COUNT=0
set FILE_COUNT=0
set PASSED_FILES=0
for %%f in (!FILES!) do (
    set /a FILE_COUNT+=1
    if exist "%%f" (
        echo Analyzing file: %%f
        clang-tidy -p "%BUILD_DIR%" "%%f" --quiet --config-file "%SRC_DIR%\.clang-tidy" >> clang-tidy-output.txt
        if errorlevel 1 (
            set /a ERROR_COUNT+=1
            echo Error in file: %%f
        ) else (
            echo OK
            set /a PASSED_FILES+=1
        )
    )
)
echo.
echo __________________ Clang-tidy results __________________

echo Files analyzed: %FILE_COUNT%
echo Files passed: %PASSED_FILES%
echo Build and analysis completed with %ERROR_COUNT% errors found.

: Handle no files analyzed case
if %FILE_COUNT% equ 0 (
    echo No files were analyzed.
    goto end
)

:: All files failed
if %PASSED_FILES% equ 0 (
    echo All files failed to pass the clang-tidy check or were not analyzed. Please check the logs for more details.
    goto end
)

:: errors found
if %ERROR_COUNT% gtr 0 (
    echo.
    echo Please check clang-tidy-output.txt for more details.
    echo Note: please remember to compile the project before running clang-tidy.
    goto end
)

:: Some files were not analyzed
if %PASSED_FILES% neq %FILE_COUNT% (
    echo.
    echo Some files were not analyzed. Please check the output file for more details.
)

:end




