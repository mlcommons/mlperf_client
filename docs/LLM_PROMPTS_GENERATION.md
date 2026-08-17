# LLM Prompt Generation Guide

This document explains how to create and manage prompt templates for MLPerf Client, including using the provided scripts for prompt generation.

## Overview

Prompt templates define the structure and content of prompts used for benchmarking language models. The MLPerf Client provides scripts to help you generate new prompts.

Prompt data files live under `data/prompts/<model_base_name>/<category>/` (e.g. `data/prompts/llama_3_1_8b_instruct/code_analysis/`). Each model directory contains a `template.json` used by the generation script.

## Script: `generate_prompt.py`

This script generates a prompt JSON file from a template and text files for the system and user prompts.

**Usage:**

```bash
python tools/generate_prompt.py \
  --template <template.json> \
  --system <system.txt> \
  --user <user.txt> \
  --category <category> \
  --output <output.json> \
  --context-length <int> \
  --max-length <int>
```

**Parameters:**
- `--template`: Path to the model template JSON file (e.g. `data/prompts/qwen_3_8b/template.json`).
- `--system`: Path to the system prompt text file.
- `--user`: Path to the user prompt text file.
- `--category`: Category for the prompt (e.g. `code_analysis`, `content_generation`, `summarization`).
- `--output`: Output path for the generated prompt JSON.
- `--context-length`: Context length for the model.
- `--max-length`: Maximum generated text length.

The generated file stores the prompt as an object entry `{ "system": ..., "user": ... }` and sets `apply_chat_template: true`. The model's chat template is applied at runtime by the C++ executable; the script no longer needs to know which model family it is targeting.

**Example:**

```bash
python tools/generate_prompt.py \
  --template data/prompts/qwen_3_8b/template.json \
  --system prompts/system.txt \
  --user prompts/user.txt \
  --category code_analysis \
  --output data/prompts/qwen_3_8b/code_analysis/my_prompt.json \
  --context-length 8192 \
  --max-length 512
```

## Tips
- Organize your prompt files by category and model for clarity.
- Use descriptive filenames for system/user prompt text files.
- Validate generated prompt JSON files before use.

---

For more details, see the script docstrings or use the `--help` flag with each script.
