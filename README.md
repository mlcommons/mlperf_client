# MLPerf Client benchmark

MLPerf Client is a benchmark for Windows and macOS, focusing on client form factors in ML inference scenarios like AI chatbots,
image classification, etc. The benchmark evaluates performance across different hardware and software configurations,
providing command line interface.

## Table of Contents
- [Features](#features)
- [Running the Application](#running-the-application)
  - [macOS Users](#macos-users)
  - [Windows Users](#windows-users)
  - [iOS Users](#ios-users)
- [Command Line Arguments](#command-line-arguments)
  - [Arguments Reference](#arguments-reference)
  - [Usage Examples](#usage-examples)
  - [Arguments Behavior Summary](#arguments-behavior-summary)
- [Configuration File Format](#configuration-file-format)
  - [URIs Supported](#uris-supported)
  - [Tested Workflows](#tested-workflows)
- [Running MLPerf Client on an Offline Machine](#running-mlperf-client-on-an-offline-machine)
  - [1. Download everything on a connected machine](#1-download-everything-on-a-connected-machine)
  - [2. Run benchmarks on the offline machine](#2-run-benchmarks-on-the-offline-machine)
- [Supported Execution Providers and Configurations](#supported-execution-providers-and-configurations)
  - [Status Definitions](#status-definitions)
  - [Notes](#notes)
- [Supported Platforms by Execution Provider](#supported-platforms-by-execution-provider)
- [Frequently Asked Questions (FAQ)](#frequently-asked-questions-faq)
  - [Q: Does the benchmark require an Internet connection to run?](#q-does-the-benchmark-require-an-internet-connection-to-run)
  - [Q: Do I need to run the test multiple times to ensure accurate performance results?](#q-do-i-need-to-run-the-test-multiple-times-to-ensure-accurate-performance-results)
  - [Q. The benchmark reports an error message and stops running without reporting results. What should I do?](#q-the-benchmark-reports-an-error-message-and-stops-running-without-reporting-results-what-should-i-do)
  - [Q. The benchmark doesn’t perform as well as expected on my system’s discrete GPU. Any ideas why?](#q-the-benchmark-doesnt-perform-as-well-as-expected-on-my-systems-discrete-gpu-any-ideas-why)
  - [Q. The tool appears stuck when processing a zip file. What should I do?](#q-the-tool-appears-stuck-when-processing-a-zip-file-what-should-i-do)



## Features

The MLPerf Client benchmark measures the performance of inference tasks on personal computers. It supports the following features:

- The application provides reference models for inference, including support for models such as Llama 2.
- Independent Hardware Vendors {IHVs} offer paths as Execution Providers, with their own custom model format and stack dependencies, which adhere to IHV APIs.
- The client bundles all enabled/active paths from the EP list (see below)
- Configure hardware and inference parameters using a JSON configuration file.
- The final build product is a single binary for Windows and MacOS, with model and data files downloaded as needed.

## Running the Application

### macOS Users

You need to ensure the application has the necessary execution permissions before running it for the first time. Here's
how to do it:

```bash
chmod +x mlperf-mac
```

After setting the permissions, you can run the application using:

```bash
./mlperf-mac [options]
```

### Windows Users

You can run the application using:

```cmd
.\mlperf-windows.exe [options]
```

### iOS Users

iOS developers can test it using standard methods for deploying and running apps on iOS devices via Xcode. CLI is not supported for the iOS app, use GUI instead.

## Command Line Arguments

### Arguments Reference

Below are the available command line arguments that control the tool's behavior:

| Argument                     | Description                                                                  | Required | Default Value | Available Values            |
|------------------------------|------------------------------------------------------------------------------|----------|---------------|-----------------------------|
| `-h, --help`                 | Show help message and exit                                                   | No       |               |                             |
| `-v, --version`              | Show the version of the tool                                                 | No       |               |                             |
| `-c, --config`               | Path to the configuration file                                               | Yes      |               | Any valid path              |
| `-o, --output-dir`           | Specify output directory. If not provided, default output directory is used. | No       |
| `-d, --data-dir`             | Specify directory, where all required data files will be downloaded          | No       | `data`        |
| `-t, --temp-dir`             | Specify directory, where temporary files will be stored.                     | No       |               |
| `-p, --pause`                | Flag to allow pausing the program at the end of execution.                   | No       | `true`        | `true`, `false`             |
| `-l, --logger`               | Path to the log4cxx configuration file                                       | No       | `log4cxx.xml` | Any valid path              |
| `-m, --list-models`          | List all the available models supported by the tool                          | No       |               |                             |
| `-b, --download_behaviour`        | Controls The files download behaviour                                         | No       | `normal`       | `forced`, `prompt`, `skip_all`, `deps_only `, `normal` |
| `-n, --cache-local-files` |  If it is set to false local files specified in the config will not be copied and cached | No | `true` | `true`, `false`|
| `-i, --device-id` | Overrides device id in the config file for EP supporting devie enumeration. Use 'All' to run benchmark on all devices sequentially | No |  | `all`, `0`, `1`, `2`, `3`, `4`, `5`, `6`, `7`, `9` |
| `-e, --enumerate-devices` |  Enumerates available devices for all EPs present in the provided config file and exits | No | `false` | `true`, `false`|
| `-s, --skip-failed-prompts` | Skip failed prompts during the execution of the application. If set, the application will not stop on failed prompts and will continue execution | No | `false` | `true`, `false` | 

### Usage Examples

Typical usage of the tool would look like this:

**Run the benchmark using a specific configuration file:**

```bash
.\mlperf-windows.exe -c NVIDIA_ORTGenAI-DML_GPU.json
```

This command runs the benchmark using `NVIDIA_ORTGenAI-DML_GPU.json` as the configuration file.

### Arguments Behavior Summary

- `-b` Controls the behavior of the application's file downloading process.
    - `forced`: The application will download the necessary files even if they are already present.
    - `prompt`: The application will check for the existence of required files.
      If any are missing, the user will be
      prompted to decide whether to download the missing files or abort the operation.
    - `skip_all`: The application will skip downloading any files.
      If a required file is missing, an error will be displayed, 
      and the operation will be aborted.
    - `deps_only`: The application will download only the required dependencies and exit.
    - `normal` (default): The application will check the local cache first. If files are not cached or their URIs have changed, it will download the missing/updated files and cache them for future use.


Now, let's see how to create a configuration file for the tool.

## Configuration File Format

*Before reading this section, please check the repository's data folder for sample configuration files.*
The configuration file is a JSON file that stores the tool's settings. This is an example of a configuration file.

```json
{
  "SystemConfig": {
    "Comment": "Default config",
    "TempPath": ""
  },
  "Scenarios": [
    list
    of
    scenarios
    here
  ]
}
```

Where:

- SystemConfig: Contains the system configuration settings for the tool.
    - `Comment`: A comment for the configuration file that describes the configuration.
    - `TempPath`: The path to the temporary directory where the tool will store the downloaded files. If this is not
      provided, the tool will use the system's temporary directory.
    - `BaseDir`: The `BaseDir` is an Optional field that can be used to specify a base directory that will be prepended
      to all relative paths defined in the configuration file. When a relative path is detected in the configuration,
      the tool automatically prepends the basedir value to it.
    - `DownloadBehavior`: The `DownloadBehavior` is an Optional field that do exactly what the `-b` command line argument does.
      It controls the behavior of the application's file downloading process. If the `DownloadBehavior` provided in both the
      configuration file and the command line, the command line argument will take precedence.
    - `CacheLocalFiles`: The `CacheLocalFiles` is an Optional field that do exactly what the `--cache-local-files` command line argument does. If it is set to false local files specified in the config will not be copied and cached. If the `CacheLocalFiles` provided in both the configuration file and the command line, the command line argument will take precedence

- Scenarios: A list of scenarios to run using the tool. Each scenario has the following format:
    ```json
    {
      "Name": "Llama2",
      "Models": [list of models here],
      "InputFilePath": [ "input file path1", "input file path2", ...],
      "AssetsPath": [ "assets path1", "assets path2", ...],
      "ResultsVerificationFile": "path to results verification file",
      "DataVerificationFile": "path to data verification file",
      "Iterations": 1000,
      "WarmUp": 1,
      "Delay": 0,
      "ExecutionProviders": [list of execution providers here]
    }
    ```
  Where:
    - `Name`: the name of the scenario.
    - `Models`: a list of models to use for the scenario, the tool will run the scenario using each of the models in the
      list.
      each model has the following format:
      ```json
      {
          "ModelName": "llama-2-7b-chat-dml",
          "FilePath": "https://client.mlcommons-storage.org/deps/1.0/scenario_files/llm/llama2/models/OrtGenAI/model.onnx",
          "DataFilePath": "https://client.mlcommons-storage.org/deps/1.0/scenario_files/llm/llama2/models/OrtGenAI/model.onnx.data.zip",
          "TokenizerPath": "https://client.mlcommons-storage.org/deps/1.0/scenario_files/llm/llama2/models/OrtGenAI/tokenizer.zip"
      }
      ```
      Where:
        - `ModelName`: Specifies the model name.
        - `FilePath`:  Specifies the path to the main model file (e.g., OrtGenAI/model.onnx).
        - `DataFilePath`: Specifies the path to the data file associated with the model (e.g.,
          OrtGenAI/model.onnx.data). This file is required for executing the Llama2 scenario. Some models might have this file splitted into multiple
          files. To avoid setting manually all the files in this dictionary, these can be zipped into a single
          file - the app will take care of the unzipping.
        - `TokenizerPath`: Specifies the path to the tokenizer file (e.g., OrtGenAI/tokenizer.zip). This file is
          required for executing the Llama2 scenario and contains the necessary tokenizer configuration and data.(The
          file does not need to be in ZIP format; it can be any supported format compatible with the model.)

    - `InputFilePath`: is a list of paths to the data files used in the scenario (input data) this could be a list of
      image files or even zip files.
    - `AssetsPath`: is a list of paths to the assets files used in the scenario (like labels file).
    - `ResultsVerificationFile`: is the path to the file that contains the expected results of the scenario.
      for example, if the scenario is a content generation task, then the ResultsVerificationFile should contain the expected tokens
      for the input file

        ```json
        {
          "expected_tokens": [
            [29896, 29900, 2440, 8020, 29892, ...]
          ]
        }
        
        ```

    - `DataVerificationFile`: a path to the file that contains the SHA256 hash of all the files used in the scenario,
      this is used to verify the integrity of the data files. Please note that this file is optional, but if it's
      provided, it should contain the hashes of all the files used in the scenario.
      The format of the `DataVerificationFile` should be:
        ```json
        {
          "FileHashes": [
            "8b13ac3ccb407ecbbcae0fe427f065c44dd96e259935de35699e607ec5ba12bc",
            ...
        
          ]
        }
        ```

      The order of the hashes shouldn't matter, the tool will verify the integrity of the files by comparing the hashes
      of the files with the hashes in the `DataVerificationFile`.

    - `Iterations`: the number of iterations to run the scenario for.
    - `WarmUp`: the number of warmup iterations to run the scenario for.
    - `Delay`: the delay in seconds between each scenario iteration. This is an optional argument, and the default
      values varies by model. For `llama2`, it is 5 seconds.
    - `ExecutionProviders`: a list of execution providers to use for the scenario, the tool will run the scenario using
      each of the execution providers in the list.
      each execution provider has the following format:
      ```json
      {
        "Name": "OrtGenAI",
        "Config": {
          "device_type": "GPU",
          "device_id": 0
        },
    	"Models": [
    	  overriden models here
        ]
      }
      ```
      Where:
      - `Name`: the name of the execution provider.
      - `Config`: the configuration for the execution provider, this is specific to each execution provider, for
      example, the configuration for OrtGenAI execution provider may look like this:
      ```json
      {
         "device_type": "GPU",
        "device_id": 0
      }
      ```
      `Models`: this is an optional section that can be used to override specific models for the current execution
      provider. It has the same format as the `Models` section of the scenario. You need to provide the `ModelName` and
      override values for the `FilePath`, `DataFilePath`, and `TokenizerPath` if necessary.

### URIs Supported

This tool accepts URIs in the following formats:

- Local files: `file://<path>` (must be a valid absolute local path or relative to `BaseDir` (if provided). Please note that these files are stored in cache later.)
- Remote resources: `https://<url>` (must be a valid URL)

### Tested Workflows

You can find specific tested workflows for different vendors in:

```
data/configs/vendors_default/
```

## Running MLPerf Client on an Offline Machine

### 1. Download everything on a **connected** machine

```bash
mlperf-windows.exe -c "path\to\<config>.json" --temp-dir .
```

* A new **`MLPerf`** sub-directory will appear next to the executable.  
* The directories/files you need to copy are:

```
data\               # datasets downloaded by the first run
MLPerf\             # IHV libraries and schemas
<config>.json       # every config file you intend to benchmark
mlperf-windows.exe  # the executable itself for Windows
```

Zip (or otherwise copy) the items above to the target machine with no Internet access.

---

### 2. Run benchmarks on the **offline** machine

```bash
mlperf-windows.exe -c "path\to\<config>.json" --temp-dir . --download_behaviour skip_all
```

* `--download_behaviour skip_all` **forces** the runner to use the local copies in  
  `./data/` and `./MLPerf/`, preventing any network calls.
* Repeat the command for each config file you packed in step&nbsp;1, replacing  
  `"path\to\<config>.json"` accordingly.

---

## Supported Execution Providers and Configurations

The following table contains a list of the supported execution providers (EPs) and the possible configurations we
support for each of them. For more detailed information and additional documentation, please refer to the
official [ONNX Runtime Documentation](https://onnxruntime.ai/docs/execution-providers/) .

| **Execution Provider**                                                                                          | **Configuration Options**                                                                                                                                 | **Configuration Type** | **Required** | **Default Value**                       | **Available Values**                                                                                                                                                                                                                                                                  | **Description**&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp; | 
|-----------------------------------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------|------------------------|--------------|-----------------------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| NativeOpenVINO(✅ **Active**)                                                                                    | device_type                                                                                                                                              | string                 | True         |                                         | ["GPU", "NPU"]                                                                                                                                                                                                                                                                        |                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
|                                                                                                                 | num_of_threads                                                                                                                                            | integer                | False        | 0                                       | >= -1                                                                                                                                                                                                                                                                                 |                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
|                                                                                                                 | num_streams                                                                                                                                               | integer                | False        | 0                                       | >= 1                                                                                                                                                                                                                                                                                  |                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| OrtGenAI(✅ **Active**)                                                                                          | device_type                                                                                                                                              | string                 | True         |                                         | ["GPU"]                                                                                                                                                                                                                                                                               |                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
|                                                                                                                 | device_id                                                                                                                                                 | integer                | False        | 0                                       |                                                                                                                                                                                                                                                                                       |                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
|                                                                                                                 | device_vendor                                                                                                                                             | string                 | False        |                                         | ["Intel", "AMD", "NVIDIA"]                                                                                                                                                                                                                                                                                      |                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| WindowsML(🚧 **Experimental**)                                                                                  | device_type                                                                                                                                              | string                 | True         |                                         | ["GPU", "NPU"]                                                                                                                                                                                                                                                                               |                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
|                                                                                                                 | device_id                                                                                                                                                 | integer                | False        | 0                                       |                                                                                                                                                                                                                                                                                        |                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
|                                                                                                                 | device_vendor                                                                                                                                             | string                 | False        |                                         | ["Intel", "AMD", "NVIDIA"]                                                                                                                                                                                                                                                                                      |                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
|                                                                                                                 | device_ep                                                                                                                                                 | string                | False        |                                        | ["OpenVINO"]                                                                                                                                                                                                                                                                                      |                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| NativeQNN(✅ **Active**)                                                                                         | device_type                                                                                                                                               | string                 | True         |                                         | ["NPU_CPU"]                                                                                                                                                                                                                                                                               |                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             |
| OrtGenAI-RyzenAI(✅ **Active**)                                                                                           | device_type                                                                                                                                               | string                 | True         |                                         | ["NPU and GPU"]                                                                                                                                                                                                                                                                               |                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| MLX(✅ **Active**)                                                                                           | device_type                                                                                                                                               | string                 | True         |                                         | ["GPU"]                                                                                                                                                                                                                                                                               |                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| Metal(🚧 **Experimental**)                                                                                      | device_type                                                                                                                                               | string                 | True         |                                         | ["GPU"]                                                                                                                                                                                                                                                                        | From [llama-cpp](https://github.com/ggml-org/llama.cpp)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
|                                                                                                                 | gpu_layers                                                                                                                                                | integer                | True         |                                         | >= 0                                                                                                                                                                                                                                                                                  |                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| CUDA(🚧 **Experimental**)                                                                                      | device_type                                                                                                                                               | string                 | True         |                                         | ["GPU"]                                                                                                                                                                                                                                                                        | From [llama-cpp](https://github.com/ggml-org/llama.cpp)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
|                                                                                                                 | gpu_layers                                                                                                                                                | integer                | True         |                                         | >= 0                                                                                                                                                                                                                                                                                  |                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
|                                                                                                                 | fa                                                                                                                                                | integer                | False         | 0                                        |                                                                                                                                                                                                                                                                                 |                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
|                                                                                                                 | no_mmap                                                                                                                                                | integer                | False         | 0                                         |                                                                                                                                                                                                                                                                                  |                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |

### Status Definitions

- ✅ **Active**: Enabled for production and development builds.
- 🚧 **Experimental**: Enabled for production and development builds, might not be stable.

### Notes

- OrtGenAI:
    - `device_id` is not required. If not present, the app will use any adapters it finds, unless they are
      software/default render devices.
    - `devide_type` - optional; "GPU" and "NPU"(for DirectML only) are valid options. If it is used:
        - If `device_id` is present, it will determine whether the device is GPU or NPU. If it does not match, it won't
          run.
        - If `device_id` is not provided, `device_type` will function as a filter, only running on devices that are "
          GPU" or "NPU".
    - `device_vendor`: It can be a vendor name, such as "Nvidia" or "Intel". Devices will be filtered to match the
      vendor. Case insensitive:
        - If `device_id` is present, it will check to see if the device vendor matches. If it does not match, it will
          not run.
        - If `device_id` is not specified, device_vendor will act as a filter, only running on devices that match the
          value.

## Supported Platforms by Execution Provider

| Execution Provider | Windows x64 | Windows ARM | macOS | iOS | Comments |
|--------------------|-------------|-------------|-------|-----|----------|
| NativeOpenVINO     | ✅           | ❌           | ❌    | ❌   |          |
| OrtGenAI           | ✅           | ❌           | ❌    | ❌   |          |
| WindowsML          | ✅           | ❌           | ❌    | ❌   |          |
| NativeQNN          | ❌           | ✅           | ❌    | ❌   |          |
| OrtGenAI-RyzenAI      | ✅           | ❌           | ❌    | ❌   |          |
| MLX                | ❌           | ❌           | ✅    | ✅   |          |
| Metal              | ❌           | ❌           | ✅    | ❌   |          |
| CUDA              | ✅           | ❌           | ❌    | ❌   |          |

_____________________________________________