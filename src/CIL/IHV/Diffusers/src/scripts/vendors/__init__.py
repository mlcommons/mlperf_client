"""Per-vendor runtime layer.

A `Vendor` owns vendor-specific concerns: validation, global flag setup,
device resolution, dtype defaults, timer factories, generators, memory
release.

`get_vendor()` picks the concrete class. Primary key is `device_vendor`
("NVIDIA"/"Apple"). When absent it falls back to `device_type`
("GPU"/"NPU") and probes the host for the matching backend. CPU is
not supported by the Diffusers EP.
"""
from vendors.base import Vendor


def get_vendor(device_vendor: str | None = None,
               device_type: str | None = None) -> Vendor:
    v = (device_vendor or "").strip().lower()
    if v in ("nvidia", "cuda"):
        from vendors.nvidia import NvidiaVendor
        return NvidiaVendor()
    if v in ("apple", "mps"):
        from vendors.apple import AppleVendor
        return AppleVendor()
    if v in ("amd", "ryzenai"):
        from vendors.amd import AmdVendor
        return AmdVendor()
    if v == "cpu":
        raise ValueError(
            "device_vendor='CPU' is not supported by the Diffusers EP. "
            "Use NVIDIA (CUDA) or Apple (MPS).")
    if v:
        raise ValueError(f"unknown device_vendor {device_vendor!r}")

    d = (device_type or "").strip().lower()
    if d == "cpu":
        raise ValueError(
            "device_type='CPU' is not supported by the Diffusers EP. "
            "Use device_type='GPU' on a CUDA or MPS host.")
    if d in ("gpu", "auto", ""):
        import torch
        if torch.cuda.is_available():
            from vendors.nvidia import NvidiaVendor
            return NvidiaVendor()
        if (hasattr(torch.backends, "mps")
                and torch.backends.mps.is_available()):
            from vendors.apple import AppleVendor
            return AppleVendor()
        raise RuntimeError(
            "device_type=GPU but neither CUDA nor MPS is available on this "
            "host. The Diffusers EP requires a CUDA or MPS GPU.")

    raise ValueError(
        f"cannot resolve vendor from device_vendor={device_vendor!r}, "
        f"device_type={device_type!r}")


__all__ = ["Vendor", "get_vendor"]
