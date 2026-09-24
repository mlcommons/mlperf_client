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
- The CUDA backend is built in CI with **CUDA Toolkit 13.3.1**.

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
          "Name": "llama-cpp",
          "Config": {
            "backend": "Metal",
            "device_type": "GPU",
            "gpu_layers": 999
          }
        }
      ]
    }
  ]
}
```
# 3. Windows ARM64 CUDA build

Both the native-host and the x64-to-ARM64 cross-compiled Windows ARM64 CUDA
paths are supported; the top-level configure examples are in
`README_BUILD.md` (Windows ARM64 Build).

Pinned inputs: llama.cpp `b10751` (GIT_TAG in this CMakeLists); Ninja
bootstrap `1.12.1` for native ARM64 (SHA-256
`79C96A50E0DEAFEC212CFA85AA57C6B74003F52D9D1673DDCD1EAB1C958C5900`), `1.11.1`
fallback on other hosts.

## 3.1 Ninja bootstrap

Ninja does not have to be installed separately. Unless an explicit override is
provided, a native ARM64 GGML CUDA build bootstraps the native ARM64 Ninja
1.12.1 archive instead of accepting a possibly emulated x64 executable from
`PATH`. The x64 cross-build retains the existing host-Ninja lookup and pinned
1.11.1 fallback.

For a fully offline source build, pre-download the pinned native archive and
pass the extracted ARM64 `ninja.exe` as `-DGGML_NINJA_EXECUTABLE=<path>`.
Supplying this override makes the caller responsible for matching Ninja to the
build host architecture.

## 3.2 Native nested toolchain

On a native ARM64 host the top-level project uses the Visual Studio ARM64
generator. Its nested llama.cpp CUDA build uses llama.cpp's
`arm64-windows-llvm` toolchain with native ARM64 clang and the pinned native
Ninja. CUDA's host pass alone uses the parent project's native ARM64 MSVC
compiler.

## 3.3 Native and cross-build llama.cpp parity

The native-host and cross-compiled Windows ARM64 CUDA paths explicitly use the
same llama.cpp feature and CUDA code-generation settings:

```text
GGML_NATIVE=OFF
GGML_OPENMP=ON
GGML_CUDA=ON
GGML_VULKAN=OFF
GGML_METAL=OFF
GGML_HIP=OFF
CMAKE_CUDA_ARCHITECTURES=OFF
CUDA gencode: sm_89, sm_120a, sm_121a
```

`CMAKE_CUDA_ARCHITECTURES` is disabled because the build supplies the complete
`-gencode` list explicitly. This prevents a native build from adding a
toolkit-default architecture that is absent from a cross-build. Override the
pipe-separated `MLPERF_GGML_CUDA_ARCHS` cache value to change this list; both
native and cross-build paths derive their `-gencode` flags from that value.

OpenMP is required for the validated llama.cpp performance. A distributable
build must place the target-architecture OpenMP runtime required by
`llama_cpp_CUDA.dll` alongside that DLL. The x64-to-ARM64 build links the ARM64
LLVM OpenMP import library and therefore requires `libomp140.aarch64.dll`.
The native build also compiles llama.cpp with clang, but the exact imported
LLVM OpenMP runtime name still depends on the selected LLVM/Visual Studio
toolchain and must be checked before packaging.

The remaining differences are required toolchain mechanics: the cross-build
uses x64 Ninja, x64 `clang-cl` targeting ARM64, `VsDevCmd`, and explicit ARM64
CUDA library paths; the native nested build uses native ARM64 Ninja/clang and
native ARM64 MSVC for nvcc's host pass. Consequently, their host CPU code and
OpenMP runtime need not be bit-for-bit identical even though both produce
Release ARM64 binaries with the same explicit settings above.
