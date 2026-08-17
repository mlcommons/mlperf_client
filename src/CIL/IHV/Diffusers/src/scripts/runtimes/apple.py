"""Apple MPS diffusers runtime."""
from __future__ import annotations

from contextlib import contextmanager
from typing import Any, Callable, Iterator

import torch

from helpers import (
    attach_diffusers_module_timing,
    diffusers_call_kwargs,
    is_frozen_or_embedded,
    strip_nonjson,
)
from registry import register_runtime
from runtime import OptimizationSpec, RunSpec, Runtime
from timing.timer import Timer
import models.flux2_klein as flux2_klein
import models.generic as generic


# Inductor on MPS needs PyTorch's Metal headers + subprocess workers via
# sys.executable, both of which break in a frozen/embedded build. Skip
# mps_compile by default there. mps_memory_tier always runs to pick the
# slicing/offload recipe for the host's unified RAM.
_BASE_OPTS: list[OptimizationSpec] = [
    OptimizationSpec(type="mps_memory_tier", params={}),
]
if not is_frozen_or_embedded():
    _BASE_OPTS.append(OptimizationSpec(
        type="mps_compile",
        params={"targets": ["transformer"], "mode": "default",
                "fullgraph": False, "dynamic": False},
    ))


@register_runtime(backend="mps")
class AppleDiffusersRuntime(Runtime):
    backend = "mps"
    family = "diffusers"
    SUPPORTED_MODELS = {
        "flux_2_klein": flux2_klein.load,
        "": generic.load,
    }
    DEFAULT_OPTS = _BASE_OPTS
    EXPECTED_SUBMODULES = {
        "flux_2_klein": ("text_encoder", "transformer", "vae"),
    }

    def generate(
        self,
        run: RunSpec,
        *,
        generator: torch.Generator | None = None,
        step_callback: Callable[[int, int], None] | None = None,
        submodule_callback: Callable[[str, float], None] | None = None,
    ) -> Any:
        if self.pipe is None:
            raise RuntimeError("runtime not loaded — call .load() first")

        n = max(1, int(run.num_images_per_prompt))

        offloading = (self.gpu_mode or "").strip().lower() in (
            "cpu_offload", "sequential_offload")

        def _seed_for(idx: int) -> int:
            if run.seeds:
                return (run.seeds[idx] if idx < len(run.seeds)
                        else run.seeds[idx % len(run.seeds)])
            return run.seed

        if step_callback is not None:
            def _step_cb(_pipe, step_index, _t, callback_kwargs):
                step_callback(step_index + 1, run.steps)
                return callback_kwargs
        else:
            _step_cb = None

        images: list[Any] = []
        prompt_embeds = None
        last_kwargs: dict[str, Any] = {}

        with torch.no_grad():
            for idx in range(n):
                seed_i = _seed_for(idx)
                gen = (
                    self.vendor.make_generator(seed_i)
                    if seed_i is not None and seed_i >= 0 else None
                )
                kwargs = diffusers_call_kwargs(run, pipe=self.pipe, generator=gen)
                if not offloading:
                    if prompt_embeds is None:
                        prompt_embeds, _text_ids = self.pipe.encode_prompt(
                            prompt=run.prompt, device=self.device)
                    if prompt_embeds is not None:
                        kwargs.pop("prompt", None)
                        kwargs["prompt_embeds"] = prompt_embeds
                if _step_cb is not None:
                    kwargs["callback_on_step_end"] = _step_cb

                result = self.pipe(**kwargs)
                imgs = getattr(result, "images", None) or []
                if imgs:
                    images.append(imgs[0])
                last_kwargs = kwargs

        return {"images": images, "call_kwargs": strip_nonjson(last_kwargs)}

    @contextmanager
    def instrument(
        self, module_timer: Timer,
    ) -> Iterator[dict[str, list[float]]]:
        if self.pipe is None:
            yield {}
            return
        with attach_diffusers_module_timing(self.pipe, module_timer) as captured:
            yield captured
