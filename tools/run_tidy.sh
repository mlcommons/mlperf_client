#!/bin/bash

BUILD_DIR="./build-ninja"
SRC_DIR="./src"
ALL_FILES="no"

# Define directories
INITIAL_DIR=$(pwd)

# Ensure Ninja build directory exists
if [ ! -d "${BUILD_NINJA_DIR}" ]; then
    mkdir -p "${BUILD_NINJA_DIR}"
fi

# Configure CMake for Ninja Build System
echo "Configuring CMake for Ninja Build..."
cmake -G Ninja \
      -S "${INITIAL_DIR}" \
      -B "${BUILD_DIR}" \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
      -DMLPERF_ONNX_USE_OPENVINO:BOOL="0" \
      -DVERBOSE_CONFIGURE=ON \
      -DCMAKE_CXX_COMPILER=clang++ \
      -DCMAKE_C_COMPILER=clang \
      -DHTTPLIB_USE_ZLIB_IF_AVAILABLE:BOOL="1"
      

# Display the configuration
echo "Build directory: $BUILD_DIR"
echo "Source directory: $SRC_DIR"
echo "All files option enabled: $ALL_FILES"

# Delete clang-tidy output file
if [ -f clang-tidy-output.txt ]; then
    rm -f clang-tidy-output.txt
fi

# Ensure that assets.h file exists in the specified path
TARGET_ASSETS_DIR="$BUILD_DIR/src/CIL/assets"
MAIN_ASSETS_HEADER_FILE_PATH="$TARGET_ASSETS_DIR/assets.h"

if [ ! -f "$MAIN_ASSETS_HEADER_FILE_PATH" ]; then
    echo "Adding missing assets files to: $TARGET_ASSETS_DIR"
    tar -xf ./tools/resources/assets.rar -C "$TARGET_ASSETS_DIR"
    if [ $? -ne 0 ]; then
        echo "Failed to extract the assets file. Please check the tar command."
        exit 1
    fi
fi

# Get changed files
FILES=()
if [ "$ALL_FILES" == "yes" ]; then
    echo "Formatting all files..."
    while IFS= read -r file; do
        if [[ "$file" == *.cpp ]]; then
            FILES+=("$file")
        fi
        clang-format -style=Google -i "$file"
    done < <(find "$SRC_DIR" -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \))
    
else
    while IFS= read -r line; do
        if [[ "$line" == *.cpp ]]; then
            FILES+=("$line")
        fi
        clang-format -style=Google -i "$line"
    done < <(git diff --name-only HEAD -- "$SRC_DIR"/*.cpp "$SRC_DIR"/*.h "$SRC_DIR"/*.hpp)
fi

echo
echo "_________________ Files to be analyzed _________________"
for f in "${FILES[@]}"; do
    echo "$f"
done
echo
echo "Total files: ${#FILES[@]}"
echo "_______________________________________________________"

echo
echo "_________________ Clang-tidy analysis __________________"
ERROR_COUNT=0
FILE_COUNT=${#FILES[@]}
PASSED_FILES=0
for f in "${FILES[@]}"; do
    if [ -f "$f" ]; then
        echo "Analyzing file: $f"
        if clang-tidy -p "$BUILD_DIR" "$f" --quiet --config-file "$SRC_DIR/.clang-tidy" >> clang-tidy-output.txt; then
            echo "OK"
            ((PASSED_FILES++))
        else
            echo "Error in file: $f"
            ((ERROR_COUNT++))
        fi
    fi
done

echo
echo "_________________ Clang-tidy results __________________"
echo "Files analyzed: $FILE_COUNT"
echo "Files passed: $PASSED_FILES"
echo "Build and analysis completed with $ERROR_COUNT errors found."

if [ $FILE_COUNT -eq 0 ]; then
    echo "No files were analyzed."
    elif [ $PASSED_FILES -eq 0 ]; then
    echo "All files failed to pass the clang-tidy check or were not analyzed. Please check the logs for more details."
    elif [ $ERROR_COUNT -gt 0  ]; then
    echo
    echo "Please check clang-tidy-output.txt for more details."
    echo "Note: please remember to compile the project before running clang-tidy."
    elif [ $PASSED_FILES -ne $FILE_COUNT ]; then
    echo
    echo "some files were not analyzed. Please check the output file for more details."
fi