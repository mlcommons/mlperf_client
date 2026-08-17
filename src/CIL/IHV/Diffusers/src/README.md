# IHV Diffusers EP

Execution provider that runs inference via the Hugging Face `diffusers` Python library,
embedded through pybind11 + CPython.

## Status

Functional — builds as a shared library with the standard `API_IHV_*` exports.
Python interpreter is embedded via pybind11.

## Supported platforms

- **Windows** — python-build-standalone at configure time; pip installs CUDA `torch`/torchvision plus dependencies at install step.
- **macOS** — same for dev builds; BeeWare XCFramework plus pre-populated packages for publishing builds.

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
- Python3 Development.Embed (`find_package` against embedded PBS or BeeWare)
- nlohmann_json / JSONSchema validator
- `common_cil`

## Python runtime

At runtime (dev / non-publishing unpack), the embedded tree lives next to the IHV DLL:
`python/` stdlib + `site-packages`, with PyConfig `home` pointing at that layout.

Publishing macOS resolves Python under the app bundle frameworks path (see `python_bridge.cpp`).

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
