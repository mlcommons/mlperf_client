"""torch.compile pass for the Apple MPS path.

Inductor on MPS needs PyTorch's Metal headers and subprocess compile
workers via sys.executable, both of which break in a frozen/embedded
build. The opt no-ops there — config that lists it still records a
`setup.opt.mps_compile` scope with `skipped: frozen`.
"""
from __future__ import annotations

import logging
from typing import Any, TYPE_CHECKING

import torch

from helpers import is_frozen_or_embedded
from vendors.apple import mps_available
from registry import register_opt

if TYPE_CHECKING:
    from runtime import Runtime

log = logging.getLogger(__name__)


@register_opt("mps_compile")
def apply(runtime: "Runtime", params: dict[str, Any]) -> dict[str, Any]:
    if is_frozen_or_embedded():
        log.info(
            "mps_compile: skipping under a frozen/embedded build "
            "(Inductor on MPS needs Metal headers + subprocess workers)."
        )
        return {"compiled": [], "skipped": "frozen"}

    if not mps_available():
        log.warning(
            "mps_compile: MPS isn't available on this host — skipping. "
            "Remove the opt from the config to silence this warning."
        )
        return {"compiled": [], "reason": "mps_unavailable"}

    if runtime.pipe is None:
        raise RuntimeError("mps_compile: runtime.pipe is None — load first")

    targets = params.get("targets") or ["transformer", "unet"]
    mode = params.get("mode", "default")
    if mode == "reduce-overhead":
        log.info("mps_compile: 'reduce-overhead' is CUDA-only — using 'default'")
        mode = "default"
    fullgraph = bool(params.get("fullgraph", False))
    dynamic = bool(params.get("dynamic", False))

    compiled: list[str] = []
    for attr in targets:
        mod = getattr(runtime.pipe, attr, None)
        if mod is None:
            continue
        try:
            setattr(runtime.pipe, attr, torch.compile(
                mod.eval(), mode=mode, fullgraph=fullgraph, dynamic=dynamic,
            ))
            compiled.append(attr)
        except Exception as exc:
            log.warning(
                "mps_compile: torch.compile failed for %r (%s) — falling back to eager.",
                attr, exc,
            )

    return {
        "compiled": compiled,
        "mode": mode,
        "fullgraph": fullgraph,
        "dynamic": dynamic,
    }
