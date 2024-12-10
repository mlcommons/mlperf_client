# MLPerf Client benchmark

MLPerf Client is a benchmark for Windows and macOS, focusing on client form factors in ML inference scenarios like AI chatbots,
image classification, etc. The benchmark evaluates performance across different hardware and software configurations,
providing command line interface.

## Table of Contents
- [Features](#features)
- [Running the Application](#running-the-application)
  - [Windows Users](#windows-users)
- [Command Line Arguments](#command-line-arguments)
  - [Arguments Reference](#arguments-reference)
  - [Usage Examples](#usage-examples)
  - [Arguments Behavior Summary](#arguments-behavior-summary)
- [Configuration File Format](#configuration-file-format)
  - [URIs Supported](#uris-supported)
  - [Tested Workflows](#tested-workflows)
- [Supported Execution Providers and Configurations](#supported-execution-providers-and-configurations)
  - [Status Definitions](#status-definitions)
  - [Notes](#notes)
- [Supported Platforms by Execution Provider](#supported-platforms-by-execution-provider)



## Features

The MLPerf Client benchmark measures the performance of inference tasks on personal computers. It supports the following features:

- The application provides reference models for inference, including support for models such as Llama 2.
- Independent Hardware Vendors {IHVs} offer paths as Execution Providers, with their own custom model format and stack dependencies, which adhere to IHV APIs.
- The client bundles all enabled/active paths from the EP list (see below)
- Configure hardware and inference parameters using a JSON configuration file.
- The final build product is a single binary for each platform, with model and data files downloaded as needed.

## Running the Application

### Windows Users

You can run the application using:

```cmd
.\mlperf-windows.exe [options]
```

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
| `-p, --pause`                | Flag to allow pausing the program at the end of execution.                   | No       | `true`        | `true`, `false`             |
| `-l, --logger`               | Path to the log4cxx configuration file                                       | No       | `log4cxx.xml` | Any valid path              |
| `-m, --list-models`          | List all the available models supported by the tool                          | No       |               |                             |
| `-b, --download_behaviour`        | Controls The files download behaviour                                         | No       | `normal`       | `forced`, `prompt`, `skip_all`, `deps_only `, `normal` |

### Usage Examples

Typical usage of the tool would look like this:

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
          "ModelName": "Llama2 llama-2-7b-chat-dml",
          "FilePath": "https://client.mlcommons-storage.org/deps/0.5/scenario_files/llm/llama2/models/OrtGenAI/llama-2-7b-chat-dml/model.onnx",
          "DataFilePath": "https://client.mlcommons-storage.org/deps/0.5/scenario_files/llm/llama2/models/OrtGenAI/llama-2-7b-chat-dml/model.onnx.data.zip",
          "TokenizerPath": "https://client.mlcommons-storage.org/deps/0.5/scenario_files/llm/llama2/models/OrtGenAI/llama-2-7b-chat-dml/tokenizer.zip"
      }
      ```
      Where:
        - `ModelName`: Specifies the model name.
        - `FilePath`:  Specifies the path to the main model file (e.g., llama2-cpu-int4/model.onnx).
        - `DataFilePath`: Specifies the path to the data file associated with the model (e.g.,
          llama-2-7b-chat-dml/model.onnx.data). This file is required for executing the Llama2 scenario. Some models might have this file splitted into multiple
          files. To avoid setting manually all the files in this dictionary, these can be zipped into a single
          file - the app will take care of the unzipping.
        - `TokenizerPath`: Specifies the path to the tokenizer file (e.g., llama-2-7b-chat/tokenizer.zip). This file is
          required for executing the Llama2 scenario and contains the necessary tokenizer configuration and data.(The
          file does not need to be in ZIP format; it can be any supported format compatible with the model.)

    - `InputFilePath`: is a list of paths to the data files used in the scenario (input data) this could be a list of
      json files or even zip files.
    - `AssetsPath`: is a list of paths to the assets files used in the scenario.
    - `ResultsVerificationFile`: is the path to the file that contains the expected results of the scenario, ie the
      excepted output for each input.
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
      values varies by model. For `llama2`.
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

## Supported Execution Providers and Configurations

The following table contains a list of the supported execution providers (EPs) and the possible configurations we
support for each of them.

| **Execution Provider**                                                                                          | **Configuration Options**                                                                                                                                 | **Configuration Type** | **Required** | **Default Value**                       | **Available Values**                                                                                                                                                                                                                                                                  | **Description**&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp; | 
|-----------------------------------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------|------------------------|--------------|-----------------------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| NativeOpenVINO(✅ **Active**)                                                                                    | device_type                                                                                                                                               | string                 | True         |                                         | ["GPU"]                                                                                                                                                                                                                                                                        |                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
|                                                                                                                 | num_of_threads                                                                                                                                            | integer                | False        | 0                                       | >= -1                                                                                                                                                                                                                                                                                 |                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
|                                                                                                                 | num_streams                                                                                                                                               | integer                | False        | 0                                       | >= 1                                                                                                                                                                                                                                                                                  |                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| OrtGenAI(✅ **Active**)                                                                                          | device_type                                                                                                                                               | string                 | True         |                                         | ["GPU"]                                                                                                                                                                                                                                                                               |                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
|                                                                                                                 | device_id                                                                                                                                                 | integer                | False        | 0                                       |                                                                                                                                                                                                                                                                                       |                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
|                                                                                                                 | device_vendor                                                                                                                                             | string                 | False        |                                         |                                                                                                                                                                                                                                                                                       |                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |

### Status Definitions

- ✅ **Active**: Enabled for production and development builds.

### Notes

- OrtGenAI:
    - `device_id` is not required. If not present, the app will use any adapters it finds, unless they are
      software/default render devices.
    - `devide_type` - optional; "GPU" is a valid option. If it is used:
        - If `device_id` is present, it will determine whether the device is GPU. If it does not match, it won't
          run.
        - If `device_id` is not provided, `device_type` will function as a filter, only running on devices that are "
          GPU" or "NPU".
    - `device_vendor`: It can be a vendor name, such as "Nvidia", "AMD" or "Intel". Devices will be filtered to match the
      vendor. Case insensitive:
        - If `device_id` is present, it will check to see if the device vendor matches. If it does not match, it will
          not run.
        - If `device_id` is not specified, device_vendor will act as a filter, only running on devices that match the
          value.

## Supported Platforms by Execution Provider

| Execution Provider | Windows x64 | Windows ARM | macOS | Comments |
|--------------------|-------------|-------------|-------|----------|
| NativeOpenVINO     | ✅           | ❌           | ❌     |          |
| OrtGenAI           | ✅           | ❌           | ❌     |          |

_____________________________________________
