"""Apply an Apple MPS memory-tier recipe to a loaded diffusers pipeline.

Picks `ultra/max/pro/base` based on unified RAM (or an explicit override
in params) and enables CPU offload + attention/VAE slicing/tiling
accordingly. Thresholds are tuned for FLUX.2-klein-4B; other models can
override via params.
"""
from __future__ import annotations

import logging
from dataclasses import dataclass
from typing import Any, TYPE_CHECKING

from vendors.apple import total_unified_memory_gb
from registry import register_opt

if TYPE_CHECKING:
    from runtime import Runtime

log = logging.getLogger(__name__)


@dataclass(frozen=True)
class _Tier:
    name: str
    cpu_offload: bool
    attention_slicing: bool
    vae_slicing: bool
    vae_tiling: bool


def resolve_tier(ram_gb: float) -> _Tier:
    if ram_gb >= 64:
        return _Tier("ultra", False, False, False, False)
    if ram_gb >= 32:
        return _Tier("max", False, True, False, False)
    if ram_gb >= 16:
        return _Tier("pro", True, True, True, True)
    return _Tier("base", True, True, True, True)


_KNOB_PROBES: dict[str, tuple[tuple[str, str | None], ...]] = {
    "attention_slicing": (("enable_attention_slicing", None),),
    "vae_slicing":       (("enable_vae_slicing", None), ("vae", "enable_slicing")),
    "vae_tiling":        (("enable_vae_tiling", None),  ("vae", "enable_tiling")),
}


def _resolve_knob(pipe: Any, key: str):
    for outer, inner in _KNOB_PROBES[key]:
        target = pipe if inner is None else getattr(pipe, outer, None)
        attr = outer if inner is None else inner
        fn = getattr(target, attr, None) if target is not None else None
        if fn is not None:
            return fn
    return None


@register_opt("mps_memory_tier")
def apply(runtime: "Runtime", params: dict[str, Any]) -> dict[str, Any]:
    if runtime.pipe is None:
        raise RuntimeError("mps_memory_tier: runtime.pipe is None — load first")
    if runtime.device.type != "mps":
        log.info("mps_memory_tier: device is %s, not mps — skipping.", runtime.device)
        return {"applied": False, "reason": "not_mps"}

    ram_gb = total_unified_memory_gb()
    tier_name = params.get("tier")
    if tier_name == "auto" or tier_name is None:
        tier = resolve_tier(ram_gb)
    else:
        forced = {
            "ultra": _Tier("ultra", False, False, False, False),
            "max":   _Tier("max",   False, True,  False, False),
            "pro":   _Tier("pro",   True,  True,  True,  True),
            "base":  _Tier("base",  True,  True,  True,  True),
        }
        if tier_name not in forced:
            raise ValueError(
                f"unknown tier {tier_name!r}; choose ultra/max/pro/base/auto"
            )
        tier = forced[tier_name]

    cpu_offload = bool(params.get("cpu_offload", tier.cpu_offload))
    attention_slicing = bool(params.get(
        "attention_slicing", tier.attention_slicing))
    vae_slicing = bool(params.get("vae_slicing", tier.vae_slicing))
    vae_tiling = bool(params.get("vae_tiling", tier.vae_tiling))

    if tier.name == "base":
        log.warning(
            "Detected %.1f GB of unified RAM — large models at 1024² may "
            "struggle to fit; runs are best-effort.", ram_gb,
        )

    if cpu_offload:
        fn = getattr(runtime.pipe, "enable_model_cpu_offload", None)
        if fn is not None:
            try:
                fn(device="mps")
            except TypeError:
                fn()

    for flag, key in (
        (attention_slicing, "attention_slicing"),
        (vae_slicing, "vae_slicing"),
        (vae_tiling, "vae_tiling"),
    ):
        if not flag:
            continue
        fn = _resolve_knob(runtime.pipe, key)
        if fn is not None:
            fn()

    log.info(
        "MPS memory tier=%s cpu_offload=%s attn_slicing=%s "
        "vae_slicing=%s vae_tiling=%s (ram=%.1f GB)",
        tier.name, cpu_offload, attention_slicing,
        vae_slicing, vae_tiling, ram_gb,
    )

    return {
        "tier": tier.name,
        "ram_gb": round(ram_gb, 1),
        "cpu_offload": cpu_offload,
        "attention_slicing": attention_slicing,
        "vae_slicing": vae_slicing,
        "vae_tiling": vae_tiling,
    }
