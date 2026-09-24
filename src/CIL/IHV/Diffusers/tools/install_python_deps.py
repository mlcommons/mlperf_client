"""Install Python dependencies for the Diffusers EP.

Installs torch then the vendor-specific requirements file. Run with the
Python environment you intend to use for inference (embedded PBS python,
venv, or system python).

The vendor -- not the host platform -- selects the requirements file and
the install strategy, matching the rest of the EP (dispatcher, per-vendor
EP libraries, <vendor>/python layout). --vendor defaults to the host platform
when omitted (darwin -> apple, otherwise -> nvidia).

Usage:
    python install_python_deps.py [--vendor {nvidia,apple,amd}]
    <embedded-python>/python.exe -I install_python_deps.py --vendor nvidia

The install order is carefully sequenced to keep a consistent CUDA build:
  1. torch + torchvision + torch-tensorrt-rtx + tensorrt-rtx (NVIDIA ARM64:
     an exact public OOT Torch 2.14 and ABI-matched public Torch-TensorRT 2.14
     wheel; other NVIDIA targets: matching CUDA nightlies; Apple: stable wheels)
  2. bitsandbytes (nvidia only)
  3. General requirements (--upgrade-strategy only-if-needed)
  4. Verify torch survived the full chain

Bytecode-freezing the installed env is a separate step -- see
compile_env.py, invoked by the build/packaging pipeline.
"""

import argparse
import os
import platform
import subprocess
import sys
from pathlib import Path

ASSETS_DIR = Path(__file__).resolve().parent.parent / "src" / "assets"

# Apple stays on stable torch/torchvision (default macOS MPS wheel).
TORCH_VERSION_SPEC = "torch>=2.12,<2.13"
TORCHVISION_VERSION_SPEC = "torchvision>=0.27,<0.28"

# NVIDIA uses torch nightly + torch-tensorrt-rtx. They are resolved together
# so torch-tensorrt-rtx pins the matching torch nightly (the newest torch
# nightly is usually ahead of the newest torch-tensorrt-rtx). Edit these to
# pin versions; leaving torch unpinned lets the resolver align it.
TORCH_NIGHTLY_INDEX = "https://download.pytorch.org/whl/nightly/cu130"
TORCH_NIGHTLY_SPEC = "torch==2.14.0.dev20260623+cu130"
TORCHVISION_NIGHTLY_SPEC = "torchvision==0.29.0.dev20260623+cu130"
TORCH_TENSORRT_RTX_SPEC = "torch-tensorrt-rtx==2.14.0.dev20260623+cu130"

# The checkpoint Windows x64 nightlies have rotated out of the cu130 index
# listing, but their version-specific CDN objects remain public. Use direct
# URLs with published SHA-256 values so CI installs the original benchmark
# stack instead of floating to a newer nightly. A local wheelhouse still uses
# the exact version specs above.
WINDOWS_X64_TORCH_URL = (
    "https://download.pytorch.org/whl/nightly/cu130/"
    "torch-2.14.0.dev20260623%2Bcu130-cp313-cp313-win_amd64.whl"
    "#sha256=00eb1852fdba8b8bbf35f61030d80d7f11f8b19b6cf9edc34b7680ba370b1391"
)
WINDOWS_X64_TORCHVISION_URL = (
    "https://download.pytorch.org/whl/nightly/cu130/"
    "torchvision-0.29.0.dev20260623%2Bcu130-cp313-cp313-win_amd64.whl"
    "#sha256=5c358a3e18348308cdb5c82afff7633c35d9a985269e2855c54d1b50fb76179b"
)
WINDOWS_X64_TORCH_TENSORRT_RTX_URL = (
    "https://download.pytorch.org/whl/nightly/cu130/"
    "torch_tensorrt_rtx-2.14.0.dev20260623%2Bcu130-cp313-cp313-win_amd64.whl"
    "#sha256=df46d598722600dcd77876c587ee53213fc40177f76f91a56625d6305939e3ad"
)
WINDOWS_X64_TENSORRT_RTX_SPEC = "tensorrt-rtx==1.5.0.114"
WINDOWS_X64_BITSANDBYTES_SPEC = "bitsandbytes==0.50.0"

# Validated Windows ARM64 CUDA 13.4 stack. Keep separate from the established
# Windows x64 CUDA 13.0 environment above.
ARM64_TORCH_NIGHTLY_INDEX = "https://pypi.nvidia.com/nvtorch_oot/torch/"
ARM64_TORCH_NIGHTLY_SPEC = "torch==2.14.0+cu134"
ARM64_TORCH_TENSORRT_RTX_SPEC = "torch-tensorrt-rtx==2.14.0+cu134"
ARM64_TENSORRT_RTX_SPEC = "tensorrt-rtx==1.6.1.120"
ARM64_BITSANDBYTES_SPEC = "bitsandbytes==0.50.2"
ARM64_TORCH_URL = (
    "https://pypi.nvidia.com/nvtorch_oot/torch/"
    "torch-2.14.0%2Bcu134-cp313-cp313-win_arm64.whl"
    "#sha256=4f781babc0e0e0722cc48d0b15107a28e6003fc2b6544f1578b6eb6f5177dcb5"
)
ARM64_TORCH_TENSORRT_RTX_URL = (
    "https://pypi.nvidia.com/nvtorch_oot/torch-tensorrt-rtx/"
    "torch_tensorrt_rtx-2.14.0%2Bcu134-cp313-cp313-win_arm64.whl"
    "#sha256=d93ec8467507a5948d3e2e00d35b41a0262e8203cca9f956762704a24d3d33e4"
)
PYPI_INDEX = "https://pypi.org/simple"
NVIDIA_INDEX = "https://pypi.nvidia.com"


def _run(args, fatal=True):
    print(f"  $ {' '.join(args)}")
    rc = subprocess.call(args)
    if rc != 0:
        if fatal:
            print(f"FAILED (rc={rc})", file=sys.stderr)
            sys.exit(rc)
        else:
            print(f"WARNING: command failed (rc={rc}), continuing...",
                  file=sys.stderr)
    return rc


def _default_vendor():
    """Vendor implied by the host platform when --vendor is omitted."""
    return "apple" if sys.platform == "darwin" else "nvidia"


def main(argv=None):
    p = argparse.ArgumentParser(
        description="Install Python dependencies for the Diffusers EP.")
    p.add_argument("--vendor", choices=("nvidia", "apple", "amd"),
                   default=_default_vendor(),
                   help="Vendor whose requirements file and install "
                        "strategy to use. Defaults to the host platform "
                        "(darwin -> apple, otherwise -> nvidia).")
    args = p.parse_args(argv)

    vendor = args.vendor
    is_nvidia = vendor == "nvidia"
    is_apple = vendor == "apple"
    is_amd = vendor == "amd"

    machine = platform.machine().lower()
    is_windows_arm64 = (sys.platform == "win32" and
                        machine in ("arm64", "aarch64"))
    is_windows_x64 = (sys.platform == "win32" and
                      machine in ("amd64", "x86_64"))
    if is_nvidia:
        # The NVIDIA Diffusers EP is Windows-only; each arch has its own
        # locked environment.
        if is_windows_arm64:
            arch = "win-arm64"
        elif is_windows_x64:
            arch = "win-x64"
        else:
            print("NVIDIA Diffusers dependencies are only defined for "
                  "Windows x64/ARM64 (got "
                  f"{sys.platform}/{machine}).", file=sys.stderr)
            sys.exit(1)
        req_file = ASSETS_DIR / f"requirements-nvidia-{arch}.txt"
        constraints = ASSETS_DIR / f"constraints-nvidia-{arch}.txt"
    else:
        req_file = ASSETS_DIR / f"requirements-{vendor}.txt"
        constraints = None
    if not req_file.exists():
        print(f"Requirements file not found: {req_file}", file=sys.stderr)
        sys.exit(1)

    py = sys.executable
    wheelhouse = os.environ.get("MLPERF_DIFFUSERS_WHEELHOUSE")
    # -I: ignore user/system site-packages so Conda/system torch can't shadow
    # the embedded Python's packages (both for pip installs and verification).
    pip = [py, "-I", "-m", "pip", "install", "--no-user", "--quiet", "--no-cache-dir"]
    if constraints is not None:
        pip += ["--constraint", str(constraints)]
        if wheelhouse:
            pip += ["--find-links", wheelhouse]

    print(f"\n[1/4] Installing torch stack ({vendor})...")
    if is_nvidia:
        # Install the exact mutually compatible stack in one resolve.
        # --index-url replaces PyPI, so add pypi.org/simple back for
        # tensorrt-rtx and its cu13 runtime libs. Direct, hash-qualified URLs
        # keep the online Windows ARM64 build reproducible even after nightly
        # index listings rotate; an optional wheelhouse supports offline use.
        if is_windows_arm64:
            if wheelhouse:
                torch_specs = [
                    ARM64_TORCH_NIGHTLY_SPEC,
                    ARM64_TORCH_TENSORRT_RTX_SPEC,
                    ARM64_TENSORRT_RTX_SPEC,
                ]
            else:
                torch_specs = [
                    ARM64_TORCH_URL,
                    ARM64_TORCH_TENSORRT_RTX_URL,
                    ARM64_TENSORRT_RTX_SPEC,
                ]
            torch_index = ARM64_TORCH_NIGHTLY_INDEX
        else:
            if wheelhouse:
                torch_specs = [
                    TORCH_NIGHTLY_SPEC,
                    TORCHVISION_NIGHTLY_SPEC,
                    TORCH_TENSORRT_RTX_SPEC,
                    WINDOWS_X64_TENSORRT_RTX_SPEC,
                ]
            else:
                torch_specs = [
                    WINDOWS_X64_TORCH_URL,
                    WINDOWS_X64_TORCHVISION_URL,
                    WINDOWS_X64_TORCH_TENSORRT_RTX_URL,
                    WINDOWS_X64_TENSORRT_RTX_SPEC,
                ]
            torch_index = TORCH_NIGHTLY_INDEX
        torch_args = pip + [
            "--pre", *torch_specs,
            "--index-url", torch_index,
            "--extra-index-url", PYPI_INDEX,
            "--extra-index-url", NVIDIA_INDEX,
        ]
    else:
        torch_args = pip + [TORCH_VERSION_SPEC, TORCHVISION_VERSION_SPEC]
    _run(torch_args)

    if is_nvidia:
        print("\n[2/4] Installing bitsandbytes...")
        if is_windows_arm64:
            bitsandbytes_spec = ARM64_BITSANDBYTES_SPEC
        else:
            bitsandbytes_spec = WINDOWS_X64_BITSANDBYTES_SPEC
        _run(pip + [bitsandbytes_spec])
    else:
        print("\n[2/4] bitsandbytes skipped (nvidia-only)")

    print(f"\n[3/4] Installing {req_file.name}...")
    req_args = pip + ["-r", str(req_file), "--upgrade-strategy", "only-if-needed"]
    if is_nvidia:
        req_args += ["--extra-index-url", torch_index]
    _run(req_args)

    print("\n[4/4] Verifying torch...")
    if is_nvidia:
        _run([py, "-I", "-c",
              "import torch; assert torch.version.cuda is not None, "
              "'torch is CPU-only -- CUDA wheel did not survive install'; "
              "print(f'torch {torch.__version__} "
              "CUDA {torch.version.cuda} OK')"])
        # torch-tensorrt-rtx imports as torch_tensorrt; verify so a mismatched
        # build surfaces here, not silently at inference time.
        _run([py, "-I", "-c",
              "import torch_tensorrt; "
              "print(f'torch_tensorrt {torch_tensorrt.__version__} OK')"],
             fatal=is_windows_arm64)
        # modelopt powers the nvfp4_mto load path.
        _run([py, "-I", "-c",
              "import modelopt; "
              "print(f'modelopt {modelopt.__version__} OK')"],
             fatal=is_windows_arm64)
    elif is_apple:
        _run([py, "-I", "-c",
              "import torch; assert torch.backends.mps.is_built(), "
              "'torch lacks MPS support -- wrong wheel installed'; "
              "print(f'torch {torch.__version__} "
              "MPS OK')"])
    elif is_amd:
        _run([py, "-I", "-c",
              "import torch; assert torch.version.cuda is None, "
              "'torch must be CPU-only for AMD RyzenAI path'; "
              "print(f'torch {torch.__version__} CPU OK')"])

    print("\nDone.")


if __name__ == "__main__":
    main()
