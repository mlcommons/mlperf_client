# IHV Diffusers EP

Execution provider that runs inference via the Hugging Face `diffusers` Python library,
embedded through pybind11 + CPython.

## Status

Functional — builds as a shared library with the standard `API_IHV_*` exports.
Python interpreter is embedded via pybind11.

## Supported platforms

- **Windows** — python-build-standalone at configure time; pip installs CUDA `torch`/torchvision plus dependencies at install step.
- **macOS** — same.

No other host OS is wired in CMake for this IHV.

## Config

Minimal schema (additional optional keys allowed by the validator):

```json
{
  "Name": "Diffusers",
  "LibraryPath": "path/to/IHV_Diffusers.dll",
  "Config": {
    "backend": "CUDA",
    "device_vendor": "NVIDIA",
    "device_type": "GPU",
    "device_id": 0
  }
}
```

| Field           | Values            | Notes |
|-----------------|--------------------|-------|
| `backend`       | `CUDA` (Windows), `MPS` (macOS) | Required; selects the torch device |
| `device_vendor` | `NVIDIA` (CUDA), `APPLE` (MPS), `AMD` (RYZENAI) | Selects which vendor's `python_env.zip` the harness downloads. The EP-dependency `Condition` matches strictly on this key — a config that omits it downloads nothing on a multi-vendor build. |
| `device_type`   | `GPU`              | Optional; display only (defaults to `GPU`) |
| `device_id`     | integer ≥ 0        | Optional; GPU index (CUDA only) |

## Build

Built as an ExternalProject from the parent IHV CMake. Dependencies:

- pybind11 v3.0.4 (FetchContent)
- Python3 Development.Embed (`find_package` against embedded PBS)
- nlohmann_json / JSONSchema validator
- `common_cil`

## Python runtime

At runtime, the embedded tree lives next to the IHV DLL:
`python/` stdlib + `site-packages`, with PyConfig `home` pointing at that layout.

## Development workflow

1. Enable a vendor: `-DMLPERF_IHV_DIFFUSERS_NVIDIA=ON` (Windows) or
   `-DMLPERF_IHV_DIFFUSERS_APPLE=ON` (macOS). `MLPERF_IHV_DIFFUSERS` is
   derived from these — it cannot be set directly.
2. Build `ihv_diffusers` — configure downloads PBS (Win/mac dev), installs wheels on `--install`.
3. Point scenario `LibraryPath` at `build/.../IHV/Diffusers/bin/<Config>/` or use packaged deps.

## Architecture

The Python side is split into vendors / runtimes / models / opts — see
the "Adding a new IHV" section of the parent `README.md` for the layout
and extension points.

## Windows ARM64 (NVIDIA CUDA) environment

The top-level configure examples are in `README_BUILD.md` (Windows ARM64
Build); this section owns the ARM64 Python environment and wheel inputs.

The embedded runtime is pinned to python-build-standalone 3.13.13, release tag
20260510, triplet `aarch64-pc-windows-msvc`. The validated archive SHA-256 is:

```text
9698256BCC4EAAC68FFDCC9F55C063316265768E4987AC374B9D253B1909482D
```

The pinned native and selector package artifacts are:

| Wheel | SHA-256 |
|---|---|
| `torch-2.14.0+cu134-cp313-cp313-win_arm64.whl` | `4F781BABC0E0E0722CC48D0B15107A28E6003FC2B6544F1578B6EB6F5177DCB5` |
| `torch_tensorrt_rtx-2.14.0+cu134-cp313-cp313-win_arm64.whl` | `D93EC8467507A5948D3E2E00D35B41A0262E8203CCA9F956762704A24D3D33E4` |
| `tensorrt_rtx-1.6.1.120.tar.gz` | `3540790F510AD03F477B38C36C25C2C5359494F865F9DF1326C997954334B726` |
| `tensorrt_rtx_cu13-1.6.1.120.tar.gz` | `5CA6F977B43F5D5CB759EB7B195FCF689BDEBFE8E9EC4B5AA5754431E13DDA99` |
| `tensorrt_rtx_cu13_bindings-1.6.1.120-cp313-none-win_arm64.whl` | `8B69E993BC6FDE954875C8A60B10AB9D84F6CD5E0AA2729B12AB409C384F8691` |
| `tensorrt_rtx_cu13_libs-1.6.1.120-py3-none-win_arm64.whl` | `B478EA95991280A3CB8F8E0F8E92E9D07C0525FA88C39E8C209D1A0B772A31D8` |
| `bitsandbytes-0.50.2-py3-none-win_arm64.whl` | `8437AB68A04EA56DAF1D6ECB54230FB1D88BE4B89FE2D79BC399BC0203B487CF` |

These packages use exact versions. They must not be changed to a range,
an unqualified `--pre` requirement, or a "latest" URL:

```text
torch==2.14.0+cu134
torch-tensorrt-rtx==2.14.0+cu134
tensorrt-rtx==1.6.1.120
tensorrt-rtx-cu13==1.6.1.120
tensorrt-rtx-cu13-bindings==1.6.1.120
tensorrt-rtx-cu13-libs==1.6.1.120
bitsandbytes==0.50.2
```

The direct NVIDIA ImageGen Python requirements are also exact:

| Package | Version |
|---|---:|
| diffusers | 0.40.0 |
| transformers | 5.16.1 |
| accelerate | 1.14.0 |
| safetensors | 0.8.0 |
| optimum-quanto | 0.2.7 |
| torchao | 0.18.0 |
| sentencepiece | 0.2.2 |
| protobuf | 7.36.0 |
| huggingface-hub | 1.29.0 |
| Pillow | 12.3.0 |
| numpy | 2.5.2 |
| PyInstaller | 6.22.2 |
| nvidia-modelopt | 0.46.0 |

### Torch-TensorRT wheel provenance

The default online path uses public, hash-qualified URLs for Torch and
Torch-TensorRT and public PyPI/NVIDIA indexes for the remaining packages. The
Torch-TensorRT wheel declares `torch>=2.14.0.dev,<2.15.0` and TensorRT RTX
`>=1.6.1.120,<1.7.0.0`. A complete PE import/export audit found that all 578
named imports from its ARM64 `torchtrt.dll` into `torch_cpu.dll`,
`torch_cuda.dll`, `c10_cuda.dll`, and `c10.dll` are exported by the pinned OOT
Torch wheel, including the five-argument `c10_cuda_check_implementation` ABI.

For release retention or an offline build, keep all resolved files in an
immutable wheelhouse and set `MLPERF_DIFFUSERS_WHEELHOUSE` when running
`install_python_deps.py`. The two source metapackages in the table select the
pinned ARM64 TensorRT RTX bindings and libraries.

### Version locks and packing

The NVIDIA direct requirements and the validated transitive environment are
version-locked in:

- `src/CIL/IHV/Diffusers/src/assets/requirements-nvidia-win-arm64.txt`;
- `src/CIL/IHV/Diffusers/src/assets/constraints-nvidia-win-arm64.txt`.

The installer automatically applies the constraints file when it is running
under Windows ARM64. It also writes `pip_freeze.txt` into the packaged
environment for auditing.

The constraints file contains exact `==` pins for the complete validated
runtime dependency closure, including packages pulled transitively by Torch,
Transformers, Diffusers, ModelOpt, and PyInstaller. Do not replace it with a
new resolver result without rerunning the ARM64 benchmark validation.

The standard packer recognizes the target explicitly:

```powershell
python src/CIL/IHV/Diffusers/tools/pack_python_env.py `
  --platform windows_arm64 `
  --output python_env.zip
```

Run the packer on an ARM64 host because both fresh packing and `--from-install`
execute the embedded interpreter to freeze bytecode. An x64 cross-build must
consume an immutable `python_env.zip` prepared on ARM64 hardware.

When cross-building on an x64 host, the target ARM64 interpreter cannot run.
Use `-DMLPERF_IHV_DIFFUSERS_INSTALL_PYTHON=OFF`, prepare and validate the ARM64
`python_env.zip` separately (on ARM64 hardware or with a target-aware wheel
staging process), and host it at the location referenced by the generated
`ep_dependencies_config_*.json`.
This is how the validated all-three build was produced. A native ARM64 build
host may leave `MLPERF_IHV_DIFFUSERS_INSTALL_PYTHON=ON`.

### Compatibility rule for existing benchmark targets

The Windows ARM64 pins apply to the Windows ARM64 path only unless a dependency
is already shared and pinned by checkpoint `c8d2dc0`. Do not update the x64,
macOS, iOS, or Linux dependency stacks as part of that work. In particular,
the existing Windows x64 NVIDIA ImageGen path must retain:

- PyTorch index `https://download.pytorch.org/whl/nightly/cu130`;
- `torch==2.14.0.dev20260623+cu130`;
- `torchvision==0.29.0.dev20260623+cu130`;
- `torch-tensorrt-rtx==2.14.0.dev20260623+cu130`;
- ONNX Runtime GenAI WinML `0.14.1`.

The Windows ARM64 selections (`cu134` and ONNX Runtime GenAI WinML `0.15.2`)
are guarded by target-architecture checks and must never become shared
defaults. Benchmark certification takes precedence over dependency freshness.

The June 23 x64 PyTorch nightlies have rotated out of the cu130 index listing,
but their version-specific public CDN objects remain available. The installer
therefore uses exact direct URLs with SHA-256 fragments for the x64 Torch,
torchvision, and Torch-TensorRT-RTX wheels, and applies
`constraints-nvidia-win-x64.txt` to reproduce the rest of the checkpoint
environment. The x64-specific requirements file omits the unavailable NGC
index; all locked x64 packages resolve from PyTorch, PyPI, or
`pypi.nvidia.com`. This does not require a private wheel mirror.

These nightly pins are a compatibility bridge. In a future benchmark version,
prefer a stable official NVIDIA/PyTorch stack published for both Windows x64
and Windows ARM64, then revalidate performance and accuracy before changing
the pins.
