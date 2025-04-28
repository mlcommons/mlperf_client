# MLPerf Client benchmark

The MLPerf Client is designed to be a single executable. This is achieved by:

- Using a custom script to compile Log4cxx as a static library and obtain all the necessary dependencies.
- Using a custom script to build OpenSSL as a static library and obtain all the necessary dependencies.
- Packaging the libraries into a single executable and extracting them during runtime. Downloading necessary dependencies for them to work.

**Note:**
> The repository includes pre-built libraries for Windows and macOS, eliminating the need to build them yourself.

However, if you wish to build the libraries, the following scripts are available in the tools folder:

- Use 'build_log4cxx.py' to create the Log4cxx.
- To build OpenSSL, run 'build_openssl.py'.

**Note:**
> GUI work is in progress

## Table of Contents
- [MLPerf Client benchmark](#mlperf-client-benchmark)
  - [Table of Contents](#table-of-contents)
  - [Prerequisites](#prerequisites)
    - [macOS](#macos)
      - [macOS CLI Mode](#macos-cli-mode)
    - [Windows](#windows)
      - [Windows CLI Mode](#windows-cli-mode)
  - [Building the Application](#building-the-application)
    - [Common Steps](#common-steps)
    - [macOS Build Instructions](#macos-build-instructions)
      - [macOS CLI Build](#macos-cli-build)
    - [Windows Build Instructions](#windows-build-instructions)
      - [Windows CLI Build](#windows-cli-build)
  - [Build Flags and Options](#build-flags-and-options)
    - [CLI Flags](#cli-flags)

## Prerequisites
### macOS
#### macOS CLI Mode
- Xcode
- Make sure to install the following components:
  - Command Line Tools for Xcode
  - CMake
  - ZSH

```bash
brew install llvm autoconf automake libtool
echo 'export PATH="/usr/local/opt/llvm/bin:/opt/homebrew/opt/llvm/bin:$PATH"' >> ~/.zshrc
source ~/.zshrc
```

### Windows
#### Windows CLI Mode
- Visual Studio 2022 with C++ Desktop Development workload.
- Ensure that Visual Studio 2022 is configured to use the Microsoft Visual C++ (MSVC) compiler.
- make sure to install the following components:
  - Desktop development with C++
  - MSVC v143 - VS 2022 C++ x64/x86 or ARM64/ARM64EC build tools
  - CMake
- Add to the path the msbuild and cmake binaries i.e. `C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin` and `C:\Program Files\CMake\bin`
- Ensure clang-tidy is available:
  - Option 1: Install LLVM binaries from https://releases.llvm.org/download.html. During installation, select "Add LLVM to the system PATH..." in the options page. This allows CMake to automatically find clang-tidy.
  - Option 2: As an alternative to LLVM, you can install Conda from https://conda.io/projects/conda/en/latest/user-guide/install/index.html. After installing Conda, use it to install clang tools from https://anaconda.org/conda-forge/clang-tools. Then, ensure you run CMake from within the Conda environment; this setup helps resolve any configuration errors by ensuring CMake recognizes the clang tools.

## Building the Application

### Common Steps
1. Open a terminal (macOS) or command prompt (Windows).

2. Clone the repository and navigate to the directory:
   ```
   git clone --recurse-submodules repository-url
   cd repository
   ```
### macOS Build Instructions

#### macOS CLI Build

1. Generate the build configuration with CMake
    ```bash
    mkdir -p "build"
    export MACOSX_DEPLOYMENT_TARGET=11.5
    cmake -G "Xcode" -S . -B "build"
    cd build
    xcodebuild -arch arm64 -scheme CLI -configuration Debug -verbose
    ```
2. Run the application
The application will be located in `Bin/MacOS/ARM/Debug/mlperf-macos`.

### Windows Build Instructions

#### Windows CLI Build

1. Generate the build configuration with CMake
    ```bash
    mkdir -p "build"
    cmake -G "Visual Studio 17 2022" -A x64 -S . -B "build" # Use -A ARM64 for ARM64 target
    cd build
    msbuild mlperf.sln -p:Configuration=Debug -ds:True -v:diag
    ```
   **Note:**
   > Cross-compilation is supported, allowing you to build for ARM64 on an x64 machine and vice versa. Adjust the -A argument accordingly. If the -A argument is not provided, CMake will default to the architecture of the host machine.
2. Run the application
The application will be located in `Bin\Windows\x64\Debug\mlperf-windows.exe`.

## Build Flags and Options
The following table shows the available flags that can be used to control the build process:

### CLI Flags

We support many IHV EPs, Each IHV EP has its own detailed documentation:
- [NativeOpenVINO](src/CIL/IHV/NativeOpenVINO/src/README.md)
- [OrtGenAI](src/CIL/IHV/OrtGenAI/src/README.md)
and more IHV EPs can be found in the [src/CIL/IHV](src/cil/IHV) directory.
