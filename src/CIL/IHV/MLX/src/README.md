# 1. Model Prepration

## 1.1 Installation

- Install `mlx_lm` with `pip` or `conda`:
  - With `pip`:
  ```
  pip install mlx-lm
  ```
  - With `conda`:
  ```
  conda install -c conda-forge mlx-lm
  ```

## 1.2 Model Conversion

- Command: `mlx_lm.convert`
- Main options:
  - `--hf-path`: Path to the Hugging Face model.
  - `--mlx-path`: Path to save the MLX model.
  - `-q`: Generate a quantized model.
  - `--q-bits`: Bits per weight for quantization. (4,8)
  - `--dtype`: Type to save the non-quantized parameters. Defaults to config.json's `torch_dtype` or the current model weights dtype. ["float16", "bfloat16", "float32"]
- Example:
```
mlx_lm.convert --hf-path "meta-llama/Llama-3.1-8B" --mlx-path ".../mlx/llama3" -q --q-bits 4
```

# 2. Example config json

- Minimal example for MLX:
```
{
    "SystemConfig": {
      "Comment": "Default config for MLX",
      "TempPath": "",
      "EPDependenciesConfigPath": ""
    },
    "Scenarios": [
      {
        "Name": "Llama2",
        "Models": [
          {
            "ModelName": "Llama2",
            "FilePath": "https://client.mlcommons-storage.org/deps/1.0/scenario_files/llm/llama2/models/MLX/config.json",
            "DataFilePath": "https://client.mlcommons-storage.org/deps/1.0/scenario_files/llm/llama2/models/MLX/safetensors.zip",
            "TokenizerPath": "https://client.mlcommons-storage.org/deps/1.0/scenario_files/llm/llama2/models/MLX/tokenizer.zip"
          }
        ],
        "InputFilePath": [
          "https://client.mlcommons-storage.org/deps/1.0/scenario_files/llm/llama2/data/content_generation/greedy-prompt_cot.39329.json"
        ],
        "AssetsPath": [],
        "ResultsVerificationFile": "https://client.mlcommons-storage.org/deps/1.0/scenario_files/llm/generation-greedy-results.json",
        "DataVerificationFile": "",
        "Iterations": 1,
        "WarmUp": 1,
        "ExecutionProviders": [
          {
            "Name": "MLX",
            "Config": {
              "device_type": "GPU"
            }
          }
        ]
      }
    ]
  }
```