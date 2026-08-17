"""NVIDIA CUDA diffusers runtime."""
from __future__ import annotations

import logging
from contextlib import contextmanager, nullcontext
from typing import Any, Callable, Iterator

import torch

from helpers import (
    attach_diffusers_module_timing,
    diffusers_call_kwargs,
    strip_nonjson,
)
from registry import register_runtime
from runtime import OptimizationSpec, RunSpec, Runtime
from timing.timer import Timer
import models.flux2_klein as flux2_klein
import models.generic as generic
from diffusers.hooks import apply_group_offloading

log = logging.getLogger(__name__)


def _arch_from_capability(major: int, minor: int) -> str:
    """Map a CUDA compute capability to an architecture name."""
    if major >= 10:
        return "blackwell"  # sm_100 / sm_120
    if (major, minor) == (9, 0):
        return "hopper"
    if (major, minor) == (8, 9):
        return "ada"
    if major == 8:
        return "ampere"
    if major == 7:
        return "volta/turing"
    return f"sm_{major}{minor}"


@register_runtime(backend="cuda")
class NvidiaDiffusersRuntime(Runtime):
    backend = "cuda"
    family = "diffusers"
    graph_ready = False
    compute_stream = None
    stream_transformer = False
    # "" is the catch-all fallback (startswith("") always matches); the
    # dispatcher iterates in insertion order so flux_2_klein wins first.
    SUPPORTED_MODELS = {
        "flux_2_klein": flux2_klein.load,
        "": generic.load,
    }
    # Opts are opt-in: configs that want TRT / FP4 patching list them
    # explicitly. An implicit default would silently TRT-compile quantized
    # transformers and OOM/fail.
    DEFAULT_OPTS: list[OptimizationSpec] = []
    EXPECTED_SUBMODULES = {
        "flux_2_klein": ("text_encoder", "transformer", "vae"),
    }

    def load(self) -> None:
        super().load()
        if not self.compute_stream:
            self.compute_stream = torch.cuda.Stream()

        total_memory_gb = self.hardware.get("total_memory_gb", 0.0)
        if total_memory_gb and total_memory_gb < 16:
            self.stream_transformer = False
            log.info(
                "text-encoder group offloading enabled (%.1f GB VRAM < 16 GB)",
                total_memory_gb,
            )
            apply_group_offloading(
                self.pipe.text_encoder,
                onload_device=self.device,
                offload_device=torch.device("cpu"),
                offload_type="leaf_level",
                use_stream=True,
            )
        if total_memory_gb and total_memory_gb <= 8:
            self.stream_transformer = True
        else:
            log.info(
                "text-encoder group offloading disabled (%.1f GB VRAM >= 16 GB)",
                total_memory_gb,
            )

    def unload(self) -> None:
        self.compute_stream = None
        self.graph_ready = False
        super().unload()
    
    def query_hardware(self) -> None:
        if not torch.cuda.is_available():
            return
        idx = self.device.index if self.device.index is not None else 0
        props = torch.cuda.get_device_properties(idx)
        major, minor = torch.cuda.get_device_capability(idx)
        self.hardware = {
            "backend": "cuda",
            "device_name": props.name,
            "compute_capability": [major, minor],
            "sm": f"sm_{major}{minor}",
            "arch": _arch_from_capability(major, minor),
            "total_memory_gb": round(props.total_memory / 1024**3, 1),
        }
        log.info("hardware: %s", self.hardware)

    def _pin_pipe_execution_device(self) -> None:
        """Pin `pipe.device` / `pipe._execution_device` to `self.device`.

        Both diffusers properties infer "the" pipeline device by inspecting
        components; once the text encoder sits on CPU while transformer/VAE
        stay on GPU, that inspection reports cpu and the pipeline builds
        latents on the wrong device. This runtime only ever targets one
        fixed compute device, so pinning both properties is correct and
        sidesteps the inspection entirely.
        """
        if getattr(self, "_pipe_device_pinned", False):
            return
        device = self.device
        pipe_cls = type(self.pipe)
        pinned_cls = type(
            f"_{pipe_cls.__name__}WithPinnedDevice",
            (pipe_cls,),
            {
                "device": property(lambda _self, _d=device: _d),
                "_execution_device": property(lambda _self, _d=device: _d),
            },
        )
        self.pipe.__class__ = pinned_cls
        self._pipe_device_pinned = True

    def generate(
        self,
        run: RunSpec,
        *,
        generator: torch.Generator | None = None,
        step_callback: Callable[[int, int], None] | None = None,
        submodule_callback: Callable[[str, float], None] | None = None,
    ) -> Any:
        try:
            import torch_tensorrt  # noqa: F401
        except (ImportError, OSError) as exc:
            log.warning(
                "tensorrt_rtx: skipping (torch_tensorrt unavailable: %s)", exc
            )
            return {"skipped": True, "reason": f"torch_tensorrt unavailable: {exc}"}
        if self.pipe is None:
            raise RuntimeError("runtime not loaded — call .load() first")

        n = max(1, int(run.num_images_per_prompt))
        prefer_cpu = (self.gpu_mode or "").strip().lower() in (
            "cpu_offload", "sequential_offload")

        def _seed_for(idx: int) -> int:
            # One seed per image from the config's seeds array; wrap around if
            # fewer seeds than images, and fall back to the scalar seed when no
            # array was provided.
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

        self._pin_pipe_execution_device()

        # Generate num_images_per_prompt images with our own loop (one image
        # per pipeline call) so each image gets an independent seed/generator —
        # we deliberately do NOT use the diffusers num_images_per_prompt
        # batching. Text embeddings are computed once on the first image (full
        # pipeline: text encoder -> transformer -> VAE) and cached; subsequent
        # images pass the cached prompt_embeds so the pipeline skips the text
        # encoder and runs only transformer -> VAE.
        images: list[Any] = []
        prompt_embeds = None
        last_kwargs: dict[str, Any] = {}

        with torch.no_grad(), torch.cuda.stream(self.compute_stream):
            stream_ctx = (
                torch_tensorrt.runtime.weight_streaming(
                    self.pipe.transformer.compiled)
                if self.stream_transformer else nullcontext()
            )
            with stream_ctx as t_ctx:
                if self.stream_transformer and t_ctx is not None:
                    # Set the device budget to the automatically determined
                    # weight-streaming budget (once, for the whole loop).
                    t_ctx.device_budget = (
                        t_ctx.get_automatic_weight_streaming_budget())

                for idx in range(n):
                    seed_i = _seed_for(idx)
                    gen = (
                        self.vendor.make_generator(seed_i, prefer_cpu=prefer_cpu)
                        if seed_i is not None and seed_i >= 0 else None
                    )
                    kwargs = diffusers_call_kwargs(
                        run, pipe=self.pipe, generator=gen)
                    if prompt_embeds is None:
                        # First image: run the text encoder once and cache.
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
