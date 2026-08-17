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
  1. torch + torchvision + torch-tensorrt-rtx + tensorrt-rtx (nvidia: one
     resolve from the nightly CUDA index so torch-tensorrt-rtx pins the
     matching torch nightly; apple: stable torch + torchvision)
  2. bitsandbytes (nvidia only)
  3. General requirements (--upgrade-strategy only-if-needed)
  4. Verify torch survived the full chain

Bytecode-freezing the installed env is a separate step -- see
compile_env.py, invoked by the build/packaging pipeline.
"""

import argparse
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
TENSORRT_RTX_SPEC = "tensorrt-rtx"
PYPI_INDEX = "https://pypi.org/simple"
NVIDIA_INDEX = "https://pypi.nvidia.com"
NGC_INDEX = "https://pypi.ngc.nvidia.com"


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

    req_file = ASSETS_DIR / f"requirements-{vendor}.txt"
    if not req_file.exists():
        print(f"Requirements file not found: {req_file}", file=sys.stderr)
        sys.exit(1)

    py = sys.executable
    # -I: ignore user/system site-packages so Conda/system torch can't shadow
    # the embedded Python's packages (both for pip installs and verification).
    pip = [py, "-I", "-m", "pip", "install", "--no-user", "--quiet", "--no-cache-dir"]

    print(f"\n[1/4] Installing torch stack ({vendor})...")
    if is_nvidia:
        # One resolve from the nightly index: torch-tensorrt-rtx pins the
        # matching torch nightly (the newest torch nightly is usually ahead of
        # the newest torch-tensorrt-rtx, so the resolver pulls torch down to
        # match). --index-url replaces PyPI, so add pypi.org/simple back for
        # tensorrt-rtx and its cu13 runtime libs. No --no-deps: torch-tensorrt-rtx
        # brings tensorrt-rtx + dllist and constrains torch itself.
        torch_args = pip + [
            "--pre",
            TORCH_NIGHTLY_SPEC, TORCHVISION_NIGHTLY_SPEC,
            TORCH_TENSORRT_RTX_SPEC, TENSORRT_RTX_SPEC,
            "--index-url", TORCH_NIGHTLY_INDEX,
            "--extra-index-url", PYPI_INDEX,
            "--extra-index-url", NVIDIA_INDEX,
            "--extra-index-url", NGC_INDEX,
        ]
    else:
        torch_args = pip + [TORCH_VERSION_SPEC, TORCHVISION_VERSION_SPEC]
    _run(torch_args)

    if is_nvidia:
        print("\n[2/4] Installing bitsandbytes...")
        _run(pip + ["bitsandbytes"], fatal=False)
    else:
        print("\n[2/4] bitsandbytes skipped (nvidia-only)")

    print(f"\n[3/4] Installing {req_file.name}...")
    req_args = pip + ["-r", str(req_file), "--upgrade-strategy", "only-if-needed"]
    if is_nvidia:
        req_args += ["--extra-index-url", TORCH_NIGHTLY_INDEX]
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
             fatal=False)
        # modelopt powers the nvfp4_mto load path.
        _run([py, "-I", "-c",
              "import modelopt; "
              "print(f'modelopt {modelopt.__version__} OK')"],
             fatal=False)
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
