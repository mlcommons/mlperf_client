# Accuracy Harnesses

This folder contains benchmark-specific accuracy harnesses that run through the MLPerf client.

The current layout is:

- `mlperf_common.py`: shared MLPerf plumbing used by multiple benchmarks.
- `ifeval/`: IFEval-specific prompt preparation, batching, execution, and scoring.
- `mmlu/`: MMLU/TinyMMLU-specific data preparation, execution, and scoring.

The benchmark runners are intentionally separate. They share the mechanics of generating and running MLPerf jobs, but each benchmark owns its dataset format, prompt construction, grouping strategy, and scoring rules.

## Shared MLPerf Layer

`mlperf_common.py` provides helpers that are common across benchmark harnesses:

- resolve `file://`, HTTP, and HTTPS paths used by MLPerf configs
- copy or download model, tokenizer, and data artifacts when needed
- allocate and resume run IDs
- generate per-job MLPerf config files from benchmark-produced `input_file.json` files
- launch `mlperf-windows.exe`
- find and read `results.json`
- optionally write stub failure results so long benchmark sweeps can continue after a failed MLPerf job

`ifeval/run_ifeval_benchmark.py` and `mmlu/run_mmlu_benchmark.py` import this shared module from this `tools/accuracy` root.

See `ifeval/README.md` and `mmlu/README.md` for benchmark-specific usage details.

## WindowsML Path Length Note

WindowsML runs can fail silently with empty `results.json` files when generated MLPerf work paths are too deep. When running WindowsML backends, use a short work root (for example `.\wmlp`) for the MLPerf output and temporary directories, and keep the path short.
