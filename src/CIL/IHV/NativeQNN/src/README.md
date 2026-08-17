## Usage
QAIRT GENAI and its implementation in MLPerf require the cpu file generated from running instructions mentioned in [<code><b><path/to/mlperf_client_dev/tools/IHV/NativeQNN/cpu_model_generation/README.md></b></code>](../../../../../tools/IHV/NativeQNN/cpu_model_generation/README.md) 
and npu bin file generated from running instructions mentioned in [<code><b><path/to/mlperf_client_dev/tools/IHV/NativeQNN/npu_model_generation/README.md></b></code>](../../../../../tools/IHV/NativeQNN/npu_model_generation/README.md)

### Llama3 sample config

#### NPU-CPU
```
{
  "SystemConfig": {
    "Comment": "Llama 3.1 8B Instruct, Native QNN, NPU CPU, Base and Extended prompts",
    "TempPath": "",
    "EPDependenciesConfigPath": ""
  },
  "Scenarios": [
    {
      "Name": "Llama3",
      "Models": [
        {
          "ModelName": "llama-3.1-8B-Instruct-qti",
          "FilePath": "file://models/llama3/llama3_cpu.bin",
          "DataFilePath": "file://models/llama3/llama3_npu_hybrid.zip",
          "TokenizerPath": "file://models/llama3/tokenizer.zip",
          "NativeQNNConfigPath": "file://tools/IHV/NativeQNN/native_qnn_configs/llama_3_1_8b_instruct/native_qnn_config_npu_cpu.json",
          "HtpBackendExtConfigPath": "file://tools/IHV/NativeQNN/backend_ext_configs/llama_3_1_8b_instruct/htp_backend_ext_config_npu_cpu.json"
        }
      ],
      "InputFilePath": {
        "base": [
          "file://prompts/llama3/content_generation/greedy-prompt_cot.39329.json",
          "file://prompts/llama3/content_generation/greedy-prompt_t0.677207.json",
          "file://prompts/llama3/creative_writing/greedy-prompt_niv.123362.json",
          "file://prompts/llama3/creative_writing/greedy-prompt_niv.77134.json",
          "file://prompts/llama3/structured_text/data_csv2json.json",
          "file://prompts/llama3/structured_text/events_ics2xml.json",
          "file://prompts/llama3/code_analysis/command_parser_cpp_2k.json",
          "file://prompts/llama3/code_analysis/performance_counter_group_cpp_2k.json",
          "file://prompts/llama3/intermediate4k/booksum_4k.json",
          "file://prompts/llama3/intermediate4k/downloader_cpp_4k.json"
        ],
        "extended": []
      },
      "AssetsPath": [],
      "ResultsVerificationFile": "file://data/generation-greedy-results-1.5.json",
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

#### NPU
```
{
  "SystemConfig": {
    "Comment": "Llama 3.1 8B Instruct, Native QNN, NPU, Base and Extended prompts, Snapdragon X2 Elite only",
    "TempPath": "",
    "EPDependenciesConfigPath": ""
  },
  "Scenarios": [
    {
      "Name": "Llama3",
      "Models": [
        {
          "ModelName": "llama-3.1-8B-Instruct-qti",
          "FilePath": "file://models/llama3/llama3_cpu.bin",
          "DataFilePath": "file://models/llama3/llama3_npu.zip",
          "TokenizerPath": "file://models/llama3/tokenizer.zip",
          "NativeQNNConfigPath": "file://tools/IHV/NativeQNN/native_qnn_configs/llama_3_1_8b_instruct/native_qnn_config_npu.json",
          "HtpBackendExtConfigPath": "file://tools/IHV/NativeQNN/backend_ext_configs/llama_3_1_8b_instruct/htp_backend_ext_config_npu.json"
        }
      ],
      "InputFilePath": {
        "base": [
          "file://prompts/llama3/content_generation/greedy-prompt_cot.39329.json",
          "file://prompts/llama3/content_generation/greedy-prompt_t0.677207.json",
          "file://prompts/llama3/creative_writing/greedy-prompt_niv.123362.json",
          "file://prompts/llama3/creative_writing/greedy-prompt_niv.77134.json",
          "file://prompts/llama3/structured_text/data_csv2json.json",
          "file://prompts/llama3/structured_text/events_ics2xml.json",
          "file://prompts/llama3/code_analysis/command_parser_cpp_2k.json",
          "file://prompts/llama3/code_analysis/performance_counter_group_cpp_2k.json",
          "file://prompts/llama3/intermediate4k/booksum_4k.json",
          "file://prompts/llama3/intermediate4k/downloader_cpp_4k.json"
        ],
        "extended": []
      },
      "AssetsPath": [],
      "ResultsVerificationFile": "file://data/generation-greedy-results-1.5.json",
      "DataVerificationFile": "",
      "Iterations": 3,
      "WarmUp": 1,
      "Delay": 5,
      "ExecutionProviders": [
        {
          "Name": "NativeQNN",
          "Config": {
            "device_type": "NPU"
          }
        }
      ]
    }
  ]
}
```

#### NPU-CPU Agentic
```
{
  "SystemConfig": {
    "Comment": "Llama 3.1 8B Instruct, Native QNN, NPU CPU, Agentic",
    "TempPath": "",
    "EPDependenciesConfigPath": ""
  },
  "Scenarios": [
    {
      "Name": "Llama3",
      "Models": [
        {
          "ModelName": "llama-3.1-8B-Instruct-qti",
          "FilePath": "file://models/llama3_agentic/llama3_cpu.bin",
          "DataFilePath": "file://models/llama3_agentic/llama3_npu_hybrid.zip",
          "TokenizerPath": "file://models/llama3_agentic/tokenizer.zip",
          "NativeQNNConfigPath": "file://tools/IHV/NativeQNN/native_qnn_configs/llama_3_1_8b_instruct/native_qnn_config_npu_cpu.json",
          "HtpBackendExtConfigPath": "file://tools/IHV/NativeQNN/backend_ext_configs/llama_3_1_8b_instruct/htp_backend_ext_config_npu_cpu.json"
        }
      ],
      "InputFilePath": {
        "base": [
          "file://prompts/llama3_agentic/swe_agent/swe-agent-prompts.json"
        ],
        "extended": []
      },
      "AssetsPath": [
        "file://prompts/llama3_agentic/swe_agent/swe_warmup.md",
        "file://prompts/llama3_agentic/swe_agent/swe_system.md",
        "file://prompts/llama3_agentic/swe_agent/swe_user_0.md",
        "file://prompts/llama3_agentic/swe_agent/swe_user_1.md",
        "file://prompts/llama3_agentic/swe_agent/swe_user_2.md",
        "file://prompts/llama3_agentic/swe_agent/swe_agent_0.md",
        "file://prompts/llama3_agentic/swe_agent/swe_agent_1.md",
        "file://prompts/llama3_agentic/swe_agent/swe_agent_2.md",
        "file://prompts/llama3_agentic/tools_sandbox.zip"
      ],
      "ResultsVerificationFile": "file://data/generation-greedy-results-1.5.json",
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
      ],
      "IsAgentic": true
    }
  ]
}
```

#### NPU Agentic
```
{
  "SystemConfig": {
    "Comment": "Llama 3.1 8B Instruct, Native QNN, NPU, Agentic",
    "TempPath": "",
    "EPDependenciesConfigPath": ""
  },
  "Scenarios": [
    {
      "Name": "Llama3",
      "Models": [
        {
          "ModelName": "llama-3.1-8B-Instruct-qti",
          "FilePath": "file://models/llama3_agentic/llama3_cpu.bin",
          "DataFilePath": "file://models/llama3_agentic/llama3_npu.zip",
          "TokenizerPath": "file://models/llama3_agentic/tokenizer.zip",
          "NativeQNNConfigPath": "file://tools/IHV/NativeQNN/native_qnn_configs/llama_3_1_8b_instruct/native_qnn_config_npu.json",
          "HtpBackendExtConfigPath": "file://tools/IHV/NativeQNN/backend_ext_configs/llama_3_1_8b_instruct/htp_backend_ext_config_npu.json"
        }
      ],
      "InputFilePath": {
        "base": [
          "file://prompts/llama3_agentic/swe_agent/swe-agent-prompts.json"
        ],
        "extended": []
      },
      "AssetsPath": [
        "file://prompts/llama3_agentic/swe_agent/swe_warmup.md",
        "file://prompts/llama3_agentic/swe_agent/swe_system.md",
        "file://prompts/llama3_agentic/swe_agent/swe_user_0.md",
        "file://prompts/llama3_agentic/swe_agent/swe_user_1.md",
        "file://prompts/llama3_agentic/swe_agent/swe_user_2.md",
        "file://prompts/llama3_agentic/swe_agent/swe_agent_0.md",
        "file://prompts/llama3_agentic/swe_agent/swe_agent_1.md",
        "file://prompts/llama3_agentic/swe_agent/swe_agent_2.md",
        "file://prompts/llama3_agentic/tools_sandbox.zip"
      ],
      "ResultsVerificationFile": "file://data/generation-greedy-results-1.0.json",
      "DataVerificationFile": "",
      "Iterations": 3,
      "WarmUp": 1,
      "Delay": 5,
      "ExecutionProviders": [
        {
          "Name": "NativeQNN",
          "Config": {
            "device_type": "NPU"
          }
        }
      ],
      "IsAgentic": true
    }
  ]
}
```

### Phi4mini sample config

#### NPU-CPU
```
{
  "SystemConfig": {
    "Comment": "Phi 4 Mini Instruct, Native QNN, NPU CPU",
    "TempPath": "",
    "EPDependenciesConfigPath": ""
  },
  "Scenarios": [
    {
      "Name": "phi4mini",
      "Models": [
        {
          "ModelName": "Phi4-mini phi-4-mini-instruct-qti",
          "FilePath": "file://models/phi4mini/phi4mini_cpu.bin",
          "DataFilePath": "file://models/phi4mini/phi4mini_npu_hybrid.zip",
          "TokenizerPath": "file://models/phi4mini/tokenizer.zip",
          "NativeQNNConfigPath": "file://tools/IHV/NativeQNN/native_qnn_configs/phi_4_mini_instruct/native_qnn_config_npu_cpu.json",
          "HtpBackendExtConfigPath": "file://tools/IHV/NativeQNN/backend_ext_configs/phi_4_mini_instruct/htp_backend_ext_config_npu_cpu.json"
        }
      ],
      "InputFilePath": {
        "base": [
          "file://prompts/phi4mini/content_generation/greedy-prompt_cot.39329.json",
          "file://prompts/phi4mini/content_generation/greedy-prompt_t0.677207.json",
          "file://prompts/phi4mini/creative_writing/greedy-prompt_niv.123362.json",
          "file://prompts/phi4mini/creative_writing/greedy-prompt_niv.77134.json",
          "file://prompts/phi4mini/structured_text/data_csv2json.json",
          "file://prompts/phi4mini/structured_text/events_ics2xml.json",
          "file://prompts/phi4mini/code_analysis/command_parser_cpp_2k.json",
          "file://prompts/phi4mini/code_analysis/performance_counter_group_cpp_2k.json",
          "file://prompts/phi4mini/intermediate4k/booksum_4k.json",
          "file://prompts/phi4mini/intermediate4k/downloader_cpp_4k.json"
        ],
        "extended": []
      },
      "AssetsPath": [],
      "ResultsVerificationFile": "file://data/generation-greedy-results-1.5.json",
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

#### NPU
```
{
  "SystemConfig": {
    "Comment": "Phi 4 Mini Instruct, Native QNN, NPU",
    "TempPath": "",
    "EPDependenciesConfigPath": ""
  },
  "Scenarios": [
    {
      "Name": "phi4mini",
      "Models": [
        {
          "ModelName": "Phi4-mini phi-4-mini-instruct-qti",
          "FilePath": "file://models/phi4mini/phi4mini_cpu.bin",
          "DataFilePath": "file://models/phi4mini/phi4mini_npu.zip",
          "TokenizerPath": "file://models/phi4mini/tokenizer.zip",
          "NativeQNNConfigPath": "file://tools/IHV/NativeQNN/native_qnn_configs/phi_4_mini_instruct/native_qnn_config_npu.json",
          "HtpBackendExtConfigPath": "file://tools/IHV/NativeQNN/backend_ext_configs/phi_4_mini_instruct/htp_backend_ext_config_npu.json"
        }
      ],
      "InputFilePath": {
        "base": [
          "file://prompts/phi4mini/content_generation/greedy-prompt_cot.39329.json",
          "file://prompts/phi4mini/content_generation/greedy-prompt_t0.677207.json",
          "file://prompts/phi4mini/creative_writing/greedy-prompt_niv.123362.json",
          "file://prompts/phi4mini/creative_writing/greedy-prompt_niv.77134.json",
          "file://prompts/phi4mini/structured_text/data_csv2json.json",
          "file://prompts/phi4mini/structured_text/events_ics2xml.json",
          "file://prompts/phi4mini/code_analysis/command_parser_cpp_2k.json",
          "file://prompts/phi4mini/code_analysis/performance_counter_group_cpp_2k.json",
          "file://prompts/phi4mini/intermediate4k/booksum_4k.json",
          "file://prompts/phi4mini/intermediate4k/downloader_cpp_4k.json"
        ],
        "extended": []
      },
      "AssetsPath": [],
      "ResultsVerificationFile": "file://data/generation-greedy-results-1.5.json",
      "DataVerificationFile": "",
      "Iterations": 3,
      "WarmUp": 1,
      "Delay": 5,
      "ExecutionProviders": [
        {
          "Name": "NativeQNN",
          "Config": {
            "device_type": "NPU"
          }
        }
      ]
    }
  ]
}
```

#### NPU-CPU Agentic
```
{
  "SystemConfig": {
    "Comment": "Phi 4 Mini Instruct, Native QNN, NPU-CPU, Agentic",
    "TempPath": "",
    "EPDependenciesConfigPath": ""
  },
  "Scenarios": [
    {
      "Name": "phi4mini",
      "Models": [
        {
          "ModelName": "Phi4-mini phi-4-mini-instruct-qti",
          "FilePath": "file://models/phi4mini_agentic/phi4mini_cpu.bin",
          "DataFilePath": "file://models/phi4mini_agentic/phi4mini_npu_hybrid.zip",
          "TokenizerPath": "file://models/phi4mini_agentic/tokenizer.zip",
          "NativeQNNConfigPath": "file://tools/IHV/NativeQNN/native_qnn_configs/phi_4_mini_instruct/native_qnn_config_npu_cpu.json",
          "HtpBackendExtConfigPath": "file://tools/IHV/NativeQNN/backend_ext_configs/phi_4_mini_instruct/htp_backend_ext_config_npu_cpu.json"
        }
      ],
      "InputFilePath": {
        "base": [
          "file://prompts/phi4mini_agentic/swe_agent/swe-agent-prompts.json"
        ],
        "extended": []
      },
      "AssetsPath": [
        "file://prompts/phi4mini_agentic/swe_agent/swe_warmup.md",
        "file://prompts/phi4mini_agentic/swe_agent/swe_system.md",
        "file://prompts/phi4mini_agentic/swe_agent/swe_user_0.md",
        "file://prompts/phi4mini_agentic/swe_agent/swe_user_1.md",
        "file://prompts/phi4mini_agentic/swe_agent/swe_user_2.md",
        "file://prompts/phi4mini_agentic/swe_agent/swe_agent_0.md",
        "file://prompts/phi4mini_agentic/swe_agent/swe_agent_1.md",
        "file://prompts/phi4mini_agentic/swe_agent/swe_agent_2.md",
        "file://prompts/phi4mini_agentic/tools_sandbox.zip"
      ],
      "ResultsVerificationFile": "file://data/generation-greedy-results-1.0.json",
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
      ],
      "IsAgentic": true
    }
  ]
}
```

#### NPU Agentic
```
{
  "SystemConfig": {
    "Comment": "Phi 4 Mini Instruct, Native QNN, NPU, Agentic",
    "TempPath": "",
    "EPDependenciesConfigPath": ""
  },
  "Scenarios": [
    {
      "Name": "phi4mini",
      "Models": [
        {
          "ModelName": "Phi4-mini phi-4-mini-instruct-qti",
          "FilePath": "file://models/phi4mini_agentic/phi4mini_cpu.bin",
          "DataFilePath": "file://models/phi4mini_agentic/phi4mini_npu.zip",
          "TokenizerPath": "file://models/phi4mini_agentic/tokenizer.zip",
          "NativeQNNConfigPath": "file://tools/IHV/NativeQNN/native_qnn_configs/phi_4_mini_instruct/native_qnn_config_npu.json",
          "HtpBackendExtConfigPath": "file://tools/IHV/NativeQNN/backend_ext_configs/phi_4_mini_instruct/htp_backend_ext_config_npu.json"
        }
      ],
      "InputFilePath": {
        "base": [
          "file://prompts/phi4mini_agentic/swe_agent/swe-agent-prompts.json"
        ],
        "extended": []
      },
      "AssetsPath": [
        "file://prompts/phi4mini_agentic/swe_agent/swe_warmup.md",
        "file://prompts/phi4mini_agentic/swe_agent/swe_system.md",
        "file://prompts/phi4mini_agentic/swe_agent/swe_user_0.md",
        "file://prompts/phi4mini_agentic/swe_agent/swe_user_1.md",
        "file://prompts/phi4mini_agentic/swe_agent/swe_user_2.md",
        "file://prompts/phi4mini_agentic/swe_agent/swe_agent_0.md",
        "file://prompts/phi4mini_agentic/swe_agent/swe_agent_1.md",
        "file://prompts/phi4mini_agentic/swe_agent/swe_agent_2.md",
        "file://prompts/phi4mini_agentic/tools_sandbox.zip"
      ],
      "ResultsVerificationFile": "file://data/generation-greedy-results-1.0.json",
      "DataVerificationFile": "",
      "Iterations": 3,
      "WarmUp": 1,
      "Delay": 5,
      "ExecutionProviders": [
        {
          "Name": "NativeQNN",
          "Config": {
            "device_type": "NPU"
          }
        }
      ],
      "IsAgentic": true
    }
  ]
}
```

### Phi4 Reasoning 14B sample config

#### NPU-CPU
```
{
  "SystemConfig": {
    "Comment": "Phi 4 Reasoning 14B, Native QNN, NPU CPU, Base prompts",
    "TempPath": "",
    "EPDependenciesConfigPath": ""
  },
  "Scenarios": [
    {
      "Name": "phi4reason",
      "Models": [
        {
          "ModelName": "Phi4 phi-4-reasoning-qti",
          "FilePath": "file://models/phi4/phi4_cpu.bin",
          "DataFilePath": "file://models/phi4/phi4_npu_hybrid.zip",
          "TokenizerPath": "file://models/phi4/tokenizer.zip",
          "NativeQNNConfigPath": "file://tools/IHV/NativeQNN/native_qnn_configs/phi_4_reasoning_14b/native_qnn_config_npu_cpu.json",
          "HtpBackendExtConfigPath": "file://tools/IHV/NativeQNN/backend_ext_configs/phi_4_reasoning_14b/htp_backend_ext_config_npu_cpu.json"
        }
      ],
      "InputFilePath": {
        "base": [
          "file://prompts/phi4reason/content_generation/greedy-prompt_cot.39329.json",
          "file://prompts/phi4reason/content_generation/greedy-prompt_t0.677207.json",
          "file://prompts/phi4reason/creative_writing/greedy-prompt_niv.123362.json",
          "file://prompts/phi4reason/creative_writing/greedy-prompt_niv.77134.json",
          "file://prompts/phi4reason/structured_text/data_csv2json.json",
          "file://prompts/phi4reason/structured_text/events_ics2xml.json",
          "file://prompts/phi4reason/code_analysis/command_parser_cpp_2k.json",
          "file://prompts/phi4reason/code_analysis/performance_counter_group_cpp_2k.json",
          "file://prompts/phi4reason/intermediate4k/booksum_4k.json",
          "file://prompts/phi4reason/intermediate4k/downloader_cpp_4k.json"
        ],
        "extended": []
      },
      "AssetsPath": [],
      "ResultsVerificationFile": "file://data/generation-greedy-results-1.5.json",
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

#### NPU
```
{
  "SystemConfig": {
    "Comment": "Phi 4 Reasoning 14B, Native QNN, NPU, Base prompts",
    "TempPath": "",
    "EPDependenciesConfigPath": ""
  },
  "Scenarios": [
    {
      "Name": "phi4reason",
      "Models": [
        {
          "ModelName": "Phi4 phi-4-reasoning-qti",
          "FilePath": "file://models/phi4/phi4_cpu.bin",
          "DataFilePath": "file://models/phi4/phi4_npu.zip",
          "TokenizerPath": "file://models/phi4/tokenizer.zip",
          "NativeQNNConfigPath": "file://tools/IHV/NativeQNN/native_qnn_configs/phi_4_reasoning_14b/native_qnn_config_npu.json",
          "HtpBackendExtConfigPath": "file://tools/IHV/NativeQNN/backend_ext_configs/phi_4_reasoning_14b/htp_backend_ext_config_npu.json"
        }
      ],
      "InputFilePath": {
        "base": [
          "file://prompts/phi4reason/content_generation/greedy-prompt_cot.39329.json",
          "file://prompts/phi4reason/content_generation/greedy-prompt_t0.677207.json",
          "file://prompts/phi4reason/creative_writing/greedy-prompt_niv.123362.json",
          "file://prompts/phi4reason/creative_writing/greedy-prompt_niv.77134.json",
          "file://prompts/phi4reason/structured_text/data_csv2json.json",
          "file://prompts/phi4reason/structured_text/events_ics2xml.json",
          "file://prompts/phi4reason/code_analysis/command_parser_cpp_2k.json",
          "file://prompts/phi4reason/code_analysis/performance_counter_group_cpp_2k.json",
          "file://prompts/phi4reason/intermediate4k/booksum_4k.json",
          "file://prompts/phi4reason/intermediate4k/downloader_cpp_4k.json"
        ],
        "extended": []
      },
      "AssetsPath": [],
      "ResultsVerificationFile": "file://data/generation-greedy-results-1.5.json",
      "DataVerificationFile": "",
      "Iterations": 3,
      "WarmUp": 1,
      "Delay": 5,
      "ExecutionProviders": [
        {
          "Name": "NativeQNN",
          "Config": {
            "device_type": "NPU"
          }
        }
      ]
    }
  ]
}
```

Example configs for all device types are available at `\data\configs\vendors_default\{model_name}\` for base scenarios and `\data\configs\vendors_default\extended\{model_name}\` for extended/agentic scenarios. If `LibraryPath` is used, it should be mentioned as `IHV/NativeQNN/IHV_NativeQNN.dll`.

## FAQ

### What devices does this backend support?

This backend supports Snapdragon (R) X Elite  device.

### Is QAIRT used to run all the models?

Yes. All the models use Qualcomm AI Runtime(QAIRT) for execution for current version.

### Path for QAIRT SDK

Default path is '<cmake-build-dir>\_deps\native_qnn_qairt_sdk-src', unless QAIRT path was provided explicitly in cmake.