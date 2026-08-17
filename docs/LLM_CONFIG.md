# LLM Configuration Reference

This document describes the JSON configuration schema used for LLM prompts and the search parameters consumed by each IHV backend.

## Prompt JSON Schema

```json
{
  "model_config": {
    "context_length": 4096,
    "search": {
      "method": "greedy",
      "temperature": 0.6,
      "top_k": 1,
      "top_p": 0.9,
      "stop_on_eos": true,
      "max_length": 256
    }
  },
  "prompts": [
    { "system": "<system instruction>", "user": "<user content>" },
    "<legacy single-string prompt>"
  ],
  "category": "Content Generation",
  "apply_chat_template": true,
  "prompt_files": false
}
```

### Prompt entry forms

Each item in `prompts` may be either:

- **Object** `{ "system"?: string, "user": string }` — the recommended format. The runtime builds a chat message list and calls the tokenizer's chat template. `apply_chat_template` is implicit for object entries (the template is always applied; no embedded-template detection is run on object entries).
- **String** — legacy form. Kept for backward compatibility with prompts that contain a pre-applied chat template, or for raw text. The runtime first tries to detect an embedded template; if none is detected and `apply_chat_template` is `true`, it wraps the string as a single user message and renders the template.

Items can be mixed within one file. Each item is templated (or not) independently.

### Loading prompts from files

When `prompt_files` is `true`, prompt values are treated as file paths relative to the directory containing the input JSON file (absolute paths also work). The runtime reads each file as plain text and substitutes the content in place before tokenization.

For **string** entries the entire string is a single path. For **object** entries every string property value is resolved as a path independently, so different roles can reference different files. Example:

```json
{
  "prompt_files": true,
  "prompts": [
    { "system": "system_instruction.txt", "user": "user_query.txt" },
    "single_prompt.txt"
  ]
}
```

If any referenced file cannot be opened, the benchmark run fails with an error message that includes the resolved path.

### Required fields

| Field | Type | Description |
|---|---|---|
| `model_config.context_length` | int | Model context window size |
| `model_config.search.method` | string | `"greedy"`, `"top_k"`, or `"top_p"` |
| `model_config.search.temperature` | number | Sampling temperature |
| `model_config.search.top_k` | int | Top-K sampling parameter |
| `model_config.search.top_p` | number | Top-P (nucleus) sampling parameter |
| `model_config.search.stop_on_eos` | bool | Whether to stop generation on EOS token |
| `model_config.search.max_length` | int | Maximum number of tokens to generate |
| `prompts` | (string \| object)[] | Array of prompt entries (see [Prompt entry forms](#prompt-entry-forms)) |

### Optional fields

| Field | Type | Default | Description |
|---|---|---|---|
| `category` | string | — | Prompt category label |
| `apply_chat_template` | bool | `false` | When `true`, the C++ executable applies the model's chat template to string prompts before tokenization. Object-form prompts are always templated regardless of this flag. |
| `prompt_files` | bool | `false` | When `true`, prompt strings are treated as file paths relative to the input JSON file's directory and loaded as plain text before tokenization. See [Loading prompts from files](#loading-prompts-from-files). |

### Deprecated fields (backward compatible)

These fields are accepted but ignored by the C++ runtime. Old prompts containing them will still load without errors.

| Field | Status |
|---|---|
| `model_config.model.context_length` | Accepted — equivalent to the flat `model_config.context_length`. Flat form takes precedence when both are present. |
| `model_config.model.bos_token_id` | Ignored — tokenizer handles BOS automatically |
| `model_config.model.eos_token_id` | Ignored — backends use their own EOS detection |
| `model_config.model.vocab_size` | Ignored — never used at runtime |

## IHV Backend Parameter Usage

The table below shows which search parameters each IHV backend actually consumes during inference.

| Parameter | OrtGenAI | OrtGenAI-RyzenAI | WindowsML | llama-cpp (CUDA/Vulkan/Metal/ROCm) | NativeOpenVINO | NativeQNN | MLX | Diffusers |
|---|---|---|---|---|---|---|---|---|
| `method` | — | — | — | **yes** | — | — (forced greedy) | greedy only | — |
| `temperature` | **yes** | **yes** | **yes** | — | — | **yes** | — | — |
| `top_k` | **yes** | **yes** | **yes** | **yes** | — | **yes** | — | — |
| `top_p` | **yes** | **yes** | **yes** | **yes** | — | **yes** | — | — |
| `stop_on_eos` | **yes** | **yes** | **yes** | — | — | — | — | — |
| `max_length` | **yes** | **yes** | **yes** | **yes** | **yes** | **yes** | **yes** | reserve only |
| `max_total_length` | **yes** | **yes** | **yes** | **yes** | **yes** | — | **yes** | — |
| `context_length` | — | — | — | **yes** | — | **yes** (capped 4096) | — | — |

### Notes

- **`stop_on_eos`**: Only OrtGenAI-family backends (OrtGenAI, OrtGenAI-RyzenAI, WindowsML) gate early exit on this flag. llamacpp always stops on EOG tokens. NativeOpenVINO, NativeQNN, and MLX use their own internal EOS handling.
- **`max_length` vs `max_total_length`**: In OrtGenAI-family backends, `max_length` controls the decode-step loop count while `max_total_length` is passed to the OGA stack as the total sequence budget. In llamacpp, `max_total_length` also determines the batch size (`max_total_length - max_length`).
- **`temperature`**: Dead in llamacpp (parsed but never added to the sampler chain), NativeOpenVINO, and MLX.
- **`method`**: Only llamacpp switches between greedy/top_k/top_p samplers. OrtGenAI always sets all three params. NativeQNN forces greedy.
- **`context_length`**: Only used by llamacpp (for `n_ctx`) and NativeQNN (capped at 4096). Other backends ignore it.

## C++ Class

The configuration is parsed by `cil::infer::LlmConfig` (defined in `src/CIL/common/llm/llm_config.h`). The runtime computes `max_total_length` from actual tokenized input before calling `Init()` on any IHV backend. Use `LlmConfig::GetContextLength(json)` to read `context_length` from either the flat or legacy nested form.
