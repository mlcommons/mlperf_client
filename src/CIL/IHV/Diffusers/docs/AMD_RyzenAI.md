# MLPerf Client Benchmark — FLUX.2 Klein 4B (AMD Ryzen AI)

Image generation on the AMD Ryzen AI NPU through the Diffusers IHV. The transformer and VAE
decoder run on the NPU via ONNX Runtime + the RyzenAI plugin EP
(`onnxruntime_providers_ryzenai.dll`); the Qwen3 text encoder runs on CPU.

## Runtime Requirements
- Install the [AMD NPU driver `NPU_RAI_376_WHQL`](https://download.amd.com/opendownload/RyzenAI/1.8.0b0/NPU_RAI_376_WHQL.zip) (version `32.0.20101.3760`). This is the only manual install; the RyzenAI runtime DLLs download automatically at run time. `xrt_coreutil.dll` is supplied by the driver and must match it — a mismatch fails at session init with `invalid unordered_map<K, T> key` (see [Troubleshooting](#troubleshooting)).
- Install Python deps: `tools/install_python_deps.py --vendor amd` (installs `onnxruntime==1.25.1`, CPU `torch`, `diffusers>=0.38`; **not** `onnxruntime-directml`, which is incompatible with the RyzenAI DLLs).
- Verify the NPU: `& "C:\Windows\System32\AMD\xrt-smi.exe" examine`

## Build
Build `mlperf-windows.exe` with the Diffusers IHV + AMD vendor. From a Visual Studio x64 developer prompt at the repo root:
```
cmake -G "Visual Studio 17 2022" -A x64 -S . -B build -DMLPERF_IHV_DIFFUSERS_AMD=ON
cmake --build build --config Release --target CLI
```
The first build downloads an embedded Python + packages (~4 GB); later builds skip it. See the Diffusers [README](../README.md) for build options.

## Best Performance Setup
- Reboot and close as many background tasks and applications as possible.
- Go to Windows → Settings → System → Power, and set the power mode to `Best Performance`.
- Set `turbo` performance mode for NPU (requires AC power). Open a command prompt as `Administrator`:
```
cd C:\Windows\System32\AMD
xrt-smi configure --pmode turbo
```

## Run benchmark
No environment variables are needed once the driver is installed. From the binary directory:
```
mlperf-windows.exe -c data/configs/vendors_default/image-gen/experimental/flux2klein/AMD_Diffusers-RyzenAI_NPU.json --pause false
```
The config auto-downloads the model and RyzenAI runtime DLLs; no manual model prep. Default run: 4 iterations, 1 warm-up. Add `-b skip_all` to reuse already-downloaded dependencies.

Success output:
```
[diffusers] pipeline components:
  vae           OnnxRuntimeModel ...
  transformer   Flux2KleinTransformer ...
[mlperf-amd] runtime.generate: calling pipeline (image 1/4, seed=42)
[mlperf-amd] runtime.generate: pipeline returned (image 1/4)
```

# Troubleshooting

`RUNTIME_EXCEPTION : ... invalid unordered_map<K, T> key` (also `Failed to initialize fusion runtime for node '...'`) — the installed NPU driver does not match the RyzenAI runtime, usually an `xrt_coreutil.dll` mismatch.
- Install the matching driver `NPU_RAI_376_WHQL` (`32.0.20101.3760`) and re-run.
- Check the active driver: `pnputil /enum-devices /class ComputeAccelerator`.

For more information, visit our public [Ryzen AI docs](https://ryzenai.docs.amd.com/en/latest/sd_demo.html). 
