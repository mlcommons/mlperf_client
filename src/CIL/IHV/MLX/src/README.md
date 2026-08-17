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

## 1.3 Per-model recipes

All currently supported MLX models are produced with `mlx_lm.convert` (q4). The
`--hf-path` below is the upstream HuggingFace repo; `--mlx-path` is the local
output directory that is then packaged and uploaded (see 1.4).

| Model (`ModelBaseName`)      | Swift family | HuggingFace source                   | Convert command |
|------------------------------|--------------|--------------------------------------|-----------------|
| `llama_3_1_8b_instruct`      | `llama3`     | `meta-llama/Llama-3.1-8B-Instruct`   | `mlx_lm.convert --hf-path "meta-llama/Llama-3.1-8B-Instruct" --mlx-path ./mlx/llama3 -q --q-bits 4` |
| `phi_4_reasoning_14b`        | `phi4`       | `microsoft/Phi-4-reasoning`          | `mlx_lm.convert --hf-path "microsoft/Phi-4-reasoning" --mlx-path ./mlx/phi4reason -q --q-bits 4` |
| `phi_4_mini_instruct`        | `phi4`       | `microsoft/Phi-4-mini-instruct`      | `mlx_lm.convert --hf-path "microsoft/Phi-4-mini-instruct" --mlx-path ./mlx/phi4mini -q --q-bits 4` |
| `qwen_3_8b`                  | `qwen3`      | `Qwen/Qwen3-8B`                      | `mlx_lm.convert --hf-path "Qwen/Qwen3-8B" --mlx-path ./mlx/qwen3 -q --q-bits 4` |

Notes:
- **Phi-4-mini** reuses the existing `phi4` Swift path (its `model_type` is
  `phi3`); no model code is required, only the convert + packaging here.
- **Qwen3** is handled by `Qwen3.swift` (adds per-head `q_norm`/`k_norm`). Make
  sure the converted `config.json` keeps `head_dim`, `rope_theta`,
  `tie_word_embeddings` and (if present) `rope_scaling` — the Swift decoder reads
  them.
- The benchmark only runs **greedy** decoding; for reasoning/thinking models
  (`phi_4_reasoning_14b`, `qwen_3_8b`) the prompt template controls whether the
  thinking block is emitted — see `data/prompts/<model>/`.

## 1.4 Packaging & upload layout

`mlx_lm.convert` writes a directory containing `config.json`, one or more
`*.safetensors` shards and the tokenizer files. The benchmark expects three
artifacts per model, referenced from the scenario config as `FilePath`,
`DataFilePath` and `TokenizerPath`:

```
<model>/models/MLX/
├── config.json        # FilePath      (model architecture + quantization)
├── safetensors.zip    # DataFilePath  (zip of the *.safetensors shard(s))
└── tokenizer.zip      # TokenizerPath (zip of tokenizer.json / tokenizer_config.json / *.model etc.)
```

Build them from the convert output directory:
```
cd ./mlx/<model>
zip safetensors.zip *.safetensors
zip tokenizer.zip tokenizer*.json *.model special_tokens_map.json added_tokens.json merges.txt vocab.json 2>/dev/null
# config.json is uploaded as-is
```

Upload the three files to the dependencies CDN under the versioned path used by
the other models, e.g.:
```
https://client.mlcommons-storage.org/deps/<ver>/scenario_files/llm/<model>/models/MLX/{config.json,safetensors.zip,tokenizer.zip}
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
# 3. FLUX.2 Klein 4B (image / txt2img)

Unlike the LLMs, MLPerf serves Klein **pre-quantized** — the MLX EP only loads
weights, it never quantizes at runtime. Four variants are produced offline from
the diffusers bf16 model `black-forest-labs/FLUX.2-klein-4B`:

| variant | transformer | encoder (Qwen3) | VAE  |
|---------|-------------|-----------------|------|
| bf16    | bf16        | bf16            | bf16 |
| int8    | 8-bit       | 8-bit           | bf16 |
| mixed   | 8-bit       | 4-bit           | bf16 |
| int4    | 4-bit       | 4-bit           | bf16 |

## 3.1 Get the base model

Download the diffusers bf16 model (public):
```
huggingface-cli download black-forest-labs/FLUX.2-klein-4B --local-dir <model_dir>
```
`<model_dir>` now holds `transformer/ vae/ text_encoder/ tokenizer/`. For the
**bf16** variant skip to 3.3.

## 3.2 Quantize (int4 / int8 / mixed)

Quantize every 2-D Linear `*.weight` whose in-features (last dim) is divisible by
the group size 64, in `transformer/` and `text_encoder/` only — leave norms, 1-D
tensors and the whole `vae/` in bf16. Keys stay in diffusers/HF form; the EP
detects quantized tensors by the presence of `.scales` and quantizes-on-load.

```python
import mlx.core as mx, glob, os

def quantize_dir(d, bits, group_size=64):
    for f in glob.glob(os.path.join(d, "*.safetensors")):
        out = {}
        for k, v in mx.load(f).items():
            if k.endswith(".weight") and v.ndim == 2 and v.shape[-1] % group_size == 0:
                base = k[: -len(".weight")]
                out[k], out[base + ".scales"], out[base + ".biases"] = \
                    mx.quantize(v, group_size=group_size, bits=bits)
            else:
                out[k] = v
        mx.save_safetensors(f, out)

# int8 :  quantize_dir("transformer", 8); quantize_dir("text_encoder", 8)
# int4 :  quantize_dir("transformer", 4); quantize_dir("text_encoder", 4)
# mixed:  quantize_dir("transformer", 8); quantize_dir("text_encoder", 4)
```

## 3.3 model_index.json

Each model dir holds `model_index.json` beside `transformer/ vae/ text_encoder/
tokenizer/`:

```
{ "model_base_name": "flux_2_klein_4b",
  "prequantized": true, "transformer_bits": 8, "encoder_bits": 8,
  "width": 1024, "height": 1024, "steps": 4, "guidance_scale": 1.0 }
```

(bf16 model: `"prequantized": false`, omit the `*_bits`.)

## 3.4 Package & host

Two files per model on the CDN:
- `model_index.json` -> `Model.FilePath`
- `data.zip`         -> `Model.DataFilePath` — must contain `transformer/ vae/
  text_encoder/ tokenizer/` **at the zip root** (the app extracts it flat next
  to `model_index.json`). No `TokenizerPath` for image models.

```
cd <model_dir> && zip -r data.zip transformer vae text_encoder tokenizer
```

Host at e.g.
`.../scenario_files/image/flux2/klein_4b/mlx/<variant>/{model_index.json,data.zip}`
and point the scenario config's `FilePath`/`DataFilePath` there (see
`data/configs/.../flux2klein/macOS_MLX_GPU_<variant>.json`).
