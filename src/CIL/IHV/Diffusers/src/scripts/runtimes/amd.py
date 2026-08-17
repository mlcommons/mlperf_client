"""AMD RyzenAI ONNX diffusers runtime (FLUX.2 Klein)."""
from __future__ import annotations

from contextlib import contextmanager
from typing import Any, Callable, Iterator

from helpers import (
    attach_diffusers_module_timing,
    diffusers_call_kwargs,
    strip_nonjson,
)
from registry import register_runtime
from runtime import OptimizationSpec, RunSpec, Runtime
import models.flux2_klein as flux2_klein


def _amd_normalize_images(result: Any) -> list[Any]:
    """Extract image list from Flux2KleinPipeline output (pil / np / tuple)."""
    import numpy as np

    if isinstance(result, tuple):
        raw = result[0]
    else:
        raw = getattr(result, "images", None)
    if raw is None:
        return []
    # output_type="pil" yields a list of PIL images; keep tuple/ndarray
    # handling as a fallback (e.g. a (rgb, depth) tuple from other output types).
    if isinstance(raw, tuple) and raw:
        first = raw[0]
        if isinstance(first, np.ndarray):
            raw = first
    if isinstance(raw, np.ndarray):
        if raw.ndim == 4:
            return [raw[i] for i in range(raw.shape[0])]
        return [raw]
    if isinstance(raw, list):
        return raw
    return [raw]


@register_runtime(backend="ryzenai")
class AMDDiffusersRuntime(Runtime):
    backend = "ryzenai"
    family = "diffusers"
    SUPPORTED_MODELS = {
        "flux_2_klein": flux2_klein.load,
    }
    DEFAULT_OPTS: list[OptimizationSpec] = []
    EXPECTED_SUBMODULES = {
        "flux_2_klein": ("text_encoder", "transformer", "vae_decoder"),
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

        import sys

        n = max(1, int(run.num_images_per_prompt))

        def _seed_for(idx: int) -> int:
            # One seed per image from the config's seeds array; wrap around if
            # fewer seeds than images, and fall back to the scalar seed when no
            # array was provided (mirrors the NVIDIA runtime).
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

        # Generate num_images_per_prompt images with our own loop (one image per
        # pipeline call) so each image gets an independent seed/generator — the
        # AMD ORT/NPU subgraphs are exported at batch=1, so we deliberately do
        # NOT use diffusers num_images_per_prompt batching. Text embeddings are
        # computed once on the first image and cached; subsequent images pass the
        # cached prompt_embeds so the pipeline skips the text encoder and runs
        # only transformer -> VAE.
        images: list[Any] = []
        prompt_embeds = None
        last_kwargs: dict[str, Any] = {}

        for idx in range(n):
            seed_i = _seed_for(idx)
            gen = (
                self.vendor.make_generator(seed_i, prefer_cpu=True)
                if seed_i is not None and seed_i >= 0 else None
            )
            kwargs = diffusers_call_kwargs(run, pipe=self.pipe, generator=gen)
            kwargs["guidance_scale"] = float(run.guidance_scale)
            # "pil" lets the pipeline's image_processor emit RGB uint8 images, so
            # the shared np.array(images[0]) path works without AMD-specific
            kwargs["output_type"] = "pil"
            # GenAI-SD ONNX export uses 256-token text embeddings for the transformer.
            kwargs.setdefault("max_sequence_length", 256)

            if prompt_embeds is None:
                # First image: run the text encoder once and cache. The AMD
                # pipeline's __call__ patch only casts prompt_embeds to the
                # transformer's encoder_hidden_states dtype when encode_prompt is
                # invoked internally; since we precompute embeds here (skipping
                # that path) we cast them ourselves.
                prompt_embeds, _text_ids = self.pipe.encode_prompt(
                    prompt=run.prompt, device=self.device,
                    max_sequence_length=kwargs["max_sequence_length"],
                )
                enc_dtype = self._encoder_hidden_states_dtype()
                if enc_dtype is not None:
                    prompt_embeds = prompt_embeds.to(dtype=enc_dtype)
            if prompt_embeds is not None:
                kwargs.pop("prompt", None)
                kwargs["prompt_embeds"] = prompt_embeds
            if _step_cb is not None:
                kwargs["callback_on_step_end"] = _step_cb

            print(
                f"[mlperf-amd] runtime.generate: calling pipeline "
                f"(image {idx + 1}/{n}, seed={seed_i})",
                flush=True,
            )
            try:
                result = self.pipe(**kwargs)
            except BaseException as exc:
                print(
                    f"[mlperf-amd] runtime.generate failed (image {idx + 1}/{n}): "
                    f"{type(exc).__name__}: {exc}",
                    file=sys.stderr,
                    flush=True,
                )
                raise
            print(
                f"[mlperf-amd] runtime.generate: pipeline returned "
                f"(image {idx + 1}/{n})",
                flush=True,
            )
            images.extend(_amd_normalize_images(result))
            last_kwargs = kwargs

        return {"images": images, "call_kwargs": strip_nonjson(last_kwargs)}

    def _encoder_hidden_states_dtype(self):
        """Torch dtype of the transformer's encoder_hidden_states ORT input.

        Mirrors the cast in flux2_klein_amd._patch_flux2_klein_pipeline so
        precomputed/cached prompt_embeds match the transformer's expected dtype.
        """
        from onnxruntime.transformers.io_binding_helper import TypeHelper

        transformer = getattr(self.pipe, "transformer", None)
        model = getattr(transformer, "model", None)
        if model is None:
            return None
        onnx_type = next(
            (inp.type for inp in model.get_inputs()
             if inp.name == "encoder_hidden_states"),
            "tensor(float)",
        )
        return TypeHelper.ort_type_to_torch_type(onnx_type)

    @contextmanager
    def instrument(self, module_timer) -> Iterator[dict[str, list[float]]]:
        if self.pipe is None:
            yield {}
            return
        with attach_diffusers_module_timing(self.pipe, module_timer) as captured:
            yield captured
