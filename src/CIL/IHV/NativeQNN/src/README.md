## Usage
QAIRT GENAI and its implementation in MLPerf require the cpu file generated from running instructions mentioned in [<code><b><path/to/mlperf_client_dev/tools/IHV/NativeQNN/CPU_MODEL_GENERATION/README.md></b></code>](../../../../../tools/IHV/NativeQNN/CPU_MODEL_GENERATION/README.md) 
and npu bin file generated from running instructions mentioned in [<code><b><path/to/mlperf_client_dev/tools/IHV/NativeQNN/NPU_MODEL_GENERATION/README.md></b></code>](../../../../../tools/IHV/NativeQNN/NPU_MODEL_GENERATION/README.md)

### Llama2 sample config
```
{
  "SystemConfig": {
    "Comment": "Llama 2 7B Chat, Native QNN, NPU CPU, Base prompts",
    "TempPath": "",
    "EPDependenciesConfigPath": ""
  },
  "Scenarios": [
    {
      "Name": "Llama2",
      "Models": [
        {
          "ModelName": "llama-2-7b-chat-hf-qti",
          "FilePath": "file://models/llama2_cpu.bin",
          "DataFilePath": "file://models/llama2_npu.zip",
          "TokenizerPath": "https://client.mlcommons-storage.org/deps/1.0/scenario_files/llm/llama2/models/NativeQNN/tokenizer-v1.zip"
        }
      ],
      "InputFilePath": [
        "https://client.mlcommons-storage.org/deps/1.5/scenario_files/llm/llama2/prompts/content_generation/greedy-prompt_cot.39329.json"
      ],
      "AssetsPath": [],
      "ResultsVerificationFile": "https://client.mlcommons-storage.org/deps/1.0/scenario_files/llm/generation-greedy-results.json",
      "DataVerificationFile": "",
      "Iterations": 3,
      "WarmUp": 1,
      "Delay": 5,
      "ExecutionProviders": [
        {
          "Name": "NativeQNN",
          "Config": {
            "device_type": "NPU_CPU"
          }
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
    "Comment": "Llama 3.1 8B Instruct, Native QNN, NPU CPU, Base prompts",
    "TempPath": "",
    "EPDependenciesConfigPath": ""
  },
  "Scenarios": [
    {
      "Name": "Llama3",
      "Models": [
        {
          "ModelName": "llama-3.1-8B-Instruct-qti",
          "FilePath": "file://models/llama3_cpu.bin",
          "DataFilePath": "file://models/llama3_npu.zip",
          "TokenizerPath": "https://client.mlcommons-storage.org/deps/1.0/scenario_files/llm/llama3/models/NativeQNN/tokenizer.zip"
        }
      ],
      "InputFilePath": [
        "https://client.mlcommons-storage.org/deps/1.5/scenario_files/llm/llama3/prompts/content_generation/greedy-prompt_cot.39329.json"
      ],
      "AssetsPath": [],
      "ResultsVerificationFile": "https://client.mlcommons-storage.org/deps/1.0/scenario_files/llm/generation-greedy-results.json",
      "DataVerificationFile": "",
      "Iterations": 3,
      "WarmUp": 1,
      "Delay": 5,
      "ExecutionProviders": [
        {
          "Name": "NativeQNN",
          "Config": {
            "device_type": "NPU_CPU"
          }
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
    "Comment": "Phi 3.5 Mini Instruct, Native QNN, NPU CPU, Base prompts",
    "TempPath": "",
    "EPDependenciesConfigPath": ""
  },
  "Scenarios": [
    {
      "Name": "Phi3.5",
      "Models": [
        {
          "ModelName": "Phi-3.5-mini-instruct-qti",
          "FilePath": "file://models/phi3_5_cpu.bin",
          "DataFilePath": "file://models/phi3_5_npu.zip",
          "TokenizerPath": "https://client.mlcommons-storage.org/deps/1.0/scenario_files/llm/phi3.5/models/NativeQNN/tokenizer.zip"
        }
      ],
      "InputFilePath": [
        "https://client.mlcommons-storage.org/deps/1.5/scenario_files/llm/phi3.5/prompts/content_generation/greedy-prompt_cot.39329.json"
      ],
      "AssetsPath": [],
      "ResultsVerificationFile": "https://client.mlcommons-storage.org/deps/1.0/scenario_files/llm/generation-greedy-results.json",
      "DataVerificationFile": "",
      "Iterations": 3,
      "WarmUp": 1,
      "Delay": 5,
      "ExecutionProviders": [
        {
          "Name": "NativeQNN",
          "Config": {
            "device_type": "NPU_CPU"
          }
        }
      ]
    }
  ]
}

```

An example config for Llama2, Llama3 and Phi3.5 with files downloaded from web is available at `\data\configs\vendors_default\{model_name}\Qualcomm_NativeQNN_NPU-CPU.json`. if using `LibraryPath` is used, it should be mentioned as `IHV/NativeQNN/IHV_NativeQNN.dll`. 

## FAQ

### What devices does this backend support?

This backend supports Snapdragon (R) X Elite  device.

### Is QAIRT used to run all the models?

Yes. All the models use Qualcomm AI Runtime(QAIRT) for execution for current version.

### Path for QAIRT SDK

Default path is '<cmake-build-dir>\_deps\native_qnn_qairt_sdk-src', unless QAIRT path was provided explicitly in cmake.