"""FLUX.2-klein-4B loader — dispatches by model layout and backend.

  - backend ryzenai               -> flux2_klein_amd.load_amd_onnx
  - quanto_qmap.json present      -> _load_quanto   (pre-quantized weights
                                                     reloaded via
                                                     optimum.quanto.requantize)
  - runtime.quantization=="fp8"   -> _load_fp8      (ModelOpt FP8, pairs with
                                                     the tensorrt_rtx opt)
  - runtime.quantization=="nvfp4" -> _load_nvfp4    (ModelOpt NVFP4, pairs with
                                                     the tensorrt_rtx opt)
  - default (CUDA/MPS)            -> _load_standard (Flux2KleinPipeline)

On the FP8 / NVFP4 paths, setting `components.vae` additionally swaps the
pipeline's decoder for the FLUX.2 small-decoder VAE (AutoencoderKLFlux2) via
`_load_small_decoder`.
"""
from __future__ import annotations

import json
import logging
import os
from typing import Any, TYPE_CHECKING

import torch

from helpers import (
    apply_gpu_mode,
    hf_kwargs,
    load_diffusers_pipeline,
    resolve_model_ref,
)

if TYPE_CHECKING:
    from runtime import Runtime

log = logging.getLogger(__name__)


DEFAULT_MODEL_ID = "black-forest-labs/FLUX.2-klein-4B"

# Quantized transformer refs used when the EP config doesn't override the path.
_NVFP4_TRANSFORMER_REF: dict[str, str] = {
    "repo": "black-forest-labs/FLUX.2-klein-4b-nvfp4",
    "filename": "flux-2-klein-4b-nvfp4.safetensors",
}
_FP8_TRANSFORMER_REF: dict[str, str] = {
    "repo": "black-forest-labs/FLUX.2-klein-4b-fp8",
    "filename": "flux-2-klein-4b-fp8.safetensors",
}

# Small-decoder VAE (AutoencoderKLFlux2), swapped in when the EP config sets
# components.vae. diffusion_pytorch_model.safetensors is the full (unchanged)
# encoder + distilled small decoder; config.json carries the narrower channels.
_SMALL_DECODER_REF: dict[str, str] = {
    "repo": "black-forest-labs/FLUX.2-small-decoder",
    "filename": "diffusion_pytorch_model.safetensors",
}

# Minimum CUDA compute capability per quantized feature.
_NVFP4_MIN_CC = (10, 0)   # sm_100 Blackwell
_FP8_MIN_CC = (8, 9)      # sm_89 Ada


def _require_sm(runtime: "Runtime", min_cc: tuple[int, int],
                feature: str, remedy: str = "") -> None:
    """Raise if the runtime's GPU is below `min_cc` (compute capability)."""
    hw = getattr(runtime, "hardware", {}) or {}
    cc = tuple(hw.get("compute_capability") or ())
    if hw.get("backend") == "cuda" and cc >= min_cc:
        return
    min_sm = f"sm_{min_cc[0]}{min_cc[1]}"
    name = hw.get("device_name", "this device")
    sm = hw.get("sm", "unknown")
    raise RuntimeError(
        f"{feature} requires an NVIDIA GPU with {min_sm}+; detected "
        f"{name} ({sm}). {remedy}".rstrip()
    )


def load(runtime: "Runtime") -> Any:
    """Top-level dispatcher. Picks a load path from the model dir + quantization."""
    model_dir = runtime.model_path
    if getattr(runtime.vendor, "backend", "") == "ryzenai":
        from models.flux2_klein_amd import load_amd_onnx
        return load_amd_onnx(runtime)
    if model_dir and os.path.isdir(model_dir) and _has_quanto_qmap(model_dir):
        return _load_quanto(runtime, model_dir)
    quant = (runtime.quantization or "").strip().lower()
    transformer_override = (
        runtime.component_files.get("transformer") or ""
    ).strip()
    # FP8 (Ada+) is checked first: its transformer override is also a
    # .safetensors, which would otherwise fall into the NVFP4 path. Loaded via
    # ModelOpt and paired with the tensorrt_rtx opt.
    if quant == "fp8":
        _require_sm(runtime, _FP8_MIN_CC, "FP8 (quantization=fp8)")
        return _load_fp8(runtime)
    # NVFP4 trigger: a transformer .safetensors override OR an explicit nvfp4
    # knob. Loaded via ModelOpt (pairs with tensorrt_rtx). NVFP4 needs
    # Blackwell; otherwise prompt FP8.
    if (transformer_override.endswith((".safetensors", ".ckpt"))
            or quant == "nvfp4"):
        _require_sm(runtime, _NVFP4_MIN_CC,
                    "NVFP4 (quantization=nvfp4)",
                    remedy="Run the FP8 model instead: quantization='fp8' "
                           "(NVIDIA_Diffusers_GPU_FP8.json).")
        return _load_nvfp4(runtime)
    return _load_standard(runtime)


def _has_quanto_qmap(model_dir: str) -> bool:
    return (
        os.path.exists(os.path.join(model_dir, "quanto_qmap.json"))
        or os.path.exists(os.path.join(model_dir, "transformer",
                                       "quanto_qmap.json"))
    )


def _load_standard(runtime: "Runtime") -> Any:
    from diffusers import Flux2KleinPipeline

    ref, auto_download, cache_dir = resolve_model_ref(
        runtime.model_path,
        default_model_id=DEFAULT_MODEL_ID,
        download_cache=runtime.download_cache,
    )
    runtime.model_ref = ref
    runtime.auto_download = auto_download

    pipe = load_diffusers_pipeline(
        Flux2KleinPipeline, ref,
        dtype=runtime.dtype,
        auto_download=auto_download,
        cache_dir=cache_dir,
        device=None,
    )
    apply_gpu_mode(pipe, device=runtime.device, gpu_mode=runtime.gpu_mode,
                   device_id=_device_index(runtime.device))

    if runtime.dtype == torch.float16:
        log.warning(
            "FLUX in float16 is fragile (transformer can overflow). "
            "bfloat16 is the recommended dtype."
        )
    return pipe


def _resolve_transformer_override(
    runtime: "Runtime", *, default_ref: dict[str, str], label: str,
) -> tuple[str, dict, dict, str]:
    """Resolve (pipeline_dir, base_kwargs, cache_only, transformer_path).

    `default_ref` is the HF {repo, filename} fallback used when no override is
    set and auto_download is on (standalone --download). `label` names the
    quantization in the offline error.
    """
    from huggingface_hub import hf_hub_download

    ref, auto_download, cache_dir = resolve_model_ref(
        runtime.model_path,
        default_model_id=DEFAULT_MODEL_ID,
        download_cache=runtime.download_cache,
    )
    runtime.model_ref = ref
    runtime.auto_download = auto_download

    pipeline_dir = ref or DEFAULT_MODEL_ID
    base_kwargs = hf_kwargs(
        dtype=runtime.dtype, auto_download=auto_download, cache_dir=cache_dir,
    )
    cache_only = {k: base_kwargs[k]
                  for k in ("cache_dir", "local_files_only")
                  if k in base_kwargs}

    transformer_path = (
        runtime.component_files.get("transformer", "")
    ).strip()
    # Resolve relative overrides against the unpacked model dir. The harness
    # ships the quantized .zip whose top-level filename is what the EP config
    # names; keep absolute paths as-is for local dev usage.
    if (transformer_path
            and not os.path.isabs(transformer_path)
            and pipeline_dir
            and os.path.isdir(pipeline_dir)):
        candidate = os.path.join(pipeline_dir, transformer_path)
        if os.path.exists(candidate):
            transformer_path = candidate
    if not transformer_path:
        # Client / harness path runs with auto_download=False (no network at
        # inference). Refuse the HF fallback there with a clear pointer to the
        # EP-config knob. Standalone runner with --download keeps
        # auto_download=True and the fallback fires.
        if not auto_download:
            raise RuntimeError(
                f"{label} transformer override not set in EP config "
                "(Config.components.transformer) and auto_download is "
                "disabled. Production / harness mode runs offline — the "
                f"{label} .safetensors must be packaged in the model bundle "
                "and referenced via components.transformer (relative to "
                "the unpacked model dir). HF fallback is dev-only "
                "(standalone --download).")
        transformer_path = hf_hub_download(
            repo_id=default_ref["repo"],
            filename=default_ref["filename"],
            cache_dir=cache_only.get("cache_dir"),
            local_files_only=bool(cache_only.get("local_files_only", False)),
        )
    return pipeline_dir, base_kwargs, cache_only, transformer_path


def _load_small_decoder(
    runtime: "Runtime", *, pipeline_dir: str, cache_only: dict,
) -> Any:
    """Load the FLUX.2 small-decoder VAE (AutoencoderKLFlux2), or None.

    Opt-in via components.vae (mirrors the components.transformer override):
    a path relative to the unpacked bundle, an absolute path, or — when it
    does not resolve locally and auto_download is on — the HF small-decoder
    repo. The small decoder ships its own (narrower) config, so it is loaded
    from that source's config.json, not the base pipeline's vae config.
    Returns None when components.vae is unset (no swap).
    """
    import sys

    from diffusers.models import AutoencoderKLFlux2

    override = (runtime.component_files.get("vae") or "").strip()
    if not override:
        return None

    # Resolve relative overrides against the unpacked model dir (mirrors the
    # transformer override handling); keep absolute paths as-is.
    source = override
    if (not os.path.isabs(source)
            and pipeline_dir
            and os.path.isdir(pipeline_dir)):
        candidate = os.path.join(pipeline_dir, source)
        if os.path.exists(candidate):
            source = candidate

    local_only = bool(cache_only.get("local_files_only", False))
    cache_dir = cache_only.get("cache_dir")

    if os.path.exists(source):
        # The bundle ships the small decoder as a diffusers folder (config.json
        # + diffusion_pytorch_model.safetensors); accept a direct file path too
        # and load from its containing directory.
        load_dir = source if os.path.isdir(source) else os.path.dirname(source)
        vae = AutoencoderKLFlux2.from_pretrained(
            load_dir, torch_dtype=runtime.dtype, local_files_only=True,
        )
    elif local_only:
        raise RuntimeError(
            "small decoder override (components.vae) not found locally and "
            "auto_download is disabled. Package the FLUX.2-small-decoder VAE "
            "(config.json + diffusion_pytorch_model.safetensors) in the model "
            "bundle and reference it via components.vae.")
    else:
        vae = AutoencoderKLFlux2.from_pretrained(
            _SMALL_DECODER_REF["repo"],
            cache_dir=cache_dir,
            torch_dtype=runtime.dtype,
            local_files_only=False,
        )
    vae.encoder = None

    print(f"[small-decoder] vae <- {source}", file=sys.stderr, flush=True)
    return vae


def _load_fp8(runtime: "Runtime") -> Any:
    import sys

    from diffusers import Flux2KleinPipeline

    from opts.nvidia_quantize import (
        disable_uncalibrated_activation_quantizers,
        load_fp8_transformer,
    )

    pipeline_dir, base_kwargs, cache_only, transformer_path = (
        _resolve_transformer_override(
            runtime, default_ref=_FP8_TRANSFORMER_REF, label="FP8")
    )

    print(f"[fp8] modelopt: transformer <- {transformer_path}",
          file=sys.stderr, flush=True)
    # Activations are quantized so the tensorrt_rtx opt can lower them to a
    # native TensorRT FP8 Q/DQ GEMM.
    transformer = load_fp8_transformer(
        transformer_path,
        base_model=pipeline_dir,
        cache_dir=cache_only.get("cache_dir"),
        local_files_only=bool(cache_only.get("local_files_only", False)),
        dtype=runtime.dtype,
        quantize_activations=True,
    )

    vae = _load_small_decoder(
        runtime, pipeline_dir=pipeline_dir, cache_only=cache_only)
    extra = {"vae": vae} if vae is not None else {}
    pipe = Flux2KleinPipeline.from_pretrained(
        pipeline_dir, transformer=transformer, **extra, **base_kwargs,
    )
    apply_gpu_mode(pipe, device=runtime.device, gpu_mode=runtime.gpu_mode,
                   device_id=_device_index(runtime.device))
    info = disable_uncalibrated_activation_quantizers(pipe.transformer)
    print(f"[fp8] disabled {info.get('quantizers_disabled')} "
          "uncalibrated activation quantizers", file=sys.stderr, flush=True)
    return pipe


def _load_nvfp4(runtime: "Runtime") -> Any:
    import sys

    from diffusers import Flux2KleinPipeline

    from opts.nvidia_quantize import (
        disable_uncalibrated_activation_quantizers,
        load_nvfp4_transformer,
    )

    pipeline_dir, base_kwargs, cache_only, transformer_path = (
        _resolve_transformer_override(
            runtime, default_ref=_NVFP4_TRANSFORMER_REF, label="NVFP4")
    )

    print(f"[nvfp4] modelopt: transformer <- {transformer_path}",
          file=sys.stderr, flush=True)
    # Activations are quantized so the tensorrt_rtx opt can lower them
    # to a native TensorRT dynamic-quantize layer.
    transformer = load_nvfp4_transformer(
        transformer_path,
        base_model=pipeline_dir,
        cache_dir=cache_only.get("cache_dir"),
        local_files_only=bool(cache_only.get("local_files_only", False)),
        dtype=runtime.dtype,
        quantize_activations=True,
    )

    vae = _load_small_decoder(
        runtime, pipeline_dir=pipeline_dir, cache_only=cache_only)
    extra = {"vae": vae} if vae is not None else {}
    pipe = Flux2KleinPipeline.from_pretrained(
        pipeline_dir, transformer=transformer, **extra, **base_kwargs,
    )
    apply_gpu_mode(pipe, device=runtime.device, gpu_mode=runtime.gpu_mode,
                   device_id=_device_index(runtime.device))
    info = disable_uncalibrated_activation_quantizers(pipe.transformer)
    print(f"[nvfp4] disabled {info.get('quantizers_disabled')} "
          "uncalibrated activation quantizers", file=sys.stderr, flush=True)
    return pipe


def _load_quanto(runtime: "Runtime", model_dir: str) -> Any:
    """Load Flux 2 Klein with quanto_qmap.json pre-quantized weights."""
    import json

    from accelerate import init_empty_weights
    from diffusers import FlowMatchEulerDiscreteScheduler, Flux2KleinPipeline
    from diffusers.models import AutoencoderKLFlux2
    from diffusers.models.transformers.transformer_flux2 import (
        Flux2Transformer2DModel,
    )
    from optimum.quanto import requantize
    from transformers import AutoTokenizer, Qwen3ForCausalLM

    runtime.model_ref = model_dir
    runtime.auto_download = False

    transformer = _load_quanto_component(
        component_dir=_pick_subdir(model_dir, "transformer"),
        model_cls=Flux2Transformer2DModel,
        dtype=runtime.dtype,
    )

    # MPS SDPA needs matching q/k/v dtypes; eager attention tolerates the
    # mixed-precision left over after quanto's int8 dequant. CUDA SDPA copes.
    te_kwargs: dict[str, Any] = {}
    if runtime.device.type == "mps":
        te_kwargs["attn_implementation"] = "eager"

    text_encoder_dir = _pick_subdir(model_dir, "text_encoder")
    if os.path.exists(os.path.join(text_encoder_dir, "quanto_qmap.json")):
        text_encoder = _load_quanto_component(
            component_dir=text_encoder_dir,
            model_cls=Qwen3ForCausalLM,
            dtype=runtime.dtype,
            **te_kwargs,
        )
    else:
        text_encoder = Qwen3ForCausalLM.from_pretrained(
            text_encoder_dir, torch_dtype=runtime.dtype,
            local_files_only=True, **te_kwargs,
        )

    tokenizer = AutoTokenizer.from_pretrained(
        _pick_subdir(model_dir, "tokenizer"), local_files_only=True,
    )
    vae = AutoencoderKLFlux2.from_pretrained(
        _pick_subdir(model_dir, "vae"),
        torch_dtype=runtime.dtype,
        local_files_only=True,
    )

    scheduler_dir = _pick_subdir(model_dir, "scheduler")
    scheduler_config_path = os.path.join(scheduler_dir, "scheduler_config.json")
    if os.path.exists(scheduler_config_path):
        with open(scheduler_config_path) as f:
            scheduler_config = json.load(f)
        scheduler = FlowMatchEulerDiscreteScheduler.from_config(scheduler_config)
    else:
        scheduler = FlowMatchEulerDiscreteScheduler()

    pipe = Flux2KleinPipeline(
        transformer=transformer,
        text_encoder=text_encoder,
        tokenizer=tokenizer,
        vae=vae,
        scheduler=scheduler,
    )
    if hasattr(pipe, "set_progress_bar_config"):
        pipe.set_progress_bar_config(disable=True)
    apply_gpu_mode(pipe, device=runtime.device, gpu_mode=runtime.gpu_mode,
                   device_id=_device_index(runtime.device))
    return pipe


def _load_quanto_component(*, component_dir: str, model_cls: Any,
                           dtype: torch.dtype, **from_pretrained_kwargs) -> Any:
    """Load a single quanto-pre-quantized component.

    Build an empty model from the config (NOT from_pretrained — that would
    try to load the quanto-shaped safetensors into the vanilla schema and
    spew MISSING/UNEXPECTED warnings), then let `optimum.quanto.requantize`
    wrap the linears and populate them with the quantized state dict. Cast
    non-quanto floating tensors to `dtype` afterwards because quanto's
    WeightQBytes refuses `.to(dtype=...)`.
    """
    import json

    from accelerate import init_empty_weights
    from diffusers.models.model_loading_utils import load_state_dict
    from diffusers.utils import SAFE_WEIGHTS_INDEX_NAME
    from optimum.quanto import requantize

    with open(os.path.join(component_dir, "quanto_qmap.json")) as f:
        qmap = json.load(f)

    model = _build_empty_for_quanto(
        model_cls=model_cls, component_dir=component_dir,
        dtype=dtype, **from_pretrained_kwargs,
    )

    index_file = os.path.join(component_dir, SAFE_WEIGHTS_INDEX_NAME)
    if os.path.exists(index_file):
        from diffusers.utils.hub_utils import _get_checkpoint_shard_files
        from optimum.quanto.models.shared_dict import ShardedStateDict

        _, sharded_metadata = _get_checkpoint_shard_files(
            component_dir, index_file,
        )
        state_dict = ShardedStateDict(component_dir,
                                      sharded_metadata["weight_map"])
    else:
        single_file = _find_single_safetensors(component_dir)
        if single_file is None:
            raise FileNotFoundError(
                f"no safetensors weight file found under {component_dir!r}"
            )
        state_dict = load_state_dict(single_file)

    requantize(model, state_dict, qmap)
    model.eval()
    _cast_non_quanto_to_dtype(model, dtype)
    return model


def _build_empty_for_quanto(*, model_cls: Any, component_dir: str,
                            dtype: torch.dtype, **from_pretrained_kwargs) -> Any:
    """Empty-weight ctor that avoids loading the quanto-shaped safetensors.

    - Diffusers models expose `load_config` + `from_config` — preferred.
    - Transformers models accept `__init__(config)` — load AutoConfig and
      apply the optional attn_implementation hint via `_attn_implementation`
      so `from_pretrained_kwargs={"attn_implementation": "eager"}` survives.
    """
    from accelerate import init_empty_weights

    attn_impl = from_pretrained_kwargs.pop("attn_implementation", None)

    if hasattr(model_cls, "load_config") and hasattr(model_cls, "from_config"):
        config = model_cls.load_config(component_dir)
        with init_empty_weights():
            return model_cls.from_config(config)

    from transformers import AutoConfig
    config = AutoConfig.from_pretrained(
        component_dir, local_files_only=True, **from_pretrained_kwargs,
    )
    if attn_impl is not None:
        config._attn_implementation = attn_impl
    with init_empty_weights():
        model = model_cls(config)
    return model


def _cast_non_quanto_to_dtype(model: Any, dtype: torch.dtype) -> None:
    """Fix attention dtype mismatch left over from quanto requantize.

    optimum.quanto's WeightQBytes refuses `.to(dtype=...)`; iterate
    individually and skip any tensor whose class lives in `optimum.quanto`.
    """
    for _, param in model.named_parameters(recurse=True):
        if (param.is_floating_point()
                and "quanto" not in type(param.data).__module__):
            param.data = param.data.to(dtype=dtype)
    for _, buf in model.named_buffers(recurse=True):
        if (buf.is_floating_point()
                and "quanto" not in type(buf).__module__):
            buf.data = buf.data.to(dtype=dtype)


def _pick_subdir(model_dir: str, name: str) -> str:
    sub = os.path.join(model_dir, name)
    return sub if os.path.isdir(sub) else model_dir


def _device_index(device: torch.device) -> int:
    return device.index if device.index is not None else 0


def _find_single_safetensors(component_dir: str) -> str | None:
    for fname in sorted(os.listdir(component_dir)):
        if fname.endswith(".safetensors"):
            return os.path.join(component_dir, fname)
    return None
