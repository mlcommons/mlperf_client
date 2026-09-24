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
      - [Windows ARM64](#windows-arm64)
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
      - [Windows ARM64 Build (native and cross)](#windows-arm64-build-native-and-cross)
  - [Build Flags and Options](#build-flags-and-options)
    - [Provider packaging (online vs offline)](#provider-packaging-online-vs-offline)
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
To build the application in GUI mode on macOS, you need Qt 6.10.2 (or compatible 6.x).

1. **Install Qt 6.10.2:**
   - Download and install Qt 6.10.2 from the [official Qt website](https://www.qt.io/download).
   - During the installation process, select the following:
     - Qt 6.10.2 and add
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
To build the application in GUI mode on Windows, you need Qt 6.10.2 (or compatible 6.x).

1. **Install Qt 6.10.2:**
   - Required Version: Qt 6.10.2.
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

#### Windows ARM64

Windows ARM64 binaries can be produced natively on an ARM64 host or
cross-compiled on an x64 host (see
[Windows ARM64 Build](#windows-arm64-build-native-and-cross)). The validated
source checkpoint was `c8d2dc027ab60563f7ca3b89af4b8dea53736b1f`. Both paths
require:

| Input | Validated version | Why it is needed |
|---|---:|---|
| Visual Studio 2022 Professional | 17.14.31 | MSVC ARM64 compiler, libraries, and tools |
| MSVC | 14.44.35207 | ARM64 C/C++ toolchain |
| Windows SDK | 10.0.26100.0 | Windows headers, libraries, `Windows.winmd`, and `cppwinrt` |
| CMake | 4.0.0-rc1 | Top-level and nested builds |
| CUDA Toolkit | 13.4.1 (nvcc 13.4.59) | Install the package matching the build host, as detailed below |
| Git | current installation | Fetches pinned external source revisions |
| Python | 3.13 | Configure/download scripts; architecture depends on build host |

Ninja does not have to be installed separately: the GGML CUDA build bootstraps
a pinned Ninja matched to the build host (see `src/CIL/IHV/GGML/src/README.md`
for the exact behavior and the offline `-DGGML_NINJA_EXECUTABLE` override).

CUDA Toolkit 13.4 is therefore the only separately installed NVIDIA SDK, but
the required installer differs by build host. A complete ImageGen distribution
also downloads the pinned public ARM64 Python/PyTorch/TensorRT packages
described in `src/CIL/IHV/Diffusers/src/README.md`. WindowsML is obtained from
its pinned NuGet version during a connected build; Qt must be installed (see
[Windows GUI Mode](#windows-gui-mode)).

The Visual Studio installation must include:

- Desktop development with C++;
- MSVC v143 ARM64 build tools;
- the Windows 11 SDK, including C++/WinRT tooling;
- the LLVM/Clang tools for Windows component.

Vendor-specific ARM64 details live with their IHV:

- llama.cpp / GGML CUDA toolchain, Ninja bootstrap, OpenMP runtime, and CUDA
  code generation: `src/CIL/IHV/GGML/src/README.md`;
- Diffusers ImageGen Python environment, wheel pins, and the Torch-TensorRT
  wheel: `src/CIL/IHV/Diffusers/src/README.md`;
- WindowsML per-architecture OGA pin and the ARM64 runtime DLL set:
  `src/CIL/IHV/WindowsML/src/README.md`.

The complete pinned-input tables and the reproducibility checklist are at the
end of the [Windows ARM64 Build](#windows-arm64-build-native-and-cross)
section; the dependency compatibility rule is in
`src/CIL/IHV/Diffusers/src/README.md`.

## Building the Application

### Common Steps
1. Open a terminal (macOS) or command prompt (Windows).

2. Clone the repository and navigate to the directory:
   ```
   git clone --recurse-submodules https://github.com/mlcommons/mlperf_client.git
   cd mlperf_client
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
    cmake -G "Xcode" -S . -B "build" -DMLPERF_BUILD_GUI=ON -DCMAKE_PREFIX_PATH="/Users/user/Qt/6.10.2/macos"
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
    cmake -G "Xcode" -S . -B "build" -DMLPERF_BUILD_GUI=ON -DCMAKE_PREFIX_PATH="/[Qt Dir]/6.10.2/ios" -DCMAKE_TOOLCHAIN_FILE=toolchain/ios.toolchain.cmake -DMLPERF_XCODE_SIGN_IDENTITY="Apple Development" -DMLPERF_XCODE_DEVELOPMENT_TEAM=[Dev Team Id]
    ```
2. Build and Run the application
- Open the `MLPerf.xcodeproj` project file in Xcode and select `GUI` as the target.
- Choose an iOS device as the run destination.
- Build and run the app in Xcode on the selected device.

### Linux Build Instructions

Dependencies: 

```bash
lsb_release -a
  No LSB modules are available.
  Distributor ID: Ubuntu
  Description:    Ubuntu 24.04.3 LTS
  Release:        24.04
  Codename:       noble

sudo apt install -y git curl wget build-essential cmake git-lfs clang-tidy libapr1-dev libapr1t64

git clone --recurse-submodules https://github.com/mlcommons/mlperf_client.git
cd mlperf_client

git-lfs pull
git submodule update --remote --recursive --init
```

#### Linux CLI Build

1. Generate the build configuration with CMake

```bash
export CC=/usr/bin/gcc
export CXX=/usr/bin/g++

mkdir -p "build"
cmake -G "Unix Makefiles" -S . -B build
```

2. Build the application

```bash
cd build
make
```

2. Run the application

The application will be located in `Bin/Linux/Release/mlperf-linux`.

#### Windows GUI Build

### Windows Build Instructions

The instructions below target Windows x64. For Windows ARM64 (native or
x64-to-ARM64 cross builds, including the NVIDIA CUDA providers), see
[Windows ARM64 Build](#windows-arm64-build-native-and-cross).

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
    cmake -G "Visual Studio 17 2022" -A x64 -S . -B "build" -DMLPERF_BUILD_GUI=ON -DCMAKE_PREFIX_PATH="C:/Qt/6.10.2/msvc2019_64" -DCMAKE_BUILD_TYPE=Release
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

#### Windows ARM64 Build (native and cross)

See [Windows ARM64](#windows-arm64) for the prerequisites; this build produces
native Windows ARM64 binaries for the CLI and GUI, and the following execution
paths:

- llama.cpp / GGML CUDA;
- WindowsML, including the NVIDIA TensorRT RTX provider;
- Diffusers ImageGen with an embedded ARM64 Python environment;
- NativeQNN by default on ARM64 (not exercised in the NVIDIA validation run).

**Build on a native Windows ARM64 host.** This is the preferred path if
official CI provides a native Windows ARM64 builder. CMake, Visual Studio
build tools, Python, Git, and Qt build tools all run natively on ARM64. No x64
Qt host-tools package or Qt runtime replacement step is used.

The top-level project uses the Visual Studio ARM64 generator; the nested
llama.cpp CUDA build's native toolchain selection is described in
`src/CIL/IHV/GGML/src/README.md`.

Install the **Windows ARM64 CUDA Toolkit 13.4** package on this builder. Do not
install or point CMake at the x64-host CUDA Toolkit for a native build. The
native toolkit supplies the ARM64 CUDA compiler tools and ARM64 target
libraries used directly by the ARM64 build processes.

Configure with the ARM64 generator platform and the ARM64 CUDA Toolkit:

```powershell
cmake -S . -B ../build-winarm64-native `
  -G "Visual Studio 17 2022" -A ARM64 `
  -DCUDAToolkit_ROOT="C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.4" `
  -DMLPERF_IHV_GGML_CUDA=ON `
  -DMLPERF_IHV_WINDOWSML=ON `
  -DMLPERF_IHV_DIFFUSERS_NVIDIA=ON `
  -DMLPERF_IHV_DIFFUSERS_INSTALL_PYTHON=ON `
  -DMLPERF_SUBPROCESS_IHV_ISOLATION=ON
```

On a native ARM64 host, the embedded ARM64 Python can execute during the build,
so `MLPERF_IHV_DIFFUSERS_INSTALL_PYTHON=ON` can create and freeze the ImageGen
environment directly. The default connected build downloads all packages from
public indexes. For an offline build, pre-download the exact packages listed in
`src/CIL/IHV/Diffusers/src/README.md` and set `MLPERF_DIFFUSERS_WHEELHOUSE` to
that directory before building.

If more than one Visual Studio instance exists, also pass the desired
`CMAKE_GENERATOR_INSTANCE`. Use that installation's native ARM64
`clang-tidy.exe` when overriding `CLANG_TIDY_EXE`.

**Cross-compile on an x64 Windows host.** This is the path validated during
bring-up. It uses x64 build tools to produce ARM64 target binaries. The
validated toolchain specifically used the `Hostx64/arm64` MSVC tools and an
x64 Python 3.13 host driver.

Install the **Windows x86-64 CUDA Toolkit 13.4** package on the x64 builder.
That toolkit includes both x64 host tools and ARM64 target files. The
cross-build specifically uses:

```text
CUDA/v13.4/bin/nvcc.exe       x64-host CUDA compiler driver
CUDA/v13.4/lib/arm64          ARM64 import/static libraries
CUDA/v13.4/bin/arm64          ARM64 target runtime files
```

Do not install the native ARM64-host CUDA Toolkit on the x64 builder: its host
executables cannot run there. `CUDAToolkit_ROOT` still points at the common
`CUDA/v13.4` root; the build selects its ARM64 target subdirectories.

Use forward slashes in all CMake paths. Pinning the generator instance matters
when more than one Visual Studio installation is present:

```powershell
cmake -S . -B ../build-winarm64-cross `
  -G "Visual Studio 17 2022" -A ARM64 `
  -DCMAKE_GENERATOR_INSTANCE="C:/Program Files/Microsoft Visual Studio/2022/Professional" `
  -DCLANG_TIDY_EXE="C:/Program Files/Microsoft Visual Studio/2022/Professional/VC/Tools/Llvm/x64/bin/clang-tidy.exe" `
  -DCUDAToolkit_ROOT="C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.4" `
  -DMLPERF_IHV_GGML_CUDA=ON `
  -DMLPERF_IHV_WINDOWSML=ON `
  -DMLPERF_IHV_DIFFUSERS_NVIDIA=ON `
  -DMLPERF_IHV_DIFFUSERS_INSTALL_PYTHON=OFF `
  -DMLPERF_SUBPROCESS_IHV_ISOLATION=ON
```

An x64 host cannot execute the embedded ARM64 Python. Prepare the pinned
`python_env.zip` separately and host it at the location referenced by the
generated `ep_dependencies_config_*.json` (see
`src/CIL/IHV/Diffusers/src/README.md` for the packer and wheel details). For
the cross-build, point `CMAKE_PREFIX_PATH` at an ARM64 Qt installation and
`QT_HOST_PATH` at a matching x64 Qt installation so `moc`, `uic`, `rcc`, and
`windeployqt` run on the host. After deployment, the build replaces any staged
host Qt runtimes with the ARM64 target files.

**Build CLI or GUI.** The following examples use `<build-dir>` for either
`../build-winarm64-native` or `../build-winarm64-cross`.

Build the CLI:

```powershell
cmake --build <build-dir> --config Release --target CLI -- /m:2 /nr:false /v:minimal
```

For a GUI build, add:

```text
-DMLPERF_BUILD_GUI=ON
-DMLPERF_PACK_VENDORS_DEFAULT=ON
```

Then build:

```powershell
cmake --build <build-dir> --config Release --target GUI -- /m:2 /nr:false /v:minimal
```

ARM64 defaults `MLPERF_IHV_NATIVE_QNN=ON`. Do not disable it for an official
all-provider build. The local NVIDIA validation disabled it only because QNN
hardware was not being tested.

**Other pinned inputs.** The Windows ARM64 build currently pins these source
and binary inputs (per-IHV dependency pins are documented in the respective
IHV READMEs):

| Input | Pin |
|---|---|
| MLPerf Client source | `c8d2dc027ab60563f7ca3b89af4b8dea53736b1f` |
| stb/ImageIO | `2c980bb59875b0d32144a71867fbdebb2f77cd20` |
| Qt | `6.10.2` |
| dylib | `v2.2.1` |
| Repository submodules | exact commits recorded by the superproject |

The validated compiler/tool pins are:

| Tool | Pin |
|---|---|
| Visual Studio 2022 Professional | 17.14.31 |
| MSVC | 14.44.35207 |
| Windows SDK | 10.0.26100.0 |
| CMake | 4.0.0-rc1 |
| CUDA Toolkit | 13.4.1 (nvcc 13.4.59) |
| Python host driver | 3.13.13 x64 |
| Python target runtime | 3.13.13 ARM64 |

**Reproducibility limits.** Exact versions prevent dependency drift, but exact
versions alone do not make a bit-for-bit build. Before an official benchmark
release:

1. Record the exact public URL for every downloaded archive and wheel. Mirror
   them only when release infrastructure supports it.
2. Record and verify SHA-256 for Qt, NuGet packages, QAIRT, Ninja, FetchContent
   archives, and every Python wheel. CMake downloads do not all verify hashes
   today.
3. Generate a hash-locked Python requirements file (`--require-hashes`) from
   the mirrored wheelhouse. The current constraints freeze versions, while the
   architecture-specific native wheels are additionally hash-audited (see
   `src/CIL/IHV/Diffusers/src/README.md`).
4. Record the exact Visual Studio, MSVC, Windows SDK, CMake, and CUDA versions
   in the release manifest.
5. Build from a new source checkout and empty build/output directories.
6. Audit every shipped `.exe`, `.dll`, and `.pyd` as PE machine `0xAA64`.
7. Run CLI and GUI smoke tests for llama.cpp CUDA, WindowsML TensorRT RTX,
   Diffusers CUDA, and QNN on representative target hardware.

Models and benchmark data are runtime inputs and remain downloadable. Their
published checksums/configuration should be versioned independently of the
application build.

## Build Flags and Options
The following table shows the available flags that can be used to control the build process:

### Provider packaging (online vs offline)

The official/default mode is online:

```text
MLPERF_USE_LOCAL_IHV_LIBRARIES=OFF
```

Vendor configs contain no local `LibraryPath`. Published provider files
must be uploaded to the locations represented by the generated
`ep_dependencies_config_*.json` before users run that build.

For a self-contained local or offline build, configure with:

```text
-DMLPERF_USE_LOCAL_IHV_LIBRARIES=ON
```

That generates build-tree vendor configs with local provider paths. CLI tests
must use configs from
`<build>/data/configs/vendors_default`, not the source-tree online configs.

### CLI Flags

| Flag                                       | Description                                                                 | Required | Available Values | Default Value |
| ----------------------------------------- | ---------------------------------------------------------------------------- | -------- | ---------------- | ------------- |
| `-DMLPERF_SUBPROCESS_IHV_ISOLATION`        | Run IHV code in a separate process to avoid DLL conflicts (Windows only). When ON, the CLI supports `--subprocess-isolate`. | No       | `OFF`, `ON`       | `ON` (Windows), N/A (other platforms) |

**Notes:**
- When `MLPERF_SUBPROCESS_IHV_ISOLATION` is enabled on Windows, you can pass `--subprocess-isolate true` to the CLI to run IHV code in an isolated subprocess.

We support many IHV EPs, Each IHV EP has its own detailed documentation:
- [NativeOpenVINO](src/CIL/IHV/NativeOpenVINO/src/README.md)
- [NativeQNN](src/CIL/IHV/NativeQNN/src/README.md)
and more IHV EPs can be found in the [src/CIL/IHV](src/cil/IHV) directory.


### GUI Flags

The following flags are available for GUI builds:

| Flag                                        | Description                                 | Required | Available Values    | Default Value |
| ------------------------------------------- | ------------------------------------------- | -------- | ------------------- | ------------- |
| `-DMLPERF_BUILD_GUI:BOOL`                   | Build the GUI target                        | No       | `OFF`, `ON`         | `OFF`        |
| `-DCMAKE_BUILD_TYPE:STRING`                 | Specify the build type                      | No       | `Debug`, `Release`  | `Debug`      |
| `-DCMAKE_PREFIX_PATH:STRING`                | Specify the Qt installation path            | No       | `[filesystem path]` |              |


