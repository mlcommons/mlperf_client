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


## Table of Contents
- [MLPerf Client benchmark](#mlperf-client-benchmark)
  - [Table of Contents](#table-of-contents)
  - [Prerequisites](#prerequisites)
    - [macOS](#macos)
      - [macOS CLI Mode](#macos-cli-mode)
      - [macOS GUI Mode](#macos-gui-mode)
    - [iOS](#ios)
      - [iOS CLI-Like Mode](#ios-cli-like-mode)
      - [iOS GUI Mode](#ios-gui-mode)
    - [Windows](#windows)
      - [Windows CLI Mode](#windows-cli-mode)
      - [Windows GUI Mode](#windows-gui-mode)
  - [Building the Application](#building-the-application)
    - [Common Steps](#common-steps)
    - [macOS Build Instructions](#macos-build-instructions)
      - [macOS CLI Build](#macos-cli-build)
      - [macOS GUI Build](#macos-gui-build)
    - [iOS Build Instructions](#ios-build-instructions)
      - [iOS CLI-Like Build](#ios-cli-like-build)
      - [iOS GUI Build](#ios-gui-build)
    - [Windows Build Instructions](#windows-build-instructions)
      - [Windows CLI Build](#windows-cli-build)
      - [Windows GUI Build](#windows-gui-build)
      - [Windows CLI Build for IHV\_NATIVE\_QNN](#windows-cli-build-for-ihv_native_qnn)
  - [Build Flags and Options](#build-flags-and-options)
    - [CLI Flags](#cli-flags)
    - [GUI Flags](#gui-flags)

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

#### macOS GUI Mode
To build the application in GUI mode on macOS, you need to have the latest Qt 6.8.0 installed. Follow these steps:

1. Install Qt 6.8.0:
   - Download and install Qt 6.8.0 from the [official Qt website](https://www.qt.io/download).
   - During the installation process, select the following:
     - Qt 6.8.0 and add
       - Desktop

### iOS
#### iOS CLI-Like Mode
We support a simple iOS app with an output interface similar to the CLI. The prerequisites listed under [macOS CLI Mode](#macos-cli-mode) also apply to iOS. Additionally, ensure that the iOS SDK is installed in Xcode.

#### iOS GUI Mode
The prerequisites listed under [macOS GUI Mode](#macos-gui-mode) also apply to iOS. Additionally, make sure to include the iOS component during the Qt installation process.

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

#### Windows GUI Mode
To build the application in GUI mode on Windows, you need to have the latest Qt 6.9.0 installed. Follow these steps:
1. Install Qt 6:
   - Required Version: Qt 6.9.0.
   - Official Download / Installation [official Qt website](https://www.qt.io/download):

     You can use the official precompiled binaries of the library for Release builds, no need to compile it yourself, but for debug mode you will need to build it from source and configure the runtime flags correctly.
	   ##### Build Instructions & Requirements
	   - Step 1: Build and Install Qt

	        Please carefully follow the official build instructions provided here:
		    [Building Qt 6 from Git](https://wiki.qt.io/Building_Qt_6_from_Git)
	        During the build setup, you must change the default runtime flags to MD for release mode and MT in debug mode.
			The command can be like 
			```shell
			"%SRC_DIR%\configure.bat" ^
		    -prefix "%INSTALL_DIR%" ^
		    -opensource ^
		    -confirm-license ^
		    -nomake examples ^
		    -nomake tests ^
		    -skip qtwebengine ^
			-platform win32-msvc ^
			-- ^
		    -DCMAKE_CXX_FLAGS_RELEASE="/MD" ^
		    -DCMAKE_C_FLAGS_RELEASE="/MD" ^
		    -DCMAKE_CXX_FLAGS_DEBUG="/MT" ^
		    -DCMAKE_C_FLAGS_DEBUG="/MT"
			```
		 It is critical at this point to skip 'qtwebengine' because it is based on Chromium and requires shared runtime libraries to function properly. You can optimize the build by bypassing the repositories that are not necessary for the build like(`qtbluetooth`, `qtnfc`, `-qtcharts`, `qtlocation`, ...)
		 
	  - Step 2 Install QtPDF

	     Please carefully adhere to the official build instructions provided here:
		 [QtPDF Build Instructions](https://wiki.qt.io/QtPDF_Build_Instructions)
		 QtPdf module are hosted within QtWebEngine repository so we need to build it without building the QtWebEngine, so we will add this flag to disable it 
		 > -DFEATURE_qtwebengine_build=OFF

		 Like in the previous step, we must also modify the runtime flags as specified in the instructions.
2. Install Qt Visual Studio Tools:
   - Open Visual Studio 2022
   - Go to Extensions -> Manage Extensions
   - Search for "Qt Visual Studio Tools" and install it
   - Also search for and install the "Qt VS Cmake Tools" extension
   - Restart Visual Studio after installation.
These steps will ensure you have all the necessary tools and components to build the application in GUI mode on Windows.

## Building the Application

### Common Steps
1. Open a terminal (macOS) or command prompt (Windows).

2. Clone the repository and navigate to the directory:
   ```
   git clone --recurse-submodules https://github.com/mlcommons/mlperf_client_dev.git
   cd mlperf_client_dev
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
The application will be located in `Bin/MacOS/Debug/mlperf-macos`.

#### macOS GUI Build
1. Generate the build configuration with CMake
    ```bash
    mkdir -p "build"
    cmake -G "Xcode" -S . -B "build" -DMLPERF_BUILD_GUI=ON -DCMAKE_PREFIX_PATH="/Users/user/Qt/6.8.0/macos"
    cd build
    xcodebuild -arch arm64 -scheme GUI -configuration Release -verbose
    ```

### iOS Build Instructions

#### iOS CLI-Like Build

1. Generate the build configuration with CMake
    ```bash
    mkdir -p "build"
    cmake -G "Xcode" -S . -B "build" -DCMAKE_TOOLCHAIN_FILE=toolchain/ios.toolchain.cmake
    ```
2. Build and Run the application
- Open the `MLPerf.xcodeproj` project file in Xcode and select `CLI` as the target.
- Choose an iOS device and set the **Development Team** in the Build Settings for the `CLI` target.
- Build and run the app in Xcode on the selected device.
- **Note:** You will need to complete some standard procedures on your device, such as enabling development mode and authorizing the app for deployment.

#### iOS GUI Build

1. Generate the build configuration with CMake
    ```bash
    mkdir -p "build"
    cmake -G "Xcode" -S . -B "build" -DMLPERF_BUILD_GUI=ON -DCMAKE_PREFIX_PATH="/[Qt Dir]/6.8.0/ios" -DCMAKE_TOOLCHAIN_FILE=toolchain/ios.toolchain.cmake -DMLPERF_XCODE_SIGN_IDENTITY="Apple Development" -DMLPERF_XCODE_DEVELOPMENT_TEAM=[Dev Team Id]
    ```
2. Build and Run the application
- Open the `MLPerf.xcodeproj` project file in Xcode and select `GUI` as the target.
- Choose an iOS device as the run destination.
- Build and run the app in Xcode on the selected device.

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
The application will be located in `Bin\Windows\Debug\mlperf-windows.exe`.

#### Windows GUI Build
1. Generate the build configuration with CMake
    ```bash
    mkdir -p "build"
    cmake -G "Visual Studio 17 2022" -A x64 -S . -B "build" -DMLPERF_BUILD_GUI=ON -DCMAKE_PREFIX_PATH="C:/Qt/6.8.0/msvc2019_64" -DCMAKE_BUILD_TYPE=Release
    cd build
    msbuild mlperf.sln -p:Configuration=Release -ds:True -v:diag
    ```

#### Windows CLI Build for IHV_NATIVE_QNN

1. Generate the build configuration with CMake
    ```bash
    mkdir -p "build"
    cmake -G "Visual Studio 17 2022" -A ARM64 -S . -B "build" -DMLPERF_IHV_NATIVE_QNN=ON # Use -A x64 for x64 target
    cd build
    msbuild mlperf.sln -p:Configuration=Debug -ds:True -v:diag
    ```

please check the [GUI Flags](#gui-flags) for more details on the available flags.

## Build Flags and Options
The following table shows the available flags that can be used to control the build process:

We support many IHV EPs, Each IHV EP has its own detailed documentation:
- [NativeOpenVINO](src/CIL/IHV/NativeOpenVINO/src/README.md)
- [NativeQNN](src/CIL/IHV/NativeQNN/src/README.md)
- [OrtGenAI](src/CIL/IHV/OrtGenAI/src/README.md)
and more IHV EPs can be found in the [src/CIL/IHV](src/cil/IHV) directory.


### GUI Flags

The following flags are available for GUI builds:

| Flag                                        | Description                                 | Required | Available Values    | Default Value |
| ------------------------------------------- | ------------------------------------------- | -------- | ------------------- | ------------- |
| `-DMLPERF_BUILD_GUI:BOOL`                   | Build the GUI target                        | No       | `OFF`, `ON`         | `OFF`        |
| `-DCMAKE_BUILD_TYPE:STRING`                 | Specify the build type                      | No       | `Debug`, `Release`  | `Debug`      |
| `-DCMAKE_PREFIX_PATH:STRING`                | Specify the Qt installation path            | No       | `[filesystem path]` |              |


