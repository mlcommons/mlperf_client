## Usage
QAIRT GENAI and its implementation in MLPerf require the llama_cpu.bin file generated from running instructions mentioned in [<code><b><path/to/mlperf_client_dev/tools/IHV/NativeQNN/CPU_MODEL_GENERATION/README.md></b></code>](../../../../../tools/IHV/NativeQNN/CPU_MODEL_GENERATION/README.md) 
and npu bin file generated from running instructions mentioned in [<code><b><path/to/mlperf_client_dev/tools/IHV/NativeQNN/NPU_MODEL_GENERATION/README.md></b></code>](../../../../../tools/IHV/NativeQNN/NPU_MODEL_GENERATION/README.md)

### Llama2 sample config
```
{
  "SystemConfig": {
    "Comment": "Default Llama2 NativeQNN Qualcomm Config",
    "TempPath": "",
    "EPDependenciesConfigPath": ""
  },
  "Scenarios": [
    {
      "Name": "Llama2",
      "Models": [
        {
          "ModelName": "Llama2",
          "FilePath": "https://client.mlcommons-storage.org/deps/1.0/scenario_files/llm/llama2/models/NativeQNN/llama2_cpu.bin",
          "DataFilePath": "https://client.mlcommons-storage.org/deps/1.0/scenario_files/llm/llama2/models/NativeQNN/llama2_npu.zip",
          "TokenizerPath": "https://client.mlcommons-storage.org/deps/1.0/scenario_files/llm/llama2/models/NativeQNN/tokenizer-v1.zip"
        }
      ],
      "InputFilePath": [
        "https://client.mlcommons-storage.org/deps/0.5/scenario_files/llm/llama2/data/content_generation/greedy-prompt_cot.39329.json"
      ],
      "AssetsPath": [],
      "ResultsVerificationFile": "https://client.mlcommons-storage.org/deps/1.0/scenario_files/llm/generation-greedy-results.json",
      "DataVerificationFile": "",
      "Iterations": 3,
      "Delay": 5,
      "ExecutionProviders": [
        {
          "Name": "NativeQNN",
          "Config": {
            "device_type": "NPU_CPU"
          },
          "LibraryPath": ""
        }
      ]
    }
  ]
}
```

### Llama3 sample config
```
{
  "SystemConfig": {
    "Comment": "Default Llama3 NativeQNN Qualcomm Config",
    "TempPath": "",
    "EPDependenciesConfigPath": ""
  },
  "Scenarios": [
    {
      "Name": "Llama3",
      "Models": [
        {
          "ModelName": "Llama3",
          "FilePath": "https://client.mlcommons-storage.org/deps/1.0/scenario_files/llm/llama3/models/NativeQNN/llama3_cpu.bin",
          "DataFilePath": "https://client.mlcommons-storage.org/deps/1.0/scenario_files/llm/llama3/models/NativeQNN/llama3_npu.zip",
          "TokenizerPath": "https://client.mlcommons-storage.org/deps/1.0/scenario_files/llm/llama3/models/NativeQNN/tokenizer.zip"
        }
      ],
      "InputFilePath": [
        "https://client.mlcommons-storage.org/deps/0.5/scenario_files/llm/llama3/data/content_generation/greedy-prompt_cot.39329.json"
      ],
      "AssetsPath": [],
      "ResultsVerificationFile": "https://client.mlcommons-storage.org/deps/1.0/scenario_files/llm/generation-greedy-results.json",
      "DataVerificationFile": "",
      "Iterations": 3,
      "Delay": 5,
      "ExecutionProviders": [
        {
          "Name": "NativeQNN",
          "Config": {
            "device_type": "NPU_CPU"
          },
          "LibraryPath": ""
        }
      ]
    }
  ]
}
```

### Phi3.5 sample config
```
{
  "SystemConfig": {
    "Comment": "Default Phi3.5 NativeQNN Qualcomm Config",
    "TempPath": "",
    "EPDependenciesConfigPath": ""
  },
  "Scenarios": [
    {
      "Name": "Phi3.5",
      "Models": [
        {
          "ModelName": "",
          "FilePath": "https://client.mlcommons-storage.org/deps/1.0/scenario_files/llm/phi3.5/models/NativeQNN/phi3_5_cpu.bin",
          "DataFilePath": "https://client.mlcommons-storage.org/deps/1.0/scenario_files/llm/phi3.5/models/NativeQNN/phi3_5_npu.zip",
          "TokenizerPath": "https://client.mlcommons-storage.org/deps/1.0/scenario_files/llm/phi3.5/models/NativeQNN/tokenizer.zip"
        }
      ],
      "InputFilePath": [
        "https://client.mlcommons-storage.org/deps/0.5/scenario_files/llm/phi3.5/data/content_generation/greedy-prompt_cot.39329.json"
      ],
      "AssetsPath": [],
      "ResultsVerificationFile": "https://client.mlcommons-storage.org/deps/1.0/scenario_files/llm/generation-greedy-results.json",
      "DataVerificationFile": "",
      "Iterations": 3,
      "Delay": 5,
      "ExecutionProviders": [
        {
          "Name": "NativeQNN",
          "Config": {
            "device_type": "NPU_CPU"
          },
          "LibraryPath": ""
        }
      ]
    }
  ]
}
```

An example config with files downloaded from web is available at `\data\configs\vendors_default\Llama2\Qualcomm_NativeQNN-NPU_CPU.json`. if using `LibraryPath` is used, it should be mentioned as `IHV/NativeQNN/IHV_NativeQNN.dll`. 

## FAQ

### What devices does this backend support?

This backend supports Snapdragon (R) X Elite  device.

### Is QAIRT used to run all the models?

Yes. All the models use Qualcomm AI Runtime(QAIRT) for execution for current version.

### Path for QAIRT SDK

Default path is '<cmake-build-dir>\_deps\native_qnn_qairt_sdk-src', unless QAIRT path was provided explicitly in cmake.