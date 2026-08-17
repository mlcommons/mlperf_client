"""NVIDIA / CUDA-specific PyInstaller contribution.

Brings in torch_tensorrt, bitsandbytes, torchao, the triton runtime, and the
runtime hook that makes triton's in-tree backend discovery work inside a
frozen exe.

Each `collect_*` call is wrapped in `_safe` in the aggregator — missing
packages are skipped silently, so this same module is fine to import on
macOS (where torch_tensorrt etc. aren't installed) without erroring.
"""
from __future__ import annotations

import os


def contribute(*, tools_dir: str) -> dict:
    return {
        "hidden_packages": [
            "torchao", "bitsandbytes",
            "torch_tensorrt", "tensorrt",
        ],
        "metadata_packages": [
            "torchao", "bitsandbytes",
        ],
        "data_packages": ["triton"],
        "binary_packages": [
            "tensorrt_libs",          # pre-cu13 split (older TRT 9.x)
            "tensorrt_cu13_libs",     # cu13 split (TRT 10+)
            "torch_tensorrt",
            "tensorrt", "tensorrt_bindings",
            "tensorrt_cu13", "tensorrt_cu13_bindings",
        ],
        # Frozen-exe shim for triton in-tree backend discovery + bundled tcc.
        "runtime_hooks": [os.path.join(tools_dir, "pyi_rt_ck_probe.py")],
    }
