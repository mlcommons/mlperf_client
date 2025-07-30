# 1. Model Prepration

## 1.1 Build llama.cpp

- To get the Code:
```
git clone https://github.com/ggml-org/llama.cpp
cd llama.cpp
```

### 1.1.1 CPU Build

- Build llama.cpp using CMake:
```
cmake -B build
cmake --build build --config Release
```

### 1.1.2 For different backends and build options

- Check out [Here](https://github.com/ggml-org/llama.cpp/blob/master/docs/build.md)

## 1.2 Prepare GGUF model

### 1.2.1 Prepare venv

> Recommend using `conda` for creating venv

- Recommended Python version: 3.10
- Create and activate venv:
```
conda create --name {venv_name} python=3.10
conda activate {venv_name}
```
- Install llama.cpp requirements:
```
python -m pip install -r requirements.txt
```

### 1.2.2 Convert HF model to GGUF

- Run: `convert_hf_to_gguf.py [--options] model`
- Main options:
  - `--outfile`: Path to write to; default: based on input. {ftype} will be replaced by the outtype.
  - `--outtype`: Output format - use f32 for float32, f16 for float16, bf16 for bfloat16, q8_0 for Q8_0, tq1_0 or tq2_0 for ternary, and auto for the highest-fidelity 16-bit float type depending on the first loaded tensor type.
  - `--remote`: Read safetensors file remotely without downloading to disk. Config and tokenizer files will still be downloaded. To use this feature, you need to specify Hugging Face model repo name instead of a local directory. For example: 'HuggingFaceTB/SmolLM2-1.7B-Instruct'. Note: To access gated repo, set HF_TOKEN environment variable to your Hugging Face token.
- Example:
```
python convert_hf_to_gguf.py --outfile "./models/Llama-2-7b-F16.gguf" --outtype "f16" --remote "meta-llama/Llama-2-7b-hf"
```

## 1.3 Model Quantization

- Run: `build/bin/llama-quantize {path_to_gguf_model} {type}`
- Allowed quantization types:
```
   2  or  Q4_0    :  4.34G, +0.4685 ppl @ Llama-3-8B
   3  or  Q4_1    :  4.78G, +0.4511 ppl @ Llama-3-8B
   8  or  Q5_0    :  5.21G, +0.1316 ppl @ Llama-3-8B
   9  or  Q5_1    :  5.65G, +0.1062 ppl @ Llama-3-8B
  19  or  IQ2_XXS :  2.06 bpw quantization
  20  or  IQ2_XS  :  2.31 bpw quantization
  28  or  IQ2_S   :  2.5  bpw quantization
  29  or  IQ2_M   :  2.7  bpw quantization
  24  or  IQ1_S   :  1.56 bpw quantization
  31  or  IQ1_M   :  1.75 bpw quantization
  36  or  TQ1_0   :  1.69 bpw ternarization
  37  or  TQ2_0   :  2.06 bpw ternarization
  10  or  Q2_K    :  2.96G, +3.5199 ppl @ Llama-3-8B
  21  or  Q2_K_S  :  2.96G, +3.1836 ppl @ Llama-3-8B
  23  or  IQ3_XXS :  3.06 bpw quantization
  26  or  IQ3_S   :  3.44 bpw quantization
  27  or  IQ3_M   :  3.66 bpw quantization mix
  12  or  Q3_K    : alias for Q3_K_M
  22  or  IQ3_XS  :  3.3 bpw quantization
  11  or  Q3_K_S  :  3.41G, +1.6321 ppl @ Llama-3-8B
  12  or  Q3_K_M  :  3.74G, +0.6569 ppl @ Llama-3-8B
  13  or  Q3_K_L  :  4.03G, +0.5562 ppl @ Llama-3-8B
  25  or  IQ4_NL  :  4.50 bpw non-linear quantization
  30  or  IQ4_XS  :  4.25 bpw non-linear quantization
  15  or  Q4_K    : alias for Q4_K_M
  14  or  Q4_K_S  :  4.37G, +0.2689 ppl @ Llama-3-8B
  15  or  Q4_K_M  :  4.58G, +0.1754 ppl @ Llama-3-8B
  17  or  Q5_K    : alias for Q5_K_M
  16  or  Q5_K_S  :  5.21G, +0.1049 ppl @ Llama-3-8B
  17  or  Q5_K_M  :  5.33G, +0.0569 ppl @ Llama-3-8B
  18  or  Q6_K    :  6.14G, +0.0217 ppl @ Llama-3-8B
   7  or  Q8_0    :  7.96G, +0.0026 ppl @ Llama-3-8B
   1  or  F16     : 14.00G, +0.0020 ppl @ Mistral-7B
  32  or  BF16    : 14.00G, -0.0050 ppl @ Mistral-7B
   0  or  F32     : 26.00G              @ 7B
          COPY    : only copy tensors, no quantizing
```

- Example:
```
build/bin/llama-quantize "./models/Llama-2-7b-F16.gguf" Q4_0
```

# 2. Example config json

- Minimal example for GGML:
```
{
  "SystemConfig": {
    "Comment": "Default Llama2 GGML Metal GPU config",
    "TempPath": "",
    "EPDependenciesConfigPath": ""
  },
  "Scenarios": [
    {
      "Name": "Llama2",
      "Models": [
        {
          "ModelName": "Llama2 gguf-Q4_0",
          "FilePath": "https://client.mlcommons-storage.org/deps/1.0/scenario_files/llm/llama2/models/GGML/Llama-2-7b-chat-hf_q4.gguf",
          "TokenizerPath": "https://client.mlcommons-storage.org/deps/1.0/scenario_files/llm/llama2/models/GGML/tokenizer.zip"
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
          "Name": "Metal",
          "Config": {
            "device_type": "GPU",
            "gpu_layers": 999
          }
        }
      ]
    }
  ]
}
```