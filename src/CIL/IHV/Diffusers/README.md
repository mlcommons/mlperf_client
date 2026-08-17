# Diffusers EP

Image-generation execution provider using Hugging Face Diffusers.

## Build

### Path 1: Full CMake build (embedded Python)

The Diffusers EP is built as part of the main CMake project. Two relevant options:

| Option | Default | Description |
|--------|---------|-------------|
| `MLPERF_IHV_DIFFUSERS_INSTALL_PYTHON` | `ON` | Download an embedded Python (python-build-standalone) and install pip packages (torch, diffusers, transformers, torch_tensorrt, etc.). Required for inference. |
| `MLPERF_IHV_DIFFUSERS_BUILD_STANDALONE` | `ON` | Register the `diffusers_standalone` build target (PyInstaller-frozen onedir bundle). Only available when `INSTALL_PYTHON` is `ON`. |

```bash
# Standard build (includes embedded Python + packages on first install)
cmake --build build --config Release
cmake --install build --config Release
```

First install takes a few minutes (downloads ~4 GB of Python packages).
Subsequent installs skip this step via a sentinel file. To force
reinstall, delete `<install-dir>/IHV/Diffusers/.packages_installed`.

The install chain pins `torch>=2.12,<2.13` (CUDA `cu130` wheels on
Windows). `torch_tensorrt` and the `tensorrt` packages are installed
with `--no-deps` to prevent pip from replacing the CUDA torch build.

### Path 2: Venv / system Python (no CMake needed for Python)

For developers who want to iterate on the Python scripts without a full
CMake build, create a venv and run the install helper:

```bash
python -m venv .venv
.venv\Scripts\activate        # Windows
# source .venv/bin/activate   # macOS/Linux

python tools/install_python_deps.py [--vendor {nvidia,apple}]
```

`--vendor` selects the requirements file and install strategy; it
defaults to the host platform (`darwin` → `apple`, otherwise `nvidia`).

This mirrors the exact same install order as the CMake path:

1. `torch + torchvision` (CUDA `cu130` index for `nvidia`, pinned to 2.12.x)
2. `bitsandbytes` (`nvidia` only, best-effort)
3. `torch_tensorrt + tensorrt + dllist` with `--no-deps` (`nvidia` only)
4. Vendor requirements file (`--upgrade-strategy only-if-needed`)
5. torch verification (CUDA for `nvidia`, MPS for `apple`)

Then run standalone_runner.py directly:

```bash
python src/scripts/standalone_runner.py \
    --config tools/standalone_configs/flux2klein_fp8_cuda.json \
    --output-dir ./out --download
```

## Models

Configs reference models by Hugging Face repo id (e.g.
`black-forest-labs/FLUX.2-klein-4B` in `Model.FilePath`). The standalone
runner downloads weights from the HF Hub on demand when passed
`-d`/`--download` (optional cache dir; defaults to the
`MLPERF_DIFFUSERS_CACHE` env var, else `.cache/models`). Without
`--download` it runs fully offline against an already-populated cache.

The client-app configs under `data/configs/dev/flux2klein/` instead
reference S3 URLs for the model weights and prompts; the harness
downloads and unpacks them automatically.

## Running inference

### Embedded Python (fastest iteration)

Edit a script, re-run immediately — no rebuild needed.

```bash
<install-dir>/IHV/Diffusers/python/python.exe -I \
    src/CIL/IHV/Diffusers/src/scripts/standalone_runner.py \
    --config <config.json> --output-dir ./out
```

### Standalone (frozen onedir bundle)

```bash
cmake --build build --target diffusers_standalone --config Release
<install-dir>/IHV/Diffusers/standalone/diffusers_standalone.exe \
    --config <config.json> --output-dir ./out
```

### Client app (full DLL path)

```bash
<install-dir>/mlperf-windows.exe -c <test_config.json>
```

## Config files

- **Client app configs**: `data/configs/dev/flux2klein/*.json` — used by the client app; reference models and prompts by S3 URL.
- **Standalone configs**: `tools/standalone_configs/*.json` — used by the standalone runner/EXE; reference models by HF repo id.

## Vendor notes

- **AMD RyzenAI (NPU)**: see [`docs/AMD_RyzenAI.md`](docs/AMD_RyzenAI.md) for the RyzenAI ONNX path, model source/hosting, and prerequisites.

## CMake targets

| Target | Description |
|--------|-------------|
| `ihv_diffusers` | Main build (C++ DLL/dylib + embedded Python + pip packages) |
| `diffusers_standalone` | PyInstaller onedir bundle at `bin/.../IHV/Diffusers/standalone/diffusers_standalone.exe` (on-demand, sentinel-gated — skips if inputs unchanged) |

## Adding a new IHV (vendor)

Today the EP supports two vendors (NVIDIA via CUDA, Apple via MPS); one
vendor per platform. The Python side is structured to make adding a
third (e.g., Intel XPU, AMD ROCm) a localised change. Touch-points:

### Python side

1. **Vendor class** — create `src/scripts/vendors/<name>.py` with a
   class derived from `Vendor` (see `vendors/base.py`). Implement
   `configure_globals` (per-backend perf flags), `resolve_device`,
   `make_generator`, `default_dtype`, `release_memory`, `system_info`.
   Wire it into `get_vendor()` in `vendors/__init__.py` (an explicit
   `if/elif` on `device_vendor` / `device_type`).
2. **Runtime class** — create `src/scripts/runtimes/<name>.py` with a
   class derived from `Runtime`. Set `backend = "<backend-id>"`
   (e.g., `"cuda"`, `"mps"`, `"xpu"`). Implement `generate()`,
   override `instrument()` if you want submodule timing.
   Decorate with `@register_runtime(backend="<backend-id>")`.
3. **Model loaders** — drop loaders under `src/scripts/models/` or
   reuse `models/generic.py` (pipeline-class-driven loader covers most
   diffusers models without per-model code).
4. **Backend-vendor mapping** — add a row to `_BACKEND_TO_VENDOR` in
   `src/scripts/diffusers_inference.py` so the bridge can resolve
   `backend → device_vendor`.
5. **Optimization handlers** — optional, drop modules under
   `src/scripts/opts/` if the vendor has vendor-specific optimizations
   (e.g., a vendor-specific compiler). Decorate with `@register_opt`.

### C++ side

6. **Schema** — add the new backend value to `ConfigSchema.json`'s
   enum for the EP `Config.backend` if needed. The harness selects the
   vendor's EP dependencies from the config's `device_vendor`; the
   Python side falls back to inferring it from `backend` via
   `_BACKEND_TO_VENDOR` when the config omits it.
7. **Configs** — add `data/configs/dev/flux2klein/<VENDOR>_Diffusers_*.json`
   for the harness path and `tools/standalone_configs/*.json` for the runner.

### Packaging

8. **PyInstaller spec snippet** — drop
   `tools/specs/<vendor>.py` exporting `contribute()` returning a dict
   with optional keys (`hidden_packages`, `metadata_packages`,
   `data_packages`, `binary_packages`, `binary_files`,
   `runtime_hooks`). The aggregator in `tools/specs/__init__.py`
   merges all vendor modules — missing packages are skipped silently,
   so platform-conditional content is automatic. Add the import to
   `tools/standalone.spec`'s `_contributions` list.
9. **Install script** — extend `tools/install_python_deps.py` to know
   the new vendor's torch index and special install ordering (e.g.,
   `--no-deps` to protect a CUDA/XPU/ROCm torch from being replaced).
10. **Pack** — add (or extend) a `PLATFORM_MAP` entry in
    `tools/pack_python_env.py` with the new vendor's `vendor` id and
    platform-specific exclusions. The requirements file is derived as
    `requirements-<vendor>.txt`; no separate `requirements` key.

### Multi-vendor per platform (future)

The current packaging model is *one venv per platform*. If you need
two IHVs on the same platform (e.g., NVIDIA + Intel XPU both on
Windows — incompatible torch wheels), each vendor gets its own
`<vendor>/python/` venv and the dispatcher loads the matching EP library
at runtime from the config's `device_vendor`.
