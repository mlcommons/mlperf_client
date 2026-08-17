"""FLUX.2-klein-4B AMD RyzenAI ONNX loader (ORT + Flux2KleinPipeline).

Used when runtime.vendor.backend == \"ryzenai\". NVIDIA/Apple HF paths stay in
flux2_klein.py.

ORT session style (onnxruntime.InferenceSession / SessionOptions):
  - NPU subgraphs: configure EPs on SessionOptions (add_provider_for_devices,
    add_provider), then InferenceSession(..., providers=None) so ORT does not
    override SessionOptions (see onnxruntime_inference_collection.py warning).
  - text_encoder: Diffusers OnnxRuntimeModel.from_pretrained(..., provider=...)
    passes providers=[...] without pre-configured SessionOptions.

RyzenAI provider DLL:
  Harness EP deps (Diffusers -> OrtGenAI-RyzenAI) place
  ``onnxruntime_providers_ryzenai.dll`` under the Diffusers deps dir; Python
  resolves it via ``__deps_dir__`` / ``sys.path``. Override with env
  ``RYZENAI_CUSTOM_OPS_DLL`` only.
"""
from __future__ import annotations

import functools
import json
import logging
import os
import types
from contextlib import contextmanager
from pathlib import Path
from typing import Any, Optional, TYPE_CHECKING

import torch
import ctypes
import onnxruntime
from onnxruntime.transformers.io_binding_helper import TypeHelper

from helpers import is_hf_id, resolve_model_ref
from vendors.amd import (
    register_ryzenai_ep,
    resolve_ryzenai_provider_dll,
    _EP_NAME,
    _PROVIDER_DLL as RYZENAI_PROVIDER_DLL,
)

# HF repo for the AMD RyzenAI ONNX export; mirrors flux2_klein.DEFAULT_MODEL_ID.
DEFAULT_AMD_MODEL_ID = "amd/FLUX.2-klein-4B-amdnpu"

if TYPE_CHECKING:
    from runtime import Runtime

log = logging.getLogger(__name__)

MLPERF_PIPELINE_KIND_AMD_ONNX = "flux2_klein_amd_onnx"


def _resolve_model_dir(runtime: "Runtime") -> str:
    """Local model dir, or snapshot_download when the ref is a HF repo id.

    Mirrors the NVIDIA path (helpers.resolve_model_ref): an empty model_path
    falls back to DEFAULT_AMD_MODEL_ID; a bare ``org/name[@revision]`` triggers
    huggingface_hub.snapshot_download into the runtime cache.
    """
    ref, auto_download, cache_dir = resolve_model_ref(
        runtime.model_path,
        default_model_id=DEFAULT_AMD_MODEL_ID,
        download_cache=runtime.download_cache,
    )
    runtime.model_ref = ref
    runtime.auto_download = auto_download

    if is_hf_id(ref):
        repo_id, _, revision = ref.partition("@")
        from huggingface_hub import snapshot_download
        model_dir = snapshot_download(
            repo_id,
            revision=revision or None,
            cache_dir=cache_dir,
            local_files_only=not auto_download,
        )
        runtime.model_ref = model_dir
        return model_dir

    model_dir = ref
    if model_dir.startswith("file://"):
        model_dir = model_dir[len("file://"):]
        if os.name == "nt" and len(model_dir) > 2 and model_dir[0] == "/" and model_dir[2] == ":":
            model_dir = model_dir[1:]
    model_dir = os.path.normpath(model_dir)
    # Configs may point FilePath at an index file in the model dir; use its parent.
    if os.path.basename(model_dir).endswith(".json"):
        model_dir = os.path.dirname(model_dir)
    runtime.model_ref = model_dir
    return model_dir


def _discover_file(directory: str, pattern: str) -> Optional[str]:
    """Sole basename matching glob `pattern` in `directory`; None if zero or many.

    Keeps the loader manifest-independent: when exactly one file matches we use
    it (robust to renames); ambiguous/empty defers to the hardcoded default.
    """
    import glob

    matches = [
        os.path.basename(p)
        for p in glob.glob(os.path.join(directory, pattern))
        if os.path.isfile(p)
    ]
    return matches[0] if len(matches) == 1 else None


def load_amd_onnx(runtime: "Runtime") -> Any:
    """Load Flux 2 Klein AMD ONNX export (ORT submodules + Flux2KleinPipeline)."""
    import torch.nn as nn
    from transformers import Qwen2TokenizerFast

    try:
        from diffusers import FlowMatchEulerDiscreteScheduler, Flux2KleinPipeline
    except ImportError:
        try:
            from diffusers.schedulers import FlowMatchEulerDiscreteScheduler
            from diffusers.pipelines.flux2.pipeline_flux2_klein import (
                Flux2KleinPipeline,
            )
        except ImportError as exc:
            import diffusers
            raise ImportError(
                "Flux2KleinPipeline requires diffusers>=0.38. "
                f"Current diffusers {getattr(diffusers, '__version__', '?')} "
                "in this Python env. Fix: pip install -U 'diffusers>=0.38.0' "
                "or use mlperf Bin/.../IHV/Diffusers/python/python.exe."
            ) from exc

    model_dir = _resolve_model_dir(runtime)

    cf = runtime.component_files
    deps_dir = (cf.get("__deps_dir__") or "").strip() or None
    provider_dll = resolve_ryzenai_provider_dll(deps_dir=deps_dir)
    if provider_dll:
        log.info("RyzenAI provider DLL: %s", provider_dll)
    else:
        log.warning(
            "RyzenAI provider DLL not found (%s). Run benchmark Download so "
            "Diffusers EP deps (OrtGenAI-RyzenAI) unpack beside IHV_Diffusers, "
            "or set RYZENAI_CUSTOM_OPS_DLL.",
            RYZENAI_PROVIDER_DLL,
        )

    is_distilled = cf.get("__is_distilled__", "true").strip().lower() not in (
        "0", "false", "no",
    )

    text_encoder_onnx = (
        cf.get("text_encoder")
        or _discover_file(os.path.join(model_dir, "text_encoder"), "*.onnx")
        or "qwen3_text_encoder_prompt_embeds_matmulnbits.onnx"
    )
    bn_weights = (
        _discover_file(os.path.join(model_dir, "vae_decoder"), "bn*.safetensors")
        or "bn.running_x.safetensors"
    )

    tokenizer = Qwen2TokenizerFast.from_pretrained(
        os.path.join(model_dir, "tokenizer"),
    )

    scheduler_config_path = os.path.join(model_dir, "scheduler", "scheduler_config.json")
    if os.path.exists(scheduler_config_path):
        with open(scheduler_config_path, encoding="utf-8") as f:
            scheduler_config = json.load(f)
        scheduler = FlowMatchEulerDiscreteScheduler.from_config(scheduler_config)
    else:
        scheduler = FlowMatchEulerDiscreteScheduler()

    from diffusers.pipelines.onnx_utils import OnnxRuntimeModel

    text_encoder = OnnxRuntimeModel.from_pretrained(
        os.path.join(model_dir, "text_encoder"),
        file_name=text_encoder_onnx,
        provider="CPUExecutionProvider",
        provider_options={},
    )
    vae_decoder_dir = os.path.join(model_dir, "vae_decoder")
    bn_running_mean, bn_running_var = _load_bn_running_stats(
        vae_decoder_dir, bn_weights,
    )

    vae = _load_optimized_model(vae_decoder_dir, deps_dir=deps_dir)
    transformer = _load_optimized_model(
        os.path.join(model_dir, "transformer"), deps_dir=deps_dir)

    vae.bn = nn.BatchNorm2d(
        bn_running_mean.numel(),
        eps=getattr(vae.config, "batch_norm_eps", 1e-5),
        affine=False,
        track_running_stats=False,
    )
    vae.bn.running_mean = bn_running_mean
    vae.bn.running_var = bn_running_var

    def _vae_decode(_self, latents, return_dict=False):
        import numpy as np

        latent_np = latents.detach().cpu().numpy().astype(np.float32)
        log.info(
            "VAE decode: feeding %s shape=%s",
            _self._vae_input_name, tuple(latent_np.shape),
        )
        out = _self.model.run(
            None, {_self._vae_input_name: latent_np},
        )
        sample = torch.from_numpy(np.asarray(out[0])).float()
        if (
            sample.ndim == 4
            and sample.shape[1] not in (1, 3, 4)
            and sample.shape[-1] in (1, 3, 4)
        ):
            sample = sample.permute(0, 3, 1, 2).contiguous()
        log.info("VAE decode done: sample shape=%s", tuple(sample.shape))
        if return_dict:
            return type("DecoderOutput", (), {"sample": sample})()
        return (sample,)

    vae._vae_input_name = _get_vae_input_name(vae)
    vae.decode = types.MethodType(_vae_decode, vae)

    pipe = Flux2KleinPipeline(
        scheduler=scheduler,
        tokenizer=tokenizer,
        text_encoder=text_encoder,
        transformer=transformer,
        vae=vae,
        is_distilled=is_distilled,
    )
    if hasattr(pipe, "set_progress_bar_config"):
        pipe.set_progress_bar_config(disable=True)

    _patch_flux2_klein_pipeline(pipe)

    pipe._mlperf_pipeline_kind = MLPERF_PIPELINE_KIND_AMD_ONNX  # type: ignore[attr-defined]
    log.info("AMD FLUX.2 Klein ONNX loaded from %s", model_dir)
    return pipe


def _load_bn_running_stats(
    vae_decoder_dir: str,
    bn_weights: str,
) -> tuple[torch.Tensor, torch.Tensor]:
    from safetensors import safe_open

    tensor_path = os.path.join(vae_decoder_dir, bn_weights)
    if not os.path.isfile(tensor_path):
        raise FileNotFoundError(
            f"BN weights not found: {tensor_path} "
            "(expected bn.running_mean / bn.running_var tensors)")

    with safe_open(tensor_path, framework="pt", device="cpu") as f:
        keys = list(f.keys())
        if "bn.running_mean" not in keys or "bn.running_var" not in keys:
            raise KeyError(
                f"{tensor_path}: expected bn.running_mean and bn.running_var, "
                f"got {keys}")
        bn_running_mean = f.get_tensor("bn.running_mean")
        bn_running_var = f.get_tensor("bn.running_var")

    bn_running_mean = bn_running_mean.float().contiguous()
    bn_running_var = bn_running_var.float().contiguous()
    log.info(
        "BN stats from %s: mean shape=%s var shape=%s",
        tensor_path, tuple(bn_running_mean.shape), tuple(bn_running_var.shape),
    )
    return bn_running_mean, bn_running_var


def _get_vae_input_name(vae: Any) -> str:
    names = [inp.name for inp in vae.model.get_inputs()]
    for candidate in ("latent_sample", "latents", "hidden_states"):
        if candidate in names:
            return candidate
    if not names:
        raise RuntimeError("VAE ONNX model has no inputs")
    return names[0]


def _onnx_input_numpy_dtype(ort_type: str):
    """ORT int inputs only; float feeds keep pipeline dtypes (no cast)."""
    if "int64" not in ort_type and "int32" not in ort_type:
        return None
    return TypeHelper.ort_type_to_numpy_type(ort_type)


class Flux2KleinTransformer:
    _SKIP_KWARGS = frozenset(("return_dict", "joint_attention_kwargs", "guidance"))

    def __init__(self, ort_model: Any) -> None:
        self._ort = ort_model
        self.model = ort_model.model
        self.config = ort_model.config
        inputs = ort_model.model.get_inputs()
        self._input_names = {inp.name for inp in inputs}
        self._input_dtypes = {
            inp.name: _onnx_input_numpy_dtype(inp.type) for inp in inputs
        }
        float_in = next(
            (inp.type for inp in inputs if inp.name == "hidden_states"),
            next(
                (inp.type for inp in inputs if "float" in inp.type),
                "tensor(float)",
            ),
        )
        self.dtype = TypeHelper.ort_type_to_torch_type(float_in)
        self.device = torch.device("cpu")

    @contextmanager
    def cache_context(self, *_args, **_kwargs):
        yield

    def __call__(self, **kwargs):
        log.debug("transformer ORT run")
        feed = {}
        for key, val in kwargs.items():
            if val is None or key in self._SKIP_KWARGS or key not in self._input_names:
                continue
            if isinstance(val, torch.Tensor):
                val = val.detach().cpu().numpy()
            np_dtype = self._input_dtypes.get(key)
            if np_dtype is not None:
                val = val.astype(np_dtype)
            feed[key] = val
        out = self.model.run(None, feed)
        if not out:
            return out
        return (torch.from_numpy(out[0]).to(dtype=self.dtype), *out[1:])


def _get_qwen3_prompt_embeds(
    text_encoder: Any,
    tokenizer: Any,
    prompt: str | list[str],
    dtype: torch.dtype | None = None,
    device: torch.device | None = None,
    max_sequence_length: int = 512,
    hidden_states_layers: tuple[int, ...] = (9, 18, 27),
):
    del hidden_states_layers
    dtype = dtype or torch.float32
    device = device or torch.device("cpu")
    prompt = [prompt] if isinstance(prompt, str) else prompt

    all_input_ids = []
    all_attention_masks = []
    for single_prompt in prompt:
        messages = [{"role": "user", "content": single_prompt}]
        text = tokenizer.apply_chat_template(
            messages,
            tokenize=False,
            add_generation_prompt=True,
            enable_thinking=False,
        )
        inputs = tokenizer(
            text,
            return_tensors="pt",
            padding="max_length",
            truncation=True,
            max_length=max_sequence_length,
        )
        all_input_ids.append(inputs["input_ids"])
        all_attention_masks.append(inputs["attention_mask"])

    input_ids = torch.cat(all_input_ids, dim=0)
    attention_mask = torch.cat(all_attention_masks, dim=0)

    int_dtype = _onnx_input_numpy_dtype(text_encoder.model.get_inputs()[0].type)
    seq_len = input_ids.shape[1]
    position_ids = torch.arange(seq_len, dtype=torch.int64).unsqueeze(0).expand(
        input_ids.shape[0], -1,
    )
    onnx_inputs = {
        "input_ids": input_ids.numpy().astype(int_dtype),
        "attention_mask": attention_mask.numpy().astype(int_dtype),
        "position_ids": position_ids.numpy().astype(int_dtype),
    }
    prompt_embeds = text_encoder(**onnx_inputs)[0]
    return torch.from_numpy(prompt_embeds).to(device=device, dtype=dtype)


def _patch_flux2_klein_pipeline(pipe: Any) -> None:
    from diffusers.pipelines.flux2.pipeline_flux2_klein import Flux2KleinPipeline

    pipe.transformer = Flux2KleinTransformer(pipe.transformer)
    pipe.vae.dtype = torch.float32
    pipe.vae.device = torch.device("cpu")
    pipe._get_qwen3_prompt_embeds = staticmethod(_get_qwen3_prompt_embeds)  # type: ignore[method-assign]

    _orig_call = Flux2KleinPipeline.__call__

    @functools.wraps(_orig_call)
    def _pipeline_call(self, *args, **kwargs):
        onnx_type = next(
            (inp.type for inp in self.transformer.model.get_inputs() if inp.name == "encoder_hidden_states"),
            "tensor(float)",
        )
        enc_dtype = TypeHelper.ort_type_to_torch_type(onnx_type)
        orig_encode = self.encode_prompt

        def _encode_with_cast(*a, **kw):
            pe, ti = orig_encode(*a, **kw)
            return pe.to(dtype=enc_dtype), ti

        self.encode_prompt = _encode_with_cast
        try:
            return _orig_call(self, *args, **kwargs)
        finally:
            self.encode_prompt = orig_encode

    pipe.__call__ = types.MethodType(_pipeline_call, pipe)


def build_ryzenai_session_options(
    provider_dll: str,
    dd_model_dir: Path,
) -> Any:
    """SessionOptions for transformer/vae (GenAI-SD config_session_options subset)."""
    ctypes.CDLL(provider_dll)
    register_ryzenai_ep(provider_dll)

    sess_opts = onnxruntime.SessionOptions()
    sess_opts.graph_optimization_level = (
        onnxruntime.GraphOptimizationLevel.ORT_DISABLE_ALL
    )

    npu_devices = [
        d for d in onnxruntime.get_ep_devices()
        if d.ep_name == _EP_NAME
    ]
    sess_opts.add_provider_for_devices(
        npu_devices, {
            "dd_cache": (dd_model_dir / "cache").as_posix(),
        },
    )
    sess_opts.add_provider("CPUExecutionProvider", {})
    sess_opts.register_custom_ops_library(provider_dll)
    return sess_opts


def _load_optimized_model(
    component_dir: str,
    *,
    deps_dir: str | None = None,
    model_file: str = "replaced.onnx",
) -> Any:
    import onnxruntime
    from diffusers.pipelines.onnx_utils import OnnxRuntimeModel

    dd_model_dir_candidates = [
        os.path.join(component_dir, "dynamic", "dd"),
        os.path.join(component_dir, "dd"),
    ]
    for dd_model_dir in dd_model_dir_candidates:
        if os.path.exists(dd_model_dir):
            break
    else:
        raise FileNotFoundError(f"dd model directory not found: {dd_model_dir}")

    model_path = os.path.join(dd_model_dir, model_file)
    config_path = os.path.join(component_dir, "config.json")
    if not os.path.exists(config_path):
        raise FileNotFoundError(f"config.json not found: {config_path}")    
    provider_dll = resolve_ryzenai_provider_dll(deps_dir=deps_dir)
    if not provider_dll:
        raise FileNotFoundError(
            f"RyzenAI provider DLL not found ({RYZENAI_PROVIDER_DLL}). "
            "Download Diffusers EP deps or set RYZENAI_CUSTOM_OPS_DLL.",
        )

    sess_opts = build_ryzenai_session_options(
        provider_dll, Path(dd_model_dir),
    )
    session = onnxruntime.InferenceSession(
        model_path, sess_options=sess_opts, providers=None,
    )
    model = OnnxRuntimeModel(session)
    with open(config_path, encoding="utf-8") as f:
        cfg = json.load(f)
        model.config = types.SimpleNamespace(
            **{
                k: types.SimpleNamespace(**v) if isinstance(v, dict) else v
                for k, v in cfg.items()
            },
        )
    return model
