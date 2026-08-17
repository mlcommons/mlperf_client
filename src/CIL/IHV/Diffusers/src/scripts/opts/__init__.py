"""Optimization handlers. Side-effect imports here trigger @register_opt.

Each submodule registers exactly one opt name. Importing this package is
the only setup required; runtime.apply_optimizations() looks handlers up
via the registry.

Opts are post-load passes only — they never quantize an unquantized
model. Models are expected to ship pre-quantized; format-specific
loading lives in `models/` (e.g. NVFP4 swap, quanto_qmap rebuild).
"""
from opts import (
    mps_compile,
    mps_memory_tier,
    nvidia_quantize,
    tensorrt_rtx,
)

__all__ = [
    "mps_compile",
    "mps_memory_tier",
    "nvidia_quantize",
    "tensorrt_rtx",
]
