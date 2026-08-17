"""Lightweight host probe for EnumerateDevices.

Imports `torch` only — no diffusers / transformers / etc. so the cost
stays bounded. Stateless: every call walks the host fresh.

Returned list shape:
    [{"device_id": int, "device_name": str, "total_memory_gb": float, ...}]
"""
from __future__ import annotations


def enumerate_devices(backend: str) -> list[dict]:
    backend = (backend or "").upper()
    if backend == "CUDA":
        return _enumerate_cuda()
    if backend == "MPS":
        return _enumerate_mps()
    if backend == "RYZENAI":
        return _enumerate_ryzenai()
    raise ValueError(
        f"unsupported backend {backend!r}; choose CUDA, MPS or RYZENAI")


def _enumerate_cuda() -> list[dict]:
    import torch
    if not torch.cuda.is_available():
        return []
    out: list[dict] = []
    for i in range(torch.cuda.device_count()):
        props = torch.cuda.get_device_properties(i)
        out.append({
            "device_id": i,
            "device_name": props.name,
            "total_memory_gb": round(props.total_memory / (1024 ** 3), 1),
            "capability": f"{props.major}.{props.minor}",
        })
    return out


def _enumerate_mps() -> list[dict]:
    import torch
    if not (hasattr(torch.backends, "mps")
            and torch.backends.mps.is_available()):
        return []
    from vendors.apple import total_unified_memory_gb
    return [{
        "device_id": 0,
        "device_name": "Apple Silicon GPU",
        "total_memory_gb": round(total_unified_memory_gb(), 1),
    }]


def _enumerate_ryzenai() -> list[dict]:
    # AMD Ryzen AI NPU. Kept torch-only/lightweight like the others; the NPU is
    # device 0. Real NPU availability is enforced downstream when the RyzenAI
    # ORT session is built (the provider DLL load there will fail if absent).
    return [{
        "device_id": 0,
        "device_name": "AMD Ryzen AI NPU",
    }]
