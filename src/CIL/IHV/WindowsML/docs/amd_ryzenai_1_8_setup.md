# AMD RyzenAI 1.8 (OGA) Setup — MLPerf Client v2

Wiring to run MLPerf Client on the AMD **OrtGenAI-RyzenAI** execution provider
(RyzenAI 1.8) for:

- Llama 3.1 8B Instruct
- Phi 4 Mini Instruct
- Qwen 3 8B

Models and RyzenAI runtime DLLs are hosted remotely: the model artifacts are on
Cloudflare (the vendor configs point at `https://client.mlcommons-storage.org/deps/...`)
and the RyzenAI EP DLLs are listed in the main EP-dependency manifest
(`data/ep_dependencies_config_windows_x64.json.in`, the `OrtGenAI-RyzenAI` entry).
The benchmark downloads both on demand — nothing model-specific is checked in.

## Prerequisites (RyzenAI 1.8 Beta)

Install, in order (see `src/CIL/IHV/OrtGenAI-RyzenAI/src/README.md` for the
canonical links and model-prep recipes):

- Latest **AMD NPU driver** — <https://download.amd.com/opendownload/RyzenAI/1.8.0b0/NPU_RAI_376_WHQL.zip>
- Latest **AMD GPU driver** — <https://www.amd.com/en/support/download/drivers.html>
- **Ryzen AI 1.8-Beta** — <https://download.amd.com/opendownload/RyzenAI/1.8.0b0/ryzen-ai-lt-1.8.0-beta.exe>

Confirm the NPU stack after install:

```powershell
& "C:\Windows\System32\AMD\xrt-smi.exe" examine
```

For best performance set Windows power mode to *Best Performance* and the NPU to
turbo (AC power, admin prompt): `xrt-smi configure --pmode turbo`.

## Build

`mlperf-windows.exe` is built with the RyzenAI IHV (`MLPERF_IHV_ORT_GENAI_RYZENAI`,
ON by default). From a Visual Studio x64 developer prompt at the repo root:

```powershell
cmake -G "Visual Studio 17 2022" -A x64 -S . -B build -DMLPERF_IHV_ORT_GENAI_RYZENAI=ON
cmake --build build --config Release --target CLI
```

If `clang-tidy` is not installed locally, pass `-DCLANG_TIDY_EXE=<no-op-wrapper>`.

After swapping RyzenAI runtime DLLs, clear the dependency cache so a stale provider
DLL is not reused:

```powershell
Remove-Item -Recurse -Force "$env:TEMP/MLPerf" -ErrorAction SilentlyContinue
```

## Run

Run from the binary directory so runtime-relative logging initializes. Use
`--pause false` for non-interactive runs.

```powershell
cd Bin/Windows/x64/Release
./mlperf-windows.exe -c <repo>/data/configs/vendors_default/llm/extended/qwen3/AMD_OrtGenAI-RyzenAI_NPU-GPU.json -o output/qwen3 -t tmp/qwen3 -b normal --pause false
```

Run configs (`NPU-GPU` = hybrid NPU+iGPU, `NPU` = NPU-only):

```text
data/configs/vendors_default/llm/Llama3.1/AMD_OrtGenAI-RyzenAI_NPU-GPU.json
data/configs/vendors_default/llm/Llama3.1/AMD_OrtGenAI-RyzenAI_NPU.json
data/configs/vendors_default/llm/extended/qwen3/AMD_OrtGenAI-RyzenAI_NPU-GPU.json
data/configs/vendors_default/llm/extended/qwen3/AMD_OrtGenAI-RyzenAI_NPU.json
data/configs/vendors_default/llm/phi4mini/AMD_OrtGenAI-RyzenAI_NPU-GPU.json
data/configs/vendors_default/llm/phi4mini/AMD_OrtGenAI-RyzenAI_NPU.json
```

Agentic (SWE + Data Analyst) variants live under
`data/configs/vendors_default/agentic/<model>/AMD_OrtGenAI-RyzenAI_NPU-GPU.json`.

## Accuracy (IFEval / MMLU)

Install the accuracy dependencies once:

```powershell
python -m pip install -r tools/accuracy/ifeval/requirements-ifeval.txt
python -m pip install -r tools/accuracy/mmlu/requirements-mmlu.txt
```

Create a benchmark config per harness (see `tools/accuracy/ifeval/README.md` and
`tools/accuracy/mmlu/README.md` for the config format) with `RunConfigPath`
pointing at the RyzenAI vendor config for the target model, e.g.
`data/configs/vendors_default/llm/extended/qwen3/AMD_OrtGenAI-RyzenAI_NPU-GPU.json`.
Then run from the repo root:

```powershell
python tools/accuracy/ifeval/run_ifeval_benchmark.py -c <ifeval-config.json>
python tools/accuracy/mmlu/run_mmlu_benchmark.py   -c <mmlu-config.json> -t mmlu
```

## Notes

- Hybrid models are tuned via `provider_options` in each model's `genai_config.json`
  (e.g. `hybrid_opt_init_prompt_size` 4096 for Llama-3.1-8B / Qwen3-8B, 8192 for
  Phi-4-mini). See `src/CIL/IHV/OrtGenAI-RyzenAI/src/README.md`.
- `Failed to serialize proto` / allocation errors usually mean NPU/GPU out-of-memory;
  free memory or raise `MemoryManager\SystemPartitionCommitLimitPercentage` (Windows
  allocates ~50% of system RAM to NPU/GPU by default).
