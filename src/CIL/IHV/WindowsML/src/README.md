
# Model Building

This document describes how to build, quantize, and compile models for WindowsML—targeting DirectML, NvTensorRtRtx, Vitis AI, and OpenVINO—with Olive and ONNXRuntime GenAI,
including recipes that reproduce quantized and compiled variants of **Llama 2 7B Chat**, **Llama 3.1 8B Instruct**, **Phi-3.5 Mini Instruct**, and **Phi 4 Reasoning 14B**.

> **Tip**  
> Each recipe fully describes the required passes and target device; you usually only need to edit the `model_path` field if you prefer a different Hugging Face repository or a local checkpoint.

---

## 1. Common Prerequisites

Follow the README provided in the examples folder of the Olive repository to install Olive and its dependencies.
[`Llama 2 7B Chat`](https://github.com/microsoft/Olive/tree/main/examples/llama2)
[`Llama 3 8B Instruct`](https://github.com/microsoft/Olive/tree/main/examples/llama3)
[`Phi 3.5 Mini Instruct`](https://github.com/microsoft/Olive/tree/main/examples/phi3_5)
[`Phi 4 Reasoning`](https://github.com/microsoft/Olive/tree/main/examples/phi4)


## 2. Running an Olive Recipe (VitisAI / OpenVINO)

### VitisAI models (NPU):

Build VitisAI NPU models using the provided recipes:

```bash
olive run --config ../recipes/VitisAI/Recipe_phi3_5_vitis_ai_config.json
olive run --config ../recipes/VitisAI/Recipe_Llama-3.1-8B-Instruct_quark_vitisai_llm.json
```

Packaging for MLPerf configuration:
- For split models (e.g., Phi‑3.5), use `embeddings.onnx` as FilePath and package the remaining model artifacts (except tokenizer files) into a single `data.zip` for DataFilePath.
- For non‑split models (e.g., Llama‑3.1‑8B‑Instruct), use `model.onnx` as FilePath and package remaining model artifacts (except tokenizer files) into `model.data.zip` for DataFilePath.

---

### OpenVINO models:

#### NPU models
```bash
olive run --config ../recipes/OpenVINO/Llama-2-7b-chat-hf_ov-int4-CHw.json
olive run --config ../recipes/OpenVINO/Llama-3.1-8B-Instruct_ov-int4-CHw.json
olive run --config ../recipes/OpenVINO/Phi-3.5-mini-instruct_ov-int4-CHw.json
olive run --config ../recipes/OpenVINO/Phi-4-reasoning_ov-int4-CHw.json

```

#### GPU models
```bash
olive run --config ../recipes/OpenVINO/Llama-2-7b-chat-hf_ov-int4-GRw.json
olive run --config ../recipes/OpenVINO/Llama-3.1-8B-Instruct_ov-int4-GRw.json
olive run --config ../recipes/OpenVINO/Phi-3.5-mini-instruct_ov-int4-GRw.json
olive run --config ../recipes/OpenVINO/Phi-4-reasoning_ov-int4-GRw.json
```

#### GPU model config example json file for Llama-2-7b-chat-hf_ov-int4-GRw
Change input_model and output_dir accordingly
```bash
$ more Llama-2-7b-chat-hf-ov-gw.json
{
    "input_model": { "type": "HfModel", "model_path": "meta-llama/Llama-2-7b-chat-hf" },
    "systems": {
        "local_system": {
            "type": "LocalSystem",
            "accelerators": [ { "execution_providers": [ "OpenVINOExecutionProvider" ] } ]
        }
    },
    "passes": {
        "optimum_convert": {
            "type": "OpenVINOOptimumConversion",
            "extra_args": { "device": "gpu" },
            "ov_quant_config": {
                "weight_format": "int4",
                "group_size": 128,
                "dataset": "wikitext2",
                "ratio": 1,
                "all_layers": true,
                "awq": true,
                "scale_estimation": true,
                "sym": true,
                "trust_remote_code": true,
                "backup_precision": "int8_asym",
                "sensitivity_metric": "weight_quantization_error"
            }
        },
        "io_update": { "type": "OpenVINOIoUpdate", "static": false, "reuse_cache": true },
        "encapsulation": {
            "type": "OpenVINOEncapsulation",
            "target_device": "gpu",
            "keep_ov_dynamic_dims": true,
            "ov_version": "2025.2",
            "reuse_cache": true
        }
    },
    "search_strategy": false,
    "host": "local_system",
    "cache_dir": "cache",
    "clean_cache": true,
    "evaluate_input_model": false,
    "output_dir": "models/windowsml/Llama-2-7b-chat-hf_ov-int4-GRw"
}
```

#### NPU model config example json file for Llama-2-7b-chat-hf_ov-int4-CHw
Change input_model and output_dir accordingly
```bash
$ more Llama-2-7b-chat-hf-ov_chw.json
{
    "input_model": { "type": "HfModel", "model_path": "meta-llama/Llama-2-7b-chat-hf" },
    "systems": {
        "local_system": {
            "type": "LocalSystem",
            "accelerators": [ { "execution_providers": [ "OpenVINOExecutionProvider" ] } ]
        }
    },
    "passes": {
        "optimum_convert": {
            "type": "OpenVINOOptimumConversion",
            "extra_args": { "device": "npu" },
            "ov_quant_config": {
                "weight_format": "int4",
                "group_size": -1,
                "dataset": "wikitext2",
                "ratio": 1,
                "all_layers": true,
                "awq": true,
                "scale_estimation": true,
                "sym": true,
                "trust_remote_code": true,
                "backup_precision": "int8_asym",
                "sensitivity_metric": "weight_quantization_error"
            }
        },
        "io_update": { "type": "OpenVINOIoUpdate", "static": false, "reuse_cache": true },
        "encapsulation": {
            "type": "OpenVINOEncapsulation",
            "target_device": "npu",
            "keep_ov_dynamic_dims": true,
            "ov_version": "2025.2",
            "reuse_cache": true
        }
    },
    "search_strategy": false,
    "host": "local_system",
    "cache_dir": "cache",
    "clean_cache": true,
    "evaluate_input_model": false,
    "output_dir": "models/windowsml/Llama-2-7b-chat-hf_ov-int4-CHw"
}
```



## 3. Building DirectML‑Optimised Models with ONNXRuntime GenAI

Use ONNXRuntime GenAI’s **Model Builder**.

### 3.1 Prerequisites

```bash
pip install numpy transformers torch onnx onnxruntime
pip install onnxruntime-genai-directml --pre
```

### 3.2 Builder commands

| Model | Command |
|-------|---------|
| **Llama 2 7B Chat** | `python -m onnxruntime_genai.models.builder -m meta-llama/Llama-2-7b-chat-hf -e dml -p int4 -o ./llama-2-7b-chat-dml/ -c llama2-dml_cache` |
| **Llama 3.1 8B Instruct** | `python -m onnxruntime_genai.models.builder -m meta-llama/Llama-3.1-8B-Instruct -e dml -p int4 -o ./llama-3.1-8b-instruct-dml/ -c llama3-dml_cache` |
| **Phi 3.5 Mini Instruct** | `python -m onnxruntime_genai.models.builder -m microsoft/Phi-3.5-mini-instruct -e dml -p int4 -o ./phi-3.5-mini-instruct-dml/ -c phi-3.5-mini-instruct-dml_cache` |
| **Phi 4 Reasoning 14B** | `python -m onnxruntime_genai.models.builder -m microsoft/Phi-4-reasoning -e dml -p int4 -o ./phi-4-reasoning-dml/ -c phi-4-reasoning-dml_cache` |

Each command:

1. Downloads the original HF weights.  
2. Converts to ONNX, applies **int4** weight‑only quantisation.
3. Emits a self‑contained folder containing:
   * `model.onnx`, `model.onnx.data`
   * `genai_config.json`
   * Tokeniser files (`tokenizer.json`, `tokenizer_config.json`, `special_tokens_map.json`)

---

## 4. Example MLPerf Entry for a DirectML‑Packaged Model

```jsonc
{
  "SystemConfig": {
    "Comment": "WindowsML config.",
    "TempPath": ""
  },
  "Scenarios": [
    {
      "Name": "Phi3.5",
      "Models": [
        {
          "ModelName": "phi-3.5-mini_instruct-dml",
          "FilePath": "https://client.mlcommons-storage.org/deps/1.5/scenario_files/llm/phi3.5/models/WindowsML/GPU/DirectML/model.onnx",
          "DataFilePath": "https://client.mlcommons-storage.org/deps/1.5/scenario_files/llm/phi3.5/models/WindowsML/GPU/DirectML/model.onnx.data.zip",
          "TokenizerPath": "https://client.mlcommons-storage.org/deps/1.5/scenario_files/llm/phi3.5/models/WindowsML/GPU/DirectML/tokenizer.zip"
        }
      ],
      "InputFilePath": [
        "https://client.mlcommons-storage.org/deps/1.5/scenario_files/llm/phi3.5/prompts/content_generation/greedy-prompt_cot.39329.json",
        "https://client.mlcommons-storage.org/deps/1.5/scenario_files/llm/phi3.5/prompts/content_generation/greedy-prompt_t0.677207.json",
        "https://client.mlcommons-storage.org/deps/1.5/scenario_files/llm/phi3.5/prompts/creative_writing/greedy-prompt_niv.123362.json",
        "https://client.mlcommons-storage.org/deps/1.5/scenario_files/llm/phi3.5/prompts/creative_writing/greedy-prompt_niv.77134.json",
        "https://client.mlcommons-storage.org/deps/1.5/scenario_files/llm/phi3.5/prompts/summarization/greedy-prompt_flan.1221607.json",
        "https://client.mlcommons-storage.org/deps/1.5/scenario_files/llm/phi3.5/prompts/summarization/greedy-prompt_flan.1221694.json",
        "https://client.mlcommons-storage.org/deps/1.5/scenario_files/llm/phi3.5/prompts/summarization2k/greedy-prompt_flan.1854150.json",
        "https://client.mlcommons-storage.org/deps/1.5/scenario_files/llm/phi3.5/prompts/summarization2k/greedy-prompt_flan.997680.json",
        "https://client.mlcommons-storage.org/deps/1.5/scenario_files/llm/phi3.5/prompts/code_analysis/command_parser_cpp_2k.json",
        "https://client.mlcommons-storage.org/deps/1.5/scenario_files/llm/phi3.5/prompts/code_analysis/performance_counter_group_cpp_2k.json"
      ],
      "AssetsPath": [],
      "ResultsVerificationFile": "https://client.mlcommons-storage.org/deps/1.5/scenario_files/llm/generation-greedy-results.json",
      "DataVerificationFile": "",
      "Iterations": 5,
      "ExecutionProviders": [
        {
          "Name": "WindowsML",
          "Config": {
            "device_type": "GPU",
            "device_id": 0
          }
        }
      ]
    }
  ]
}
```

The example config file should work on DirectML compatible devices from WindowsML enumeration.

---
## 5. Building WinML + NvTensorRtRtx optimized Model
WinML models for the Nvidia IHV path are build similar to the ONNXRuntime GenAI DirectML path, with a slightly different build command and updated Python packages.
## Prerequisites
1. Install Python (3.8 or later recommended)
2. Install ONNXRuntime GenAI (Pre-release) and its dependencies:
```
pip install numpy transformers torch onnx onnx_ir onnxruntime-gpu
pip install onnxruntime-genai --pre
```
## Building a INT4 Quantized ONNX Model for NvTensorRtRtx
Use the following command to build a ONNX model to use with NvTensorRtRtx in WinML:
```
# Build llama-3.1-8B Model
python -m onnxruntime_genai.models.builder -m meta-llama/Llama-3.1-8B -e NvTensorRtRtx -p int4 -o ./Llama-3.1-8B-trt-rtx/ -c ./cache --extra_options use_qdq=1

# Build phi-3.5-mini-instruct Model
python -m onnxruntime_genai.models.builder -m microsoft/Phi-3.5-mini-instruct -e NvTensorRtRtx -p int4 -o ./Phi-3.5-mini-instruct-trt-rtx/ -c ./cache --extra_options use_qdq=1

# Build phi-4-reasoning Model
python -m onnxruntime_genai.models.builder -m microsoft/Phi-4-reasoning -e NvTensorRtRtx -p int4 -o ./Phi-4-reasoning-trt-rtx/ -c ./cache --extra_options use_qdq=1
```

This command:

- Uses the Llama-3.1-8B / phi-3.5-mini-instruct / phi-4-reasoning model from huggingface
- Targets the NvTensorRtRtx execution provider (`-e NvTensorRtRtx`)
- Applies int4 quantization (`-p int4`)
- Outputs to the specified directory (`-o ./<model_name>-trt/`)
- Uses a cache for faster processing (`-c cache`)
- Specifies the use of Q/DQ nodes (`--extra_options use_qdq=1`)

The result is a folder containing all necessary components for ONNXRuntime GenAI:

- Model (`model.onnx`)
- Weights(`model.onnx.data`)
- Tokenizer(`tokenizer.json, special_tokens_map.json`, )
- Configuration files (`genai_config.json, tokenizer_config.json`)

**Note**: The created model is specific to the chosen execution provider (EP). In this case, the model can only be executed with NvTensorRtRtx.

## 6. Example MLPerf Entry for a WinML + NvTensorRtRtx Model

```jsonc
{
  "SystemConfig": {
    "Comment": "Llama 3.1 8B Instruct, WindowsML TensorRT RTX, GPU",
    "TempPath": "",
    "EPDependenciesConfigPath": ""
  },
  "Scenarios": [
    {
      "Name": "Llama3.1-8B",
      "Models": [
        {
          "ModelName": "Llama-3.1-8B-trt-rtx",
          "FilePath": "https://client.mlcommons-storage.org/deps/1.5/scenario_files/llm/llama3/models/WindowsML/GPU/NvTensorRtRtx/model.onnx",
          "DataFilePath": "https://client.mlcommons-storage.org/deps/1.5/scenario_files/llm/llama3/models/WindowsML/GPU/NvTensorRtRtx/model.onnx.data",
          "TokenizerPath": "https://client.mlcommons-storage.org/deps/1.5/scenario_files/llm/llama3/models/WindowsML/GPU/NvTensorRtRtx/tokenizer.zip"
        }
      ],
      "InputFilePath": {
        "base": [
          "https://client.mlcommons-storage.org/deps/1.5/scenario_files/llm/llama3/prompts/content_generation/greedy-prompt_cot.39329.json",
          "https://client.mlcommons-storage.org/deps/1.5/scenario_files/llm/llama3/prompts/content_generation/greedy-prompt_t0.677207.json",
          "https://client.mlcommons-storage.org/deps/1.5/scenario_files/llm/llama3/prompts/creative_writing/greedy-prompt_niv.123362.json",
          "https://client.mlcommons-storage.org/deps/1.5/scenario_files/llm/llama3/prompts/creative_writing/greedy-prompt_niv.77134.json",
          "https://client.mlcommons-storage.org/deps/1.5/scenario_files/llm/llama3/prompts/summarization/greedy-prompt_flan.1221607.json",
          "https://client.mlcommons-storage.org/deps/1.5/scenario_files/llm/llama3/prompts/summarization/greedy-prompt_flan.1221694.json",
          "https://client.mlcommons-storage.org/deps/1.5/scenario_files/llm/llama3/prompts/summarization2k/greedy-prompt_flan.1854150.json",
          "https://client.mlcommons-storage.org/deps/1.5/scenario_files/llm/llama3/prompts/summarization2k/greedy-prompt_flan.997680.json",
          "https://client.mlcommons-storage.org/deps/1.5/scenario_files/llm/llama3/prompts/code_analysis/command_parser_cpp_2k.json",
          "https://client.mlcommons-storage.org/deps/1.5/scenario_files/llm/llama3/prompts/code_analysis/performance_counter_group_cpp_2k.json"
        ],
        "extended": [
          "https://client.mlcommons-storage.org/deps/1.5/scenario_files/llm/llama3/prompts/intermediate4k/booksum_4k.json",
          "https://client.mlcommons-storage.org/deps/1.5/scenario_files/llm/llama3/prompts/intermediate4k/downloader_cpp_4k.json",
          "https://client.mlcommons-storage.org/deps/1.5/scenario_files/llm/llama3/prompts/substantial8k/booksum_8k.json",
          "https://client.mlcommons-storage.org/deps/1.5/scenario_files/llm/llama3/prompts/substantial8k/gov_report_train_infrastructure.json"
        ]
      },
      "AssetsPath": [],
      "ResultsVerificationFile": "https://client.mlcommons-storage.org/deps/1.5/scenario_files/llm/generation-greedy-results.json",
      "DataVerificationFile": "",
      "Iterations": 3,
      "Delay": 0,
      "ExecutionProviders": [
        {
          "Name": "WindowsML",
          "Config": {
            "device_type": "GPU",
            "device_ep": "NvTensorRtRtx",
            "device_vendor": "NVIDIA"
          }
        }
      ]
    }
  ]
}
```

This config will run on NVIDIA compatible hardware using WinML with NvTensorRtRtx. 

## 7. Troubleshooting & Tips

- Ensure you have the correct version of Olive installed.
- Check the ONNX version installed, ONNX==1.17, for IR_Version_ = 10
- For DirectML, ensure you have the latest ONNXRuntime GenAI package installed with DirectML support.
- WindowsML path tries to run the model on all devices. You can use `device_ep` and/or `device_vendor` config options to specify the target EP and Vendor if the model provided is not compatible with all devices on this path.
```
 "ExecutionProviders": [
        {
          "Name": "WindowsML",
          "Config": {
            "device_type": "GPU",
            "device_ep": "OpenVINO",
            "device_vendor": "Intel"
          },
        }
      ]
```

---

### References

* Olive repo – <https://github.com/microsoft/Olive>  
* ONNXRuntime GenAI docs – <https://onnxruntime.ai/docs/genai/>  
