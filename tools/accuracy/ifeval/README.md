# IFEval

This folder contains the final IFEval MLPerf harness.

It is a thin runner around the MLPerf client:

- it reads the IFEval dataset
- it builds MLPerf `input_file.json` files
- it applies model-family-specific chat framing when needed
- it launches `mlperf-windows.exe`
- it scores the generated responses with the bundled `instruction_following_eval`

The archived investigation material, exploratory scripts, and older run artifacts are not part of this checked-in harness.

## What This Harness Does

IFEval is a benchmark for instruction following. Each prompt contains one or more constraints, such as:

- no commas
- exact number of bullets
- lowercase only
- exact placeholder count
- repeat the request first, then answer

The harness does not change the benchmark itself. It prepares the prompt text so the model sees the correct chat wrapper for the model family, then it runs MLPerf and scores the result.

## How It Works

`run_ifeval_benchmark.py` performs the full flow:

1. Load the benchmark config JSON.
2. Load `instruction_following_eval/data/input_data.jsonl`.
3. Group prompts according to `IFEvalGroupBy`.
4. Write one MLPerf `input_file.json` per job.
5. Apply model-specific chat framing in `ifeval_mlperf_input.py` when the run config says the model family needs it.
6. Generate MLPerf config files and run `mlperf-windows.exe`.
7. Collect `results.json` files.
8. Score every response with `instruction_following_eval.evaluation_lib`.
9. Write an `ifeval_report.json` plus strict and loose eval details.

The bundled scorer is vendored from `mlcommons/mobile_open`'s Python IFEval implementation, with this tree keeping its own 541-prompt `input_data.jsonl`.

The key point is that the prompt wrapper is applied before MLPerf sees the prompt text. MLPerf is still just consuming `input_file.json`.

Paths inside the JSON config are resolved relative to the config file location. Paths passed on the command line are resolved relative to the current working directory unless they are already absolute.

The runner supports two practical execution styles:

- `per_prompt`: one MLPerf invocation per prompt. This reloads the model every time and is best for debugging.
- `fixed`: one MLPerf invocation per batch of `PromptsPerMLPerfJob` prompts. This keeps the model loaded for the whole batch and is the normal mode for faster runs.

The other two groupings, `instruction_signature` and `category_set`, are semantic batching modes. They also reduce reloads compared with `per_prompt`, but they group prompts by their instruction structure instead of by a simple fixed batch size.

## Batch Recovery Workflow

The normal production mode is `fixed` with a batch size such as 5 or 10. This is faster because the backend keeps the model loaded across several prompts. The tradeoff is that a hard backend failure can wipe out the entire batch, not just one prompt.

That means the recommended recovery flow is:

1. Run the full benchmark in `fixed` mode at the batch size you want.
2. Inspect `ifeval_report.json` for `NumMissingResults` and `NumEmptyResponses`.
3. For any missing or empty batch, rerun just that prompt range in `per_prompt` mode or in a smaller fixed batch.
4. Use the runner's `--result-only` mode to merge the original batch run and any recovery runs.
5. Re-score the combined prompt->response set to produce one final consolidated report.

The harness already keeps enough metadata to make this practical:

- `ifeval_examples.json` preserves the exact prompt set and keys used in a run.
- `run_manifest.json` records the grouping mode and batch size.
- `model_responses.jsonl` stores the final key/prompt/response mapping used for scoring.
- `eval_results_strict.jsonl` and `eval_results_loose.jsonl` store per-prompt scoring details.

Practical rule:

- Use `fixed` for the main run.
- Use `per_prompt` only for spot-checking or for recovering failed batches.
- Use `--result-only` to combine several partial runs and re-score them as one full benchmark.
- Combine results by `key`, not by file order.

If you rerun a failed batch in `per_prompt`, keep the same config family, model version, and backend settings so the recovered responses are comparable to the original batch. If the backend or model changes, treat it as a separate experiment.

Example:

```powershell
python .\tools\accuracy\ifeval\run_ifeval_benchmark.py `
  --instruction-following-eval-dir .\tools\accuracy\ifeval\instruction_following_eval `
  --canonical-data-path .\tools\accuracy\ifeval\instruction_following_eval\data\input_data.jsonl `
  -c .\my-ifeval-config.json `
  --result-only `
  .\tools\accuracy\ifeval\output\batch_run\ifeval\1 `
  .\tools\accuracy\ifeval\output\recovery_run\ifeval\1
```

The `--result-only` mode scores against the full canonical IFEval dataset bundled with the harness, not just the prompts present in the source runs. Any prompt that is still missing after merging remains an empty response in the final report.
When you pass several source runs, list the older or broader batch first and the newer recovery run later so the later responses can overwrite earlier empty or conflicting entries.
The report now includes both views:

- `StrictAccuracyAttempted` / `LooseAccuracyAttempted`: score over the prompts that were actually covered by the source run(s)
- `StrictAccuracy` / `LooseAccuracy`: score over the full canonical IFEval dataset
- `ResponseCoverageAttempted` / `ResponseCoverageTotal`: how many prompts produced any response, again over the source run(s) and over the full set

## Chat Framing Rules

The current harness knows about these model families:

- `Llama3`
- `Phi3.5`

The prompt builder applies different wrappers based on `Scenarios[0].Name` in the MLPerf run config.

Current behavior:

- `Llama3` prompts are wrapped in the Llama 3.1 instruct template.
- `Phi3.5` prompts are wrapped in the Phi 3.5 chat template from the official model card.
- If a model family is not recognized, prompts are left as raw text.

Current EOS handling:

- Llama 3.1 uses `eos_token_id = 128009`.
- Phi 3.5 uses `eos_token_id = 32000`.

If you need a different model family, add a branch in `ifeval_mlperf_input.py` and point it at the template used by that model.

## Repository Layout

- `run_ifeval_benchmark.py`: main runner
- `ifeval_mlperf_input.py`: prompt wrapper and MLPerf input builder
- `instruction_following_eval/`: bundled scoring code and dataset

## Requirements

- Python 3.10 or newer (3.12 recommended)
- `mlperf-windows.exe` from the MLPerf client package
- The backend runtime and model files required by the MLPerf run config you choose
- Python packages from `requirements-ifeval.txt`

### Environment Setup with Conda (Recommended)

Create and activate a dedicated conda environment:

```powershell
conda create -n ifeval python=3.12 -y
conda activate ifeval
conda install pip -y
```

Install the Python dependencies (from the repository root):

```powershell
pip install -r .\tools\accuracy\ifeval\requirements-ifeval.txt
```

Activate the environment before every session:

```powershell
conda activate ifeval
```

To remove the environment when no longer needed:

```powershell
conda deactivate
conda env remove -n ifeval
```

### Alternative: Install Without Conda

If you prefer not to use conda, install the dependencies directly:

```powershell
python -m pip install -r .\tools\accuracy\ifeval\requirements-ifeval.txt
```

If your environment already has the needed packages, you can skip this step.

The bundled scorer uses the dependencies listed in `requirements-ifeval.txt`.

## Running The Benchmark

**Important:** Always activate the conda environment before running any benchmark command:

```powershell
conda activate ifeval
```

Create a benchmark config JSON (see the example and field reference below), then run from the repository root:

```powershell
$ifevalArgs = @(
  "--instruction-following-eval-dir", ".\tools\accuracy\ifeval\instruction_following_eval",
  "--canonical-data-path", ".\tools\accuracy\ifeval\instruction_following_eval\data\input_data.jsonl"
)

python .\tools\accuracy\ifeval\run_ifeval_benchmark.py @ifevalArgs -c .\my-ifeval-config.json
```

The runner writes all outputs under `OutputDir`, then under `ifeval/<RunID>/...`.
`OutputDir` may be relative; if so, it is interpreted relative to the config file location.

## Targeting A Backend

To test a backend:

1. Create a config file (start from the example below).
2. Set `RunConfigPath` to the vendor default run template for your backend (see `data/configs/vendors_default/`).
3. Update `InputConfigTemplate.model` to match the model's token ids and context limits.
4. Keep `IFEvalDataPath` pointing at `instruction_following_eval/data/input_data.jsonl`.
5. Keep `MLPerfProgramPath` pointing at your MLPerf client executable.

The MLPerf run template must still describe a valid scenario, model, tokenizer, and execution provider for that backend.

Examples of things that may change by backend:

- model path format
- tokenizer path format
- data file handling
- execution provider name
- temporary directory handling
- EOS token id
- context length and vocabulary size

The IFEval runner itself only needs a valid MLPerf config and a model family name it knows how to wrap.

## Config Fields

Example config (paths inside the JSON are resolved relative to the config file location):

```json
{
    "IFEvalDataPath": "tools/accuracy/ifeval/instruction_following_eval/data/input_data.jsonl",
    "MLPerfProgramPath": "mlperf-windows.exe",
    "OutputDir": "tools/accuracy/ifeval/output",
    "RunID": null,
    "IFEvalGroupBy": "fixed",
    "PromptsPerMLPerfJob": 5,
    "PromptFormat": "auto",
    "RunConfigPath": "data/configs/vendors_default/llm/Llama3.1/NVIDIA_llamacpp-CUDA_GPU.json",
    "InputConfigTemplate": {
        "model_config": {
            "model": {
                "context_length": 4096
            },
            "search": {
                "method": "greedy",
                "num_beams": 1,
                "temperature": 0.6,
                "top_k": 1,
                "top_p": 0.9,
                "stop_on_eos": true,
                "max_length": 3072
            }
        }
    }
}
```

The main JSON config uses these fields:

- `IFEvalDataPath`: path to the IFEval JSONL prompt file
- `MLPerfProgramPath`: path to `mlperf-windows.exe`
- `OutputDir`: root directory for run outputs
- `RunID`: numeric run identifier used for resume behavior
- `IFEvalGroupBy`: one of `per_prompt`, `fixed`, `instruction_signature`, `category_set`
- `PromptsPerMLPerfJob`: batch size for `fixed` runs
- `RunConfigPath`: MLPerf run template for the target backend
- `InputConfigTemplate`: base `model_config` used for each generated `input_file.json`
- `PromptFormat`: controls who applies the chat template. `auto` (default) pre-applies the tokenizer's chat template in the harness (Python). `raw` passes the prompt text through unchanged (no templating anywhere) — useful to compare an official MLPerf prompt inside and outside this harness with the exact same visible text. `app` does not template in Python; it emits object-form prompts plus `apply_chat_template: true` (and `enable_thinking`) so the MLPerf client (`mlperf-windows.exe`) applies the chat template at inference time.
- `EnableThinking`: optional boolean for reasoning models whose chat template supports a thinking toggle (e.g. Qwen 3). With `PromptFormat: "auto"` it is passed to the tokenizer's `apply_chat_template` in the harness; with `PromptFormat: "app"` it is written into `input_file.json` and applied by the MLPerf client. Set `false` (the default) to suppress `<think>...</think>` reasoning so the scored response is the direct answer; set `true` to keep it. Ignored for models whose template has no thinking toggle (e.g. Phi-4-mini) and in `raw` mode.

`InputConfigTemplate.model.search` is set for greedy decoding by default because IFEval expects short, deterministic completions.

Required runner command-line paths:

- `--instruction-following-eval-dir`: package directory for the bundled `instruction_following_eval`
- `--canonical-data-path`: full canonical IFEval dataset used for total-score denominators

Optional runner command-line arguments:

- `-r, --run-config`: path to a vendor default config (overrides `RunConfigPath` in the JSON config)
- `-p, --program`: path to `mlperf-windows.exe` (overrides `MLPerfProgramPath` in the JSON config)
- `--output-dir`: override `OutputDir` from the JSON config for this invocation
- `--winml-llama31-local-bundle`: local WinML Llama 3.1 TRT bundle containing `model.onnx`, `model.onnx.data`, and `tokenizer.zip`
- `--postprocess-only`: only run scoring on existing results (requires `--run-id`)
- `--run-id`: specify run ID for `--postprocess-only`
- `-v, --verbose`: enable debug-level logging

## Grouping Modes

- `per_prompt`: one MLPerf job per prompt
- `fixed`: sequential batches of `PromptsPerMLPerfJob` with one model load per batch
- `instruction_signature`: group prompts that share the same exact instruction set
- `category_set`: group prompts by the set of instruction categories

`per_prompt` is the easiest way to debug a single example. `fixed` is the best choice when you want to keep throughput reasonable. `instruction_signature` is the best resumable mode when you want less fragmentation.

## Outputs

Each run writes:

- `ifeval_report.json`: summary with strict and loose accuracy
- `model_responses.jsonl`: prompt/response pairs
- `eval_results_strict.jsonl`: strict scoring details
- `eval_results_loose.jsonl`: loose scoring details
- `run_manifest.json`: run metadata
- `ifeval_examples.json`: serialized copy of the prompt set that was used

When `--result-only` is used, the runner writes a new consolidated set of these artifacts for the merged run.

The report also records:

- `NumPrompts`: total prompts in the IFEval dataset
- `NumAttemptedPrompts`: prompts covered by the run or merged source runs
- `NumCoveredPrompts`: same as `NumAttemptedPrompts`
- `NumResponses`: prompts that produced a non-empty response
- `NumEmptyResponses`: prompts that produced no response

## Merging Runs

If you run a benchmark in batches and then recover a failed batch with smaller reruns, the final result is the union of responses keyed by prompt key.

The simplest merge rule is:

1. Start with the original `model_responses.jsonl` from the batch run.
2. Replace any missing keys with responses from the rerun.
3. Recompute the strict and loose scores from the merged prompt->response mapping.

The prompt key is the stable identifier for this benchmark. Do not rely on prompt order, because the batch run and the rerun may have different grouping and different file layouts.

## Notes For New Model Families

If you want to add a new chat template:

1. Add a model-family check in `ifeval_mlperf_input.py`.
2. Add a wrapper that builds the exact prompt format required by that model.
3. Set the correct `eos_token_id`.
4. Create a config file that points at the backend run template.
5. Validate with a small `per_prompt` run first.

The harness is designed to work with any backend as long as its MLPerf run template is valid and its model family is supported in `ifeval_mlperf_input.py`.
