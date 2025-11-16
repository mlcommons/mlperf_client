# Prompt Generation Guide

This document explains how to create and manage prompt templates for MLPerf Client, including using the provided scripts for prompt generation.

## Overview

Prompt templates define the structure and content of prompts used for benchmarking language models. The MLPerf Client provides scripts to help you generate new prompts.

## Script `generate_prompt.py`

This script generates a prompt JSON file from a template and text files for the system and user prompts.

**Usage:**

```bash
python tools/generate_prompt.py \
  --template <template.json> \
  --model <llama2|llama3|phi3.5|phi4> \
  --system <system.txt> \
  --user <user.txt> \
  --category <category> \
  --output <output.json> \
  --context-length <int> \
  --max-length <int> \
  --max-total-length <int>
```

**Parameters:**
- `--template`: Path to the model template JSON file.
- `--model`: Model type (e.g., llama2, llama3, phi3.5, phi4).
- `--system`: Path to the system prompt text file.
- `--user`: Path to the user prompt text file.
- `--category`: Category for the prompt (e.g., math, reasoning).
- `--output`: Output path for the generated prompt JSON.
- `--context-length`: Context length for the model.
- `--max-length`: Maximum generated text length.
- `--max-total-length`: Maximum total length.

**Example:**

```bash
python tools/generate_prompt.py \
  --template templates/llama2_template.json \
  --model llama2 \
  --system prompts/system.txt \
  --user prompts/user.txt \
  --category math \
  --output prompts/math_llama2.json \
  --context-length 4096 \
  --max-length 512 \
  --max-total-length 4096
```

