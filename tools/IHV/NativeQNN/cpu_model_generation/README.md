# CPU_MODEL_GEN_NEW — Linux CPU bin generator (minimal)

Single-script Linux equivalent of `cpu_model_generation/`. One `run.sh` does everything end-to-end.

## Prerequisites

- Linux x86_64
- `python3.10` on PATH (override with `PYTHON_BIN=...`)
- `git`, `git-lfs`, `curl`, `unzip`

## Usage

```bash
./run.sh phi4-mini      # default
./run.sh phi4
./run.sh llama3
```

## What it does

1. Downloads the QAIRT SDK (`v2.46.0.260424`) into `./qairt/` if missing
2. Creates a Python 3.10 venv in `./venv/` and installs `requests tqdm numpy sentencepiece cmake`
3. Sources `qairt/<version>/bin/envsetup.sh`
4. Runs `qairt/<version>/bin/check-linux-dependency.sh`
5. Runs `qairt/<version>/bin/check-python-dependency`
6. `git lfs clone` of the HuggingFace model repo
7. Runs `qnn-genai-transformer-composer --quantize Z8` with the matching `*_config_file.json`

Output: `./<model>_cpu.bin` in this directory.

## Overrides

- `QAIRT_VERSION=2.46.0.260424`
- `QAIRT_URL=<custom zip URL>`
- `PYTHON_BIN=python3.10`
