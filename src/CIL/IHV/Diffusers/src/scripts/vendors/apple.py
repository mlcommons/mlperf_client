"""Apple Vendor — MPS hardware concerns only."""
from __future__ import annotations

import logging
import os
import platform
import subprocess
from typing import Any

import torch

from vendors.base import Vendor, parse_dtype

log = logging.getLogger(__name__)


def mps_available() -> bool:
    return (
        hasattr(torch.backends, "mps")
        and torch.backends.mps.is_available()
        and torch.backends.mps.is_built()
    )


_TOTAL_RAM_GB: float | None = None


def total_unified_memory_gb() -> float:
    """macOS unified memory in GB, cached after first call."""
    global _TOTAL_RAM_GB
    if _TOTAL_RAM_GB is not None:
        return _TOTAL_RAM_GB
    bytes_total = 0
    try:
        out = subprocess.run(
            ["sysctl", "-n", "hw.memsize"],
            capture_output=True, text=True, check=True, timeout=2,
        )
        bytes_total = int(out.stdout.strip())
    except (FileNotFoundError, subprocess.CalledProcessError, ValueError,
            subprocess.TimeoutExpired):
        try:
            bytes_total = (os.sysconf("SC_PAGE_SIZE")
                           * os.sysconf("SC_PHYS_PAGES"))
        except (ValueError, OSError, AttributeError):
            bytes_total = 0
    _TOTAL_RAM_GB = bytes_total / (1024 ** 3)
    return _TOTAL_RAM_GB


class AppleVendor(Vendor):
    display_name = "Apple MPS"
    backend = "mps"

    def __init__(self) -> None:
        self._device: torch.device | None = None

    def validate(self, deps_dir: str | None = None) -> None:
        if mps_available():
            return
        raise RuntimeError(
            f"device_vendor=Apple but MPS is not available on this host "
            f"(platform={platform.system()}, machine={platform.machine()}). "
            "The Diffusers EP requires Apple Silicon with MPS."
        )

    def configure_globals(self) -> dict[str, Any]:
        allow = os.environ.get("MLPERF_DIFFUSERS_MPS_ALLOW_FALLBACK") == "1"
        os.environ["PYTORCH_ENABLE_MPS_FALLBACK"] = "1" if allow else "0"
        os.environ.setdefault("PYTORCH_MPS_HIGH_WATERMARK_RATIO", "0.0")

        ram_gb = total_unified_memory_gb()
        info: dict[str, Any] = {
            "env.PYTORCH_ENABLE_MPS_FALLBACK":
                os.environ["PYTORCH_ENABLE_MPS_FALLBACK"],
            "mps_strict": not allow,
            "env.PYTORCH_MPS_HIGH_WATERMARK_RATIO":
                os.environ["PYTORCH_MPS_HIGH_WATERMARK_RATIO"],
            "unified_ram_gb": round(ram_gb, 1),
            "mps_available": mps_available(),
        }
        log.info("Apple backend flags: %s", info)
        return info

    def resolve_device(self, requested: str | None) -> torch.device:
        if requested:
            dev = torch.device(requested)
            if dev.type != "mps":
                raise ValueError(
                    f"AppleVendor requires an mps device, got {dev!s}.")
        else:
            dev = torch.device("mps")
        self._device = dev
        return dev

    def normalize_dtype(self, name: str) -> torch.dtype:
        return parse_dtype(name)

    def _device_sync_fn(self):
        if self._device is not None and self._device.type == "mps":
            sync = getattr(torch.mps, "synchronize", None)
            if sync is not None:
                return sync
        return lambda: None

    def make_module_timer(self):
        from timing.timer import HostTimer
        return HostTimer(sync_fn=self._device_sync_fn())

    def make_generator(self, seed: int,
                       *, prefer_cpu: bool = False) -> torch.Generator:
        # MPS generators are unreliable (non-determinism vs CPU) and the
        # cpu_offload path needs CPU anyway — always use CPU regardless
        # of `prefer_cpu`.
        _ = prefer_cpu
        gen = torch.Generator(device="cpu")
        gen.manual_seed(int(seed))
        return gen

    def default_dtype(self) -> str:
        return "bfloat16"

    def release_memory(self) -> None:
        import gc
        gc.collect()
        if self._device is not None and self._device.type == "mps":
            empty_cache = getattr(getattr(torch, "mps", None),
                                  "empty_cache", None)
            if empty_cache is not None:
                empty_cache()

    def system_info(self) -> dict[str, Any]:
        info = super().system_info()
        info["mps_available"] = mps_available()
        info["mps_built"] = bool(
            getattr(torch.backends, "mps", None)
            and torch.backends.mps.is_built()
        )
        info["macos_release"] = platform.mac_ver()[0] or None
        info["machine"] = platform.machine()
        info["unified_ram_gb"] = round(total_unified_memory_gb(), 1)
        info["mps_strict"] = \
            os.environ.get("PYTORCH_ENABLE_MPS_FALLBACK") == "0"
        return info
