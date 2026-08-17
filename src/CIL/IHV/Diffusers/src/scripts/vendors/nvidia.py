"""NVIDIA Vendor — CUDA hardware concerns only."""
from __future__ import annotations

import logging
from typing import Any

import torch

from vendors.base import Vendor

log = logging.getLogger(__name__)


def _configure_cuda() -> dict[str, Any]:
    """Apply NVIDIA-recommended inference flags. Returns the applied set."""
    info: dict[str, Any] = {}
    if not torch.cuda.is_available():
        return info

    torch.backends.cudnn.benchmark = True
    info["cudnn.benchmark"] = True

    torch.backends.cuda.matmul.allow_tf32 = True
    torch.backends.cudnn.allow_tf32 = True
    info["matmul.allow_tf32"] = True

    if hasattr(torch, "set_float32_matmul_precision"):
        torch.set_float32_matmul_precision("high")
        info["float32_matmul_precision"] = "high"

    try:
        torch.backends.cuda.enable_flash_sdp(True)
        torch.backends.cuda.enable_mem_efficient_sdp(True)
        info["sdp.flash"] = True
        info["sdp.mem_efficient"] = True
    except AttributeError:
        pass

    log.info("CUDA flags: %s", info)
    return info


class NvidiaVendor(Vendor):
    display_name = "NVIDIA CUDA"
    backend = "cuda"

    def validate(self, deps_dir: str | None = None) -> None:
        if not torch.cuda.is_available():
            raise RuntimeError(
                "device_vendor=NVIDIA but CUDA is not available. Install a "
                "CUDA-enabled PyTorch build or run on an Apple MPS host."
            )

    def configure_globals(self) -> dict[str, Any]:
        return _configure_cuda()

    def resolve_device(self, requested: str | None) -> torch.device:
        dev = torch.device(requested or "cuda:0")
        if dev.type != "cuda":
            raise ValueError(
                f"NvidiaVendor requires a cuda device, got {dev!s}."
            )
        torch.cuda.set_device(dev)
        return dev

    def _device_sync_fn(self):
        return torch.cuda.synchronize

    def make_module_timer(self):
        from timing.timer import CudaTimer
        return CudaTimer()

    def make_generator(self, seed: int,
                       *, prefer_cpu: bool = False) -> torch.Generator:
        gen = torch.Generator(device="cpu" if prefer_cpu else "cuda")
        gen.manual_seed(int(seed))
        return gen

    def default_dtype(self) -> str:
        return "bfloat16"

    def release_memory(self) -> None:
        import gc
        gc.collect()
        if torch.cuda.is_available():
            torch.cuda.empty_cache()
            torch.cuda.ipc_collect()

    def system_info(self) -> dict[str, Any]:
        info = super().system_info()
        if torch.cuda.is_available():
            info["device_name"] = torch.cuda.get_device_name(0)
            info["device_capability"] = list(torch.cuda.get_device_capability(0))
            info["device_count"] = torch.cuda.device_count()
            info["cuda_version"] = torch.version.cuda
            try:
                info["driver_version"] = torch.cuda.driver_version()
            except Exception:
                info["driver_version"] = None
        return info
