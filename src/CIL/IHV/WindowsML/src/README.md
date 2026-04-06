
# Model Building

This document describes how to build, quantize, and compile models for WindowsML—targeting DirectML, NvTensorRtRtx, Vitis AI, and OpenVINO—with Olive and ONNXRuntime GenAI,
including recipes that reproduce quantized and compiled variants of **Llama 2 7B Chat**, **Llama 3.1 8B Instruct**, **Phi-3.5 Mini Instruct**, and **Phi 4 Reasoning 14B**.

**How to use this README**

| Goal | Section |
|------|---------|
| Olive (VitisAI / OpenVINO / QNN) | **§1**–**§2** |
| DirectML via ONNX Runtime GenAI | **§3**; MLPerf JSON example **§4** |
| NVIDIA TensorRT RTX EP (NvTensorRtRtx) | **§5** (per model); MLPerf JSON **§6** |

**NvTensorRtRtx (§5)** — **Windows**, **CUDA-capable NVIDIA GPU**. Use one virtual environment for **§5.1** and **§5.2** (Llama 3.1 and Phi‑4) and a different one for **§5.3** (Phi‑3.5); do not mix those package sets.

| Track | Sections | Python | `onnxruntime-gpu` | `onnxruntime-genai` |
|-------|----------|--------|-------------------|---------------------|
| Llama 3.1, Phi‑4 | §5.1, §5.2 | 3.11, 3.12, or 3.13 | 1.24.1 | 0.12.2 |
| Phi‑3.5 | §5.3 | 3.10 | 1.23.0 | 0.9.2 |

---

## 1. Common Prerequisites

Follow the README provided in the examples folder of the Olive repository to install Olive and its dependencies.
[`Llama 2 7B Chat`](https://github.com/microsoft/olive-recipes/tree/main/meta-llama-Llama-2-7b-chat-hf)
[`Llama 3 8B Instruct`](https://github.com/microsoft/olive-recipes/tree/main/meta-llama-Llama-3.1-8B-Instruct)
[`Phi 3.5 Mini Instruct`](https://github.com/microsoft/olive-recipes/tree/main/microsoft-Phi-3.5-mini-instruct)
[`Phi 4 Reasoning`](https://github.com/microsoft/olive-recipes/tree/main/microsoft-Phi-4-mini-reasoning)


## 2. Running an Olive Recipe (VitisAI / OpenVINO / QNN)

The `olive run --config …` paths below use **`../recipes/`** relative to the working directory your Olive layout expects (typically next to a [olive-recipes](https://github.com/microsoft/olive-recipes) clone). Adjust the path so it points at the matching JSON under your **`recipes`** tree.

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

### QNN models

#### Preparing the environment

Clone the Olive recipe repository from <https://github.com/microsoft/olive-recipes> and checkout commit id cf46f765cdaf2b782e6184ebe681f9b5c508d32a.
This will provide the recipe and binary versions needed for this benchmark version.

#### Creating the Phi3.5 and LLama3.1 models

For recipe select the x_elite_config.json as it will work for all Snapdragon X generations. 
NOTE: For X2 there is another recipe but that is not used in this benchmark.

- Phi3.5: Follow the README.md in olive-recipes\microsoft-Phi-3.5-mini-instruct\QNN. 
- LLama3.1: Follow the README.md in olive-recipes\meta-llama-Llama-3.1-8B-Instruct\QNN. 

## 3. Building DirectML‑Optimised Models with ONNXRuntime GenAI

Use ONNXRuntime GenAI’s **Model Builder** (`python -m onnxruntime_genai.models.builder`) on **Windows** with a DirectML-capable GPU/driver. Install a **`onnxruntime-genai-directml`** release and the **`onnxruntime-directml`** build its release notes specify ([GenAI install](https://onnxruntime.ai/docs/genai/howto/install.html)).

### 3.1 Prerequisites

Use a **Python** version supported by the **`onnxruntime-genai-directml`** wheel you install.

```bash
pip install numpy transformers torch onnx onnxruntime-directml
pip install onnxruntime-genai-directml
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

The example config targets hosted assets. For outputs from **§3**, set **`FilePath`**, **`DataFilePath`**, and **`TokenizerPath`** to your built **`model.onnx`**, **`model.onnx.data`** (or packaged data zip), and **`tokenizer.zip`** paths.

---

## 5. WinML + NvTensorRtRtx

Each subsection is a full recipe: environment, model build, then **`tokenizer.zip`**. The environment split at the top of this document applies.

### 5.1 Llama 3.1 8B Instruct

**1. Python** — **3.13**.

**2. Packages**

| Package | Version |
|---------|---------|
| onnxruntime-gpu | 1.24.1 |
| onnxruntime-genai | 0.12.2 |
| transformers | 5.4.0 |

```powershell
pip install numpy torch onnx onnx_ir
pip install onnxruntime-gpu==1.24.1
pip install onnxruntime-genai==0.12.2 --no-deps
pip install transformers==5.4.0
```

**3. Hugging Face** — Run **`huggingface-cli login`** so **`meta-llama/Llama-3.1-8B-Instruct`** is authorized before step **4**.

**4. Build**

```bash
python -m onnxruntime_genai.models.builder -m meta-llama/Llama-3.1-8B-Instruct -e NvTensorRtRtx -p int4 -o ./llama-3.1-8b-instruct-trt-rtx/ -c ./cache-llama31-trt-rtx --extra_options use_qdq=1
```

**5. MLPerf weights** — **`./llama-3.1-8b-instruct-trt-rtx/model.onnx`**, **`./llama-3.1-8b-instruct-trt-rtx/model.onnx.data`**.

**6. `tokenizer.zip`**

1. Create an empty **staging** folder.
2. Copy **`genai_config.json`** from **`./llama-3.1-8b-instruct-trt-rtx/`** into staging.
3. Copy every **`*.jinja`** file from **`./llama-3.1-8b-instruct-trt-rtx/`** into staging.
4. Open **`./cache-llama31-trt-rtx/models--meta-llama--Llama-3.1-8B-Instruct/snapshots/`**, enter the sole revision subdirectory, and copy **`tokenizer.json`**, **`tokenizer_config.json`**, **`special_tokens_map.json`**, **`tokenizer.model`**, and **`added_tokens.json`** into staging.
5. In staging **`tokenizer_config.json`**: set **`"tokenizer_class"`** to **`"PreTrainedTokenizerFast"`**. Remove the **`backend`** field.
6. In staging **`genai_config.json`**: set **`search.max_length`** to **`384`**.
7. Zip the **contents** of staging at the root of **`tokenizer.zip`**. Place **`tokenizer.zip`** in **`./llama-3.1-8b-instruct-trt-rtx/`** next to **`model.onnx`**.
8. In **`./llama-3.1-8b-instruct-trt-rtx/`**, delete **`genai_config.json`**, tokenizer JSON files, **`*.jinja`**, and any other loose tokenizer files the builder wrote. The folder must end with only **`model.onnx`**, **`model.onnx.data`**, and **`tokenizer.zip`**.

---

### 5.2 Phi‑4 reasoning

**1. Python** — **3.13**.

**2. Packages**

| Package | Version |
|---------|---------|
| onnxruntime-gpu | 1.24.1 |
| onnxruntime-genai | 0.12.2 |
| transformers | 5.4.0 |

```powershell
pip install numpy torch onnx onnx_ir
pip install onnxruntime-gpu==1.24.1
pip install onnxruntime-genai==0.12.2 --no-deps
pip install transformers==5.4.0
```

**3. Hugging Face** — Repo **`microsoft/Phi-4-reasoning`**.

**4. Build**

```bash
python -m onnxruntime_genai.models.builder -m microsoft/Phi-4-reasoning -e NvTensorRtRtx -p int4 -o ./Phi-4-reasoning-trt-rtx/ -c ./cache-phi4-trt-rtx --extra_options use_qdq=1
```

**5. MLPerf weights** — **`./Phi-4-reasoning-trt-rtx/model.onnx`**, **`./Phi-4-reasoning-trt-rtx/model.onnx.data`**.

**6. `tokenizer.zip`**

1. Create an empty **staging** folder.
2. Copy **`genai_config.json`** from **`./Phi-4-reasoning-trt-rtx/`** into staging.
3. Copy every **`*.jinja`** file from **`./Phi-4-reasoning-trt-rtx/`** into staging.
4. Open **`./cache-phi4-trt-rtx/models--microsoft--Phi-4-reasoning/snapshots/`**, enter the sole revision subdirectory, and copy **`tokenizer.json`**, **`tokenizer_config.json`**, **`special_tokens_map.json`**, **`vocab.json`**, and **`added_tokens.json`** into staging.
5. In staging **`tokenizer_config.json`**: set **`"tokenizer_class"`** to **`"PreTrainedTokenizerFast"`**. Remove the **`backend`** field.
6. In staging **`genai_config.json`**: set **`search.max_length`** to **`384`**.
7. Zip the **contents** of staging at the root of **`tokenizer.zip`**. Place **`tokenizer.zip`** in **`./Phi-4-reasoning-trt-rtx/`** next to **`model.onnx`**.
8. In **`./Phi-4-reasoning-trt-rtx/`**, delete **`genai_config.json`**, tokenizer JSON files, **`*.jinja`**, **`vocab.json`**, and any other loose tokenizer files the builder wrote. The folder must end with only **`model.onnx`**, **`model.onnx.data`**, and **`tokenizer.zip`**.

---

### 5.3 Phi‑3.5 Mini Instruct

**1. Python** — **3.10**.

**2. Packages** — Install in this order:

| Package | Version |
|---------|---------|
| onnxruntime-gpu | 1.23.0 |
| onnxruntime-genai | 0.9.2 |
| transformers | 4.46.3 |
| huggingface-hub | 0.36.2 |

```powershell
pip install numpy torch onnx onnx_ir
pip install onnxruntime-gpu==1.23.0
pip install onnxruntime-genai==0.9.2 --no-deps
pip install transformers==4.46.3 huggingface-hub==0.36.2
```

**3. Hugging Face** — Repo **`microsoft/Phi-3.5-mini-instruct`**.

**4. Build**

```bash
python -m onnxruntime_genai.models.builder -m microsoft/Phi-3.5-mini-instruct -e NvTensorRtRtx -p int4 -o ./Phi-3.5-mini-instruct-trt-rtx/ -c ./cache-phi35-trt-rtx --extra_options use_qdq=1
```

**5. MLPerf weights** — **`./Phi-3.5-mini-instruct-trt-rtx/model.onnx`**, **`./Phi-3.5-mini-instruct-trt-rtx/model.onnx.data`**.

**6. `tokenizer.zip`**

1. Create an empty **staging** folder.
2. Copy **`genai_config.json`** from **`./Phi-3.5-mini-instruct-trt-rtx/`** into staging.
3. Copy every **`*.jinja`** file from **`./Phi-3.5-mini-instruct-trt-rtx/`** into staging.
4. Open **`./cache-phi35-trt-rtx/models--microsoft--Phi-3.5-mini-instruct/snapshots/`**, enter the sole revision subdirectory, and copy **`tokenizer.json`**, **`tokenizer_config.json`**, **`special_tokens_map.json`**, **`tokenizer.model`**, and **`added_tokens.json`** into staging.
5. In staging **`tokenizer_config.json`**: set **`"tokenizer_class"`** to **`"LlamaTokenizer"`**. Remove the **`backend`** field.
6. In staging **`genai_config.json`**: set **`search.max_length`** to **`2304`**.
7. In staging **`genai_config.json`**, under **`model.decoder.sliding_window`**, set **`"layers"`** to **`[]`**.
8. Zip the **contents** of staging at the root of **`tokenizer.zip`**. Place **`tokenizer.zip`** in **`./Phi-3.5-mini-instruct-trt-rtx/`** next to **`model.onnx`**.
9. In **`./Phi-3.5-mini-instruct-trt-rtx/`**, delete **`genai_config.json`**, tokenizer JSON files, **`*.jinja`**, and any other loose tokenizer files the builder wrote. The folder must end with only **`model.onnx`**, **`model.onnx.data`**, and **`tokenizer.zip`**.

---

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
          "ModelName": "Llama-3.1-8B-Instruct-trt-rtx",
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

This JSON matches **Llama 3.1** NvTensorRtRtx hosted assets. Build weights and **`tokenizer.zip`** per **§5.1**, then point **`FilePath`**, **`DataFilePath`**, and **`TokenizerPath`** at your outputs if you do not use the URLs below.

**Phi‑3.5 Mini, NvTensorRtRtx** — Use the same **`ExecutionProviders`** block as in the JSON above (`device_ep` **`NvTensorRtRtx`**, `device_vendor` **`NVIDIA`**). Use **`InputFilePath`** as a **flat array** of prompt URLs—the same entries as **§4** (`phi3.5/prompts/…`). Set **`Models`** to your **`model.onnx`**, **`model.onnx.data`**, and **`tokenizer.zip`** from **§5.3**, or to the hosted paths under **`llm/phi3.5/models/WindowsML/GPU/NvTensorRtRtx/`** (same URL pattern as **§4** but replace **`DirectML`** with **`NvTensorRtRtx`** in each model path).

**Phi‑4 reasoning, NvTensorRtRtx** — Keep the same **`ExecutionProviders`** shape as below. Copy **`Models`**, **`InputFilePath`** (and any scenario-specific fields) from the MLPerf Inference client’s **Phi‑4** + **WindowsML** + **NvTensorRtRtx** scenario file under **`scenario_files/llm/phi4/`** (exact name varies by client version). Build artifacts per **§5.2**.

## 7. Troubleshooting & Tips

- Ensure you have the correct version of Olive installed.
- Check the ONNX / opset requirements for your installed **`onnxruntime`** (release notes supersede a fixed **`ONNX==1.17`** pin unless your pipeline still requires it).
- **NvTensorRtRtx:** If **`import onnxruntime`** fails or loads CPU-only, reinstall: **`onnxruntime-gpu`** first, then **`onnxruntime-genai --no-deps`**, then **`pip check`**.
- **Tokenizer zip:** **§5.1** / **§5.2**: **`PreTrainedTokenizerFast`**, no **`backend`**. **§5.3** (Phi‑3.5): **`LlamaTokenizer`**, no **`backend`**. Do not zip only the tokenizer files from the NvTensorRtRtx build output folder—include Hub tokenizer files per those steps.
- For DirectML, pin **`onnxruntime-genai-directml`** to a version compatible with your **`onnxruntime-directml`** install.
- WindowsML may enumerate multiple devices. Use **`device_ep`** and **`device_vendor`** when the model targets a specific EP (NvTensorRtRtx example in §6; Intel OpenVINO example below).

Example **`ExecutionProviders`** snippet for **OpenVINO** on GPU:

```jsonc
"ExecutionProviders": [
  {
    "Name": "WindowsML",
    "Config": {
      "device_type": "GPU",
      "device_ep": "OpenVINO",
      "device_vendor": "Intel"
    }
  }
]
```

---

### References

* Olive recipe repo – <https://github.com/microsoft/olive-recipes>  
* Olive repo – <https://github.com/microsoft/Olive>  
* ONNX Runtime GenAI docs – <https://onnxruntime.ai/docs/genai/>  
* ONNX Runtime GenAI releases (ORT compatibility) – <https://github.com/microsoft/onnxruntime-genai/releases>  
