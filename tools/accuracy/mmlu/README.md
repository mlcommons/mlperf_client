
# MMLU Benchmark Script

This folder lives under **`tools/accuracy/mmlu`**. Shared MLPerf helpers used by this script are in **`tools/accuracy/mlperf_common.py`**. Run commands from the repository root or pass explicit paths in your JSON config.

## Overview

The `run_mmlu_benchmark.py` script is designed to run the MLPerf program on the Massive Multitask Language Understanding (MMLU) test data, automating the process of testing and reporting accuracy across various subjects. It performs the following steps:

1. **Download MMLU Data** (if needed)
2. **Generate Input Files**: Creates JSON files with formatted prompts for MLPerf.
3. **Generate Config Files**: Builds configuration files for MLPerf.
4. **Run MLPerf Program**: Executes MLPerf on the generated configurations.
5. **Calculate Accuracy**: Processes results to calculate accuracy.
6. **Generate Report**: Produces a report based on accuracy and inference data.

## Requirements

- **MLPerf Program**: Specify the path to the MLPerf executable in the configuration file.
- **Python Packages**: Install required packages:

    ```bash
    pip install -r requirements-mmlu.txt
    ```

This script has been tested with **Python 3.12**. It is recommended to use Python 3.12 or higher to ensure compatibility.

## Command-Line Arguments

| Argument | Description |
|----------|-------------|
| `-c, --config` | Path to the JSON configuration file (required) |
| `-r, --run-config` | Path to a vendor default config (overrides `RunConfigPath` in the config file) |
| `-p, --program` | Path to `mlperf-windows.exe` (overrides `MLPerfProgramPath` in the config file) |
| `-t, --type` | Benchmark type: `mmlu` (default) or `tinymmlu` |
| `-v, --verbose` | Enable debug-level logging |
| `-s, --skip-failed-prompts` | Skip prompts that fail during execution |
| `--postprocess-only` | Only run postprocessing on existing results (requires `--run-id`) |
| `--run-id` | Specify run ID for postprocessing existing results |

## Configuration File

The script takes a JSON configuration file as input. Below is an example configuration:

```json
{
    "MMLUDataPath": "mmlu_data",  
    "MLPerfProgramPath": "path/to/mlperf-windows.exe",
    "OutputDir": "output",  
    "DataSplitStep": null,  
    "Subjects": ["abstract algebra", "astronomy"],
    "HfToken": "hf_*****",
    "RunID": null,  
    "FewShotPromptsNumber": 5,  
    "InputConfigPath": null,
    "RunConfigPath": null,
    "InputConfigTemplate": {  
        "model_config": {
            "model": {
                "bos_token_id": 1,
                "context_length": 4096,
                "eos_token_id": 2,
                "vocab_size": 32000
            }
        }
    },
    "RunConfigTemplate": {
        "SystemConfig": {
            "Comment": "Default config",
            "TempPath": "",
            "EPDependenciesConfigPath": ""
        },
        "Scenarios": [
            {
                "Name": "Llama2",
                "Models": [
                    {
                        "ModelName": "Llama2 int4-cpu",
                        "FilePath": "https://client.mlcommons-storage.org/deps/1.0/scenario_files/llm/llama2/models/llama-2-7b-chat/llama2-cpu-int4/model.onnx",
                        "DataFilePath": "https://client.mlcommons-storage.org/deps/1.0/scenario_files/llm/llama2/models/llama-2-7b-chat/llama2-cpu-int4/model.onnx.data",
                        "TokenizerPath": "https://client.mlcommons-storage.org/deps/1.0/scenario_files/llm/llama2/models/llama-2-7b-chat/tokenizer.zip"
                    }
                ],
                "AssetsPath": [],
                "DataVerificationFile": "",
                "ExecutionProviders": [
                    {
                        "Name": "llama-cpp",
                        "Config": {
                            "backend": "CUDA",
                            "device_type": "GPU",
                            "device_id": 0
                        }
                    }
                ]
            }
        ]
    }
}
```

### Configuration Parameters

- **MMLUDataPath**: Path to the MMLU data. If empty, data is downloaded automatically.
- **MLPerfProgramPath**: Path to the MLPerf executable. Can be overridden with `-p` on the command line.
- **OutputDir**: Directory for all generated files and results.
- **DataSplitStep**: Controls how prompts are split and stored in configuration files (set to `null` by default). When set to an integer, the program splits prompts into separate input configuration files, each containing the specified number of prompts, and runs them individually. When set to `null`, prompts are grouped by subject into a single configuration file, and all prompts are run at once. Note: for the TinyMMLU benchmark a `null` value is treated as `1`, so each prompt is written to its own configuration file and run individually.
- **Subjects**: List of subjects to calculate accuracy for. If set to `null`, all subjects are used.
- **HfToken**: Hugging Face token for gated datasets. Falls back to the `HF_TOKEN` or `HUGGING_FACE_HUB_TOKEN` environment variable if not set. Optional for public datasets.
- **RunID**: Use to resume a previous run (`null` starts a new run).
- **FewShotPromptsNumber**: Number of few-shot prompts to include.
- **VerbalAnswerFormat**: When `true`, the model is prompted to answer in a sentence ("The correct answer is X") instead of a single letter. Default `false`.
- **VerbalAnswerMaxLength**: Maximum generation length when `VerbalAnswerFormat` is enabled. Default `96`.
- **VerbalAnswerLenientScoring**: When `true` (and `VerbalAnswerFormat` is enabled), the scorer accepts partial matches and fallback parsing. Default `false`.
- **PromptFormat**: Controls who applies the chat template. `raw` (default) passes the prompt text through unchanged (no templating). `auto` pre-applies the tokenizer's chat template in the harness (Python). `app` does not template in Python; it emits object-form prompts plus `apply_chat_template: true` (and `enable_thinking`) so the MLPerf client (`mlperf-windows.exe`) applies the chat template at inference time.
- **EnableThinking**: Optional boolean for reasoning models whose chat template supports a thinking toggle (e.g. Qwen 3). With `PromptFormat: "auto"` it is passed to the tokenizer's `apply_chat_template` in the harness; with `PromptFormat: "app"` it is written into `input_file.json` and applied by the MLPerf client. Set `false` (the default) to suppress `<think>...</think>` reasoning so the scored answer is the direct response; set `true` to keep it. Ignored for models whose template has no thinking toggle (e.g. Phi-4-mini) and in `raw` mode — so with the default `raw` prompt format it only takes effect once you switch `PromptFormat` to `auto` or `app`.
- **InputConfigPath** and **RunConfigPath**: These specify paths to pre-defined configuration files. When `InputConfigPath` is `null`, the script will use `InputConfigTemplate` to define model architecture details; otherwise, it will use the specified path in `InputConfigPath`. Similarly, if `RunConfigPath` is `null`, the script defaults to `RunConfigTemplate` to specify model and tokenizer paths, required files, and execution providers for running the model; otherwise, it uses the provided path in `RunConfigPath`. `RunConfigPath` can also be overridden with `-r` on the command line.

### Customizing the Templates

Adjust the `RunConfigTemplate` and `InputConfigTemplate` sections for model configurations and settings that match your MLPerf requirements.

## Running the Script

### Environment Setup (conda)

Create and configure a conda environment (tested with Python 3.12):

```bash
conda create -n mmlu python=3.12 -y
conda run -n mmlu pip install -r tools/accuracy/mmlu/requirements-mmlu.txt
```

> **Note:** Use `conda run -n mmlu` to invoke the scripts. This ensures the correct
> Python interpreter and packages are used both by the parent process and any
> subprocesses spawned via `sys.executable` (e.g. by `mmlu_runner.py`).

### Environment Setup (venv, no conda)

If you don't have conda, use a standard Python venv instead:

```bash
# Create and activate a venv (requires Python 3.12+ on PATH)
python -m venv vmmlu
# Windows
vmmlu\Scripts\activate
# Linux / macOS
# source vmmlu/bin/activate

pip install -r tools/accuracy/mmlu/requirements-mmlu.txt
```

Once activated, run scripts directly — no `conda run` wrapper needed:

```bash
python tools/accuracy/mmlu/run_mmlu_benchmark.py --config <path_to_config.json>

# Or via mmlu_runner.py (from tools/accuracy/mmlu/)
python mmlu_runner.py -c mmlu-config.json -t tinymmlu -p <path_to_mlperf-windows.exe> -s -v
```

> **Tip (Windows):** If the venv activation doesn't propagate to subprocesses
> (e.g. in automated scripts or CI), invoke the venv Python directly instead:
>
> ```powershell
> .\vmmlu\Scripts\python.exe mmlu_runner.py -c mmlu-config.json -t tinymmlu -p <path_to_mlperf-windows.exe> -s -v
> ```
>
> This ensures `sys.executable` points to the venv Python in all child processes.

### Using `run_mmlu_benchmark.py` Directly

From the bundle root:

```bash
conda run -n mmlu python tools/accuracy/mmlu/run_mmlu_benchmark.py --config <path_to_config.json>
```

Paths like `MLPerfProgramPath` and `RunConfigPath` in the sample configs are resolved by the runner. Use the checked-in configs as examples for paths relative to the config file or pass absolute paths when needed.

### Using `mmlu_runner.py` (Recommended)

`mmlu_runner.py` wraps `run_mmlu_benchmark.py` with automatic retry support (up to 3 attempts). Run from the `tools/accuracy/mmlu` directory:

```bash
# TinyMMLU with verbose logging, skip-failed-prompts, and explicit mlperf path
conda run -n mmlu python mmlu_runner.py \
    -c mmlu-config.json \
    -t tinymmlu \
    -p <path_to_mlperf-windows.exe> \
    -s -v

# Full MMLU (all 57 subjects; mmlu-config.json has "Subjects": null)
conda run -n mmlu python mmlu_runner.py \
    -c mmlu-config.json \
    -t mmlu \
    -p <path_to_mlperf-windows.exe> \
    -s -v
```

> **Monitoring progress with `conda run`:** `conda run` buffers all stdout/stderr
> until the process exits, so nothing is printed while the benchmark runs. To
> monitor progress, watch the output directory in a separate terminal:
>
> ```powershell
> # List completed subjects (each gets its own results.json)
> Get-ChildItem output\tinymmlu\*\*\results.json | Measure-Object
>
> # Or on Linux / macOS
> # find output/tinymmlu -name results.json | wc -l
> ```
>
> You can also tail the log file written by the script:
>
> ```powershell
> Get-Content mmlu.log -Tail 20 -Wait
> ```
>
> The final `report.json` in the `output/` directory is created only after all
> subjects complete.

### TinyMMLU

`HfToken` is optional: if unset, the Hugging Face dataset is loaded with anonymous access when the repo is public. You can still set `HfToken` or export `HF_TOKEN` / `HUGGING_FACE_HUB_TOKEN` for gated models.

```bash
conda run -n mmlu python tools/accuracy/mmlu/run_mmlu_benchmark.py --config <path_to_config.json> -t tinymmlu
```

### Skip Failed Prompts

You can skip the failed prompts during benchmarking. In the generated `results.json` files you will be able to see `Skipped Prompts` indexes. To enable this feature, run the program with `-s` or `--skip-failed-prompts` option:

```bash
python tools/accuracy/mmlu/run_mmlu_benchmark.py --config <path_to_config.json> -s
```

## Output

- **Generated Files**: Input prompts and config files saved in `OutputDir`.
- **Inference Results**: MLPerf results stored in `OutputDir`.
- **Accuracy Report**: Final report based on calculated accuracy saved as `report.json`.

## Distributed Execution

The script supports running subsets of subjects on different machines and combining results for final scoring.

### Directory Structure

```
<OutputDir>/<benchmark_type>/<run_id>/
+-- <subject_name>/
|   +-- input_file.json        # Generated prompts
|   +-- answers.json           # Ground truth: {"answers": ["A", "B", ...], "subject": "..."}
|   +-- mlperf_config.json     # MLPerf configuration
|   +-- results.json           # MLPerf output with "Output" array
+-- ...
```

### Workflow

**1. Generate input files on a coordinator machine:**

Create configs with disjoint subject sets for each machine:

```json
// machine1-config.json
{
    "Subjects": ["abstract algebra", "anatomy", "astronomy"],
    "RunID": 1,
    ...
}

// machine2-config.json  
{
    "Subjects": ["business ethics", "clinical knowledge", "college biology"],
    "RunID": 1,
    ...
}
```

**2. Run on each machine:**

```bash
# Machine 1
python run_mmlu_benchmark.py --config machine1-config.json

# Machine 2
python run_mmlu_benchmark.py --config machine2-config.json
```

**3. Collect results:**

Copy all subject directories from each machine into a single `<OutputDir>/<benchmark_type>/<run_id>/` folder.

**4. Generate combined report:**

```bash
python run_mmlu_benchmark.py --config config.json --postprocess-only --run-id 1
```

The script scans all `results.json` files under the run directory and computes the aggregate MMLU score.

## Retry Runner (`mmlu_runner.py`)

`mmlu_runner.py` is a wrapper that runs one or more MMLU configs with automatic retry on failure (up to 3 attempts). If a run fails, it creates a `_cont.json` continuation config with the `RunID` set to resume the partial run.

```bash
python tools/accuracy/mmlu/mmlu_runner.py -c <config1.json> [config2.json ...] [-r <run_config>] [-p <program>]
```

| Argument | Description |
|----------|-------------|
| `-c, --config` | One or more config files to run (if omitted, scans `execution_configs/`) |
| `-r, --run-config` | Path to a vendor default config (forwarded to each run) |
| `-p, --program` | Path to `mlperf-windows.exe` (forwarded to each run) |
| `-t, --type` | Benchmark type: `mmlu` (default) or `tinymmlu` |
| `-v, --verbose` | Verbose mode |
| `-s, --skip-failed-prompts` | Skip failed prompts |

The runner archives each successful `report.json` with a timestamp and saves per-attempt log deltas under `output/<type>/`.
