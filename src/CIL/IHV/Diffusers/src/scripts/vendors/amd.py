"""AMD Vendor — RyzenAI ONNX (CPU torch host, NPU via ORT EP).

Owns RyzenAI plugin-EP registration (``RyzenAILightExecutionProvider`` +
``onnxruntime_providers_ryzenai.dll``): DLL discovery, one-time EP library
registration, and the SessionOptions used by the ONNX submodules in
``models.flux2_klein_amd``.
"""
from __future__ import annotations

import ctypes
import logging
import os
import sys
from pathlib import Path
from typing import Any

import onnxruntime
import torch

from vendors.base import Vendor

log = logging.getLogger(__name__)

_EP_NAME = "RyzenAILightExecutionProvider"
_PROVIDER_DLL = "onnxruntime_providers_ryzenai.dll"


def resolve_ryzenai_provider_dll(*, deps_dir: str | None = None) -> str | None:
    """Resolve onnxruntime_providers_ryzenai.dll from env, deps, or sys.path.

    Search order:
    1. ``RYZENAI_CUSTOM_OPS_DLL`` — full path to the DLL file
    2. ``RYZEN_AI_INSTALLATION_PATH`` — Ryzen AI install root
       (``<root>/deployment/onnxruntime_providers_ryzenai.dll``)
    3. ``deps_dir`` — harness EP deps directory
    4. entries on ``sys.path``
    """
    ordered: list[Path] = []

    custom_ops_dll = os.environ.get("RYZENAI_CUSTOM_OPS_DLL", "").strip()
    if custom_ops_dll:
        ordered.append(Path(custom_ops_dll))

    rai_installation_path = os.environ.get("RYZEN_AI_INSTALLATION_PATH", "").strip()
    if rai_installation_path:
        ordered.append(Path(rai_installation_path) / "deployment" / _PROVIDER_DLL)

    if deps_dir:
        dp = Path(deps_dir)
        # deps_dir may be the vendor subdir (amd/); also check its parent
        ordered.append(dp / _PROVIDER_DLL)
        ordered.append(dp.parent / _PROVIDER_DLL)
    for entry in sys.path:
        if entry:
            ordered.append(Path(entry) / _PROVIDER_DLL)

    seen: set[str] = set()
    for path in ordered:
        norm = os.path.normpath(str(path))
        key = norm.lower()
        if key in seen:
            continue
        seen.add(key)
        if os.path.isfile(norm):
            log.info("Resolved RyzenAI DLL: %s", norm)
            return norm
    log.warning(
        "RyzenAI DLL %s NOT found; deps_dir=%r checked=%s",
        _PROVIDER_DLL, deps_dir, [os.path.normpath(str(p)) for p in ordered],
    )
    return None


def _prepare_ryzenai_provider_dll(provider_dll: str) -> Path:
    dll = Path(provider_dll).resolve()

    if not dll.is_file():
        raise FileNotFoundError(
            f"RyzenAI provider DLL not found: {dll}. Download Diffusers EP deps "
            f"({_PROVIDER_DLL} under deps/IHV/Diffusers/), set "
            "RYZENAI_CUSTOM_OPS_DLL, or set RYZEN_AI_INSTALLATION_PATH.",
        )

    if os.name == "nt" and hasattr(os, "add_dll_directory"):
        os.add_dll_directory(str(dll.parent))

    return dll


def register_ryzenai_ep(provider_dll: str) -> None:
    """Register the RyzenAI plugin-EP library once per process.

    No module-level 'registered' flag: registration is process-global in ORT
    and surfaced via get_ep_devices(). register_execution_provider_library is
    itself idempotent (raises 'already registered'), kept as a fallback.
    """
    is_registered = any(d.ep_name == _EP_NAME for d in onnxruntime.get_ep_devices())
    if is_registered:
        return
    try:
        onnxruntime.register_execution_provider_library(
            _EP_NAME,
            str(_prepare_ryzenai_provider_dll(provider_dll)),
        )
    except Exception as e:
        if "already registered" not in str(e).lower():
            raise


class AmdVendor(Vendor):
    display_name = "AMD RyzenAI"
    backend = "ryzenai"

    def validate(self, deps_dir: str | None = None) -> None:
        if os.name != "nt":
            raise RuntimeError(
                "AMD RyzenAI FLUX.2 Klein ONNX path is supported on Windows only."
            )
        available = onnxruntime.get_available_providers()
        if _EP_NAME not in available:
            provider_dll = resolve_ryzenai_provider_dll(deps_dir=deps_dir)
            if provider_dll:
                log.info(
                    "%s not listed yet; will register %s at model load",
                    _EP_NAME, provider_dll,
                )
            else:
                raise RuntimeError(
                    f"{_EP_NAME} is not available in ONNX Runtime "
                    f"(providers={available}). Download Diffusers EP deps "
                    f"(OrtGenAI-RyzenAI provides {_PROVIDER_DLL} under "
                    "deps/IHV/Diffusers/), set RYZENAI_CUSTOM_OPS_DLL, or set "
                    "RYZEN_AI_INSTALLATION_PATH."
                )

    def configure_globals(self) -> dict[str, Any]:
        log.info("AMD RyzenAI: using CPU torch tensors + ORT NPU EP")
        return {"ryzenai_ep": _EP_NAME}

    def resolve_device(self, requested: str | None) -> torch.device:
        return torch.device("cpu")

    def make_module_timer(self):
        from timing.timer import Timer
        return Timer()

    def make_generator(self, seed: int,
                       *, prefer_cpu: bool = False) -> torch.Generator:
        gen = torch.Generator(device="cpu")
        gen.manual_seed(int(seed))
        return gen

    def default_dtype(self) -> str:
        return "float32"

    def release_memory(self) -> None:
        import gc
        gc.collect()
