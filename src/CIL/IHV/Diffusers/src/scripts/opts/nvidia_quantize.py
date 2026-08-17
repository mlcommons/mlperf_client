"""FP8 / NVFP4 weight loading for FLUX.2 Klein via NVIDIA ModelOpt.

Both quantized paths build a meta Flux2 transformer, apply ModelOpt
quantizers, and load the pre-quantized BFL weights; they pair with the
tensorrt_rtx opt and register no-op `apply` handlers (`fp8` / `nvfp4`) whose
real work happens in the model loader at `models/flux2_klein.py`.

- FP8 (E4M3) targets per-tensor FP8 (SM >= 8.9 / Ada), so GPUs without NVFP4
  (Blackwell) tensor cores can still run a quantized FLUX.2 transformer.
- NVFP4 targets Blackwell (sm_100+).

The two formats share almost everything; the differences are isolated to the
state-dict suffix/qkv-chunk rules (`convert_flux2_*_state_dict`) and the
ModelOpt quant config + pre-quantized loader patch fed into the shared
`_load_modelopt_transformer`.

Adapted from the flux2_trt_sample torch_tensorrt_rtx workflows.
"""
from __future__ import annotations

import logging
from pathlib import Path
from typing import Any, Callable, TYPE_CHECKING

import torch

from registry import register_opt

if TYPE_CHECKING:
    from runtime import Runtime

log = logging.getLogger(__name__)


# FP8 E4M3 max magnitude. ModelOpt's per-tensor FP8 scale is amax / 448, so a
# checkpoint weight_scale (the dequant scale) maps back to amax = scale * 448.
FP8_E4M3_MAX = 448.0


class _DedupLogFilter(logging.Filter):
    """Emits each distinct (logger, level, message) once.

    Some libraries log the same warning on every call (e.g. Torch-TensorRT
    conversion warnings repeated per layer); collapse the flood to one line.
    """

    def __init__(self) -> None:
        super().__init__()
        self._seen: set = set()

    def filter(self, record: logging.LogRecord) -> bool:
        key = (record.name, record.levelno, record.getMessage())
        if key in self._seen:
            return False
        self._seen.add(key)
        return True


# --------------------------------------------------------------------------- #
# BFL FLUX-2 -> diffusers state-dict conversion (shared FP8 / NVFP4)
# --------------------------------------------------------------------------- #
# The rename maps below are identical for FP8 and NVFP4; only the per-linear
# tensor `suffixes` and the qkv `chunk_suffixes` differ (NVFP4 carries an extra
# weight_scale_2 and chunks its per-block weight_scale across q/k/v, while FP8
# scales are per-tensor scalars that are replicated instead of chunked).
_TOP_LEVEL = {
    "img_in.weight": "x_embedder.weight",
    "txt_in.weight": "context_embedder.weight",
    "time_in.in_layer.weight": "time_guidance_embed.timestep_embedder.linear_1.weight",
    "time_in.out_layer.weight": "time_guidance_embed.timestep_embedder.linear_2.weight",
    "double_stream_modulation_img.lin.weight": "double_stream_modulation_img.linear.weight",
    "double_stream_modulation_txt.lin.weight": "double_stream_modulation_txt.linear.weight",
    "single_stream_modulation.lin.weight": "single_stream_modulation.linear.weight",
    "final_layer.linear.weight": "proj_out.weight",
}
_DOUBLE_BLOCK = {
    "img_attn.norm.query_norm": "attn.norm_q",
    "img_attn.norm.key_norm": "attn.norm_k",
    "img_attn.proj": "attn.to_out.0",
    "img_mlp.0": "ff.linear_in",
    "img_mlp.2": "ff.linear_out",
    "txt_attn.norm.query_norm": "attn.norm_added_q",
    "txt_attn.norm.key_norm": "attn.norm_added_k",
    "txt_attn.proj": "attn.to_add_out",
    "txt_mlp.0": "ff_context.linear_in",
    "txt_mlp.2": "ff_context.linear_out",
}
_SINGLE_BLOCK = {
    "linear1": "attn.to_qkv_mlp_proj",
    "linear2": "attn.to_out",
    "norm.query_norm": "attn.norm_q",
    "norm.key_norm": "attn.norm_k",
}


def _convert_flux2_state_dict(
    original_state_dict: dict[str, torch.Tensor],
    *,
    suffixes: tuple[str, ...],
    chunk_suffixes: set[str],
) -> dict[str, torch.Tensor]:
    """Rename BFL FLUX-2 keys to diffusers + split fused qkv.

    `suffixes` lists the per-linear tensor suffixes for the quant format;
    `chunk_suffixes` are the ones chunked across the q/k/v split (others are
    replicated, since per-tensor scalar scales are shared).
    """
    from diffusers.loaders.single_file_utils import swap_scale_shift

    state_dict: dict[str, torch.Tensor] = {}

    for key, tensor in original_state_dict.items():
        if key == "final_layer.adaLN_modulation.1.weight":
            state_dict["norm_out.linear.weight"] = swap_scale_shift(tensor, 0)
            continue
        if key in _TOP_LEVEL:
            state_dict[_TOP_LEVEL[key]] = tensor
            continue

        if key.startswith("double_blocks."):
            parts = key.split(".")
            block_idx = parts[1]
            rest = ".".join(parts[2:])
            for suffix in suffixes:
                if rest.endswith(f".{suffix}"):
                    base = rest[: -(len(suffix) + 1)]
                    break
            else:
                raise ValueError(f"Unrecognized double-block tensor key: {key}")

            param = "weight" if suffix == "scale" else suffix
            if base.endswith("qkv"):
                chunks = (
                    torch.chunk(tensor, 3, dim=0)
                    if suffix in chunk_suffixes
                    else (tensor, tensor, tensor)
                )
                names = (
                    ("attn.to_q", "attn.to_k", "attn.to_v")
                    if parts[2].startswith("img")
                    else ("attn.add_q_proj", "attn.add_k_proj", "attn.add_v_proj")
                )
                for name, chunk in zip(names, chunks, strict=True):
                    state_dict[f"transformer_blocks.{block_idx}.{name}.{param}"] = chunk
            else:
                state_dict[f"transformer_blocks.{block_idx}.{_DOUBLE_BLOCK[base]}.{param}"] = tensor
            continue

        if key.startswith("single_blocks."):
            parts = key.split(".")
            block_idx = parts[1]
            rest = ".".join(parts[2:])
            for suffix in suffixes:
                if rest.endswith(f".{suffix}"):
                    base = rest[: -(len(suffix) + 1)]
                    break
            else:
                raise ValueError(f"Unrecognized single-block tensor key: {key}")
            param = "weight" if suffix == "scale" else suffix
            state_dict[f"single_transformer_blocks.{block_idx}.{_SINGLE_BLOCK[base]}.{param}"] = tensor
            continue

        raise ValueError(f"Unrecognized tensor key: {key}")

    return state_dict


def convert_flux2_fp8_state_dict(
    original_state_dict: dict[str, torch.Tensor],
) -> dict[str, torch.Tensor]:
    """Rename BFL FLUX-2 FP8 keys to diffusers + split fused qkv.

    FP8 linears carry only a scalar per-tensor weight_scale / input_scale (no
    weight_scale_2, no tiled scales); those scalars are replicated onto each
    q/k/v split (only weight/bias are chunked).
    """
    return _convert_flux2_state_dict(
        original_state_dict,
        suffixes=("weight_scale", "input_scale", "weight", "bias", "scale"),
        chunk_suffixes={"weight", "bias"},
    )


def convert_flux2_nvfp4_state_dict(
    original_state_dict: dict[str, torch.Tensor],
) -> dict[str, torch.Tensor]:
    """Rename BFL FLUX-2 NVFP4 keys to diffusers + split fused qkv."""
    return _convert_flux2_state_dict(
        original_state_dict,
        suffixes=("weight_scale_2", "weight_scale", "input_scale", "weight", "bias", "scale"),
        chunk_suffixes={"weight", "weight_scale", "bias"},
    )


# --------------------------------------------------------------------------- #
# NVFP4 pre-quantized loader patch + scale helpers
# --------------------------------------------------------------------------- #
def from_128x4_tiled_scale(scale: torch.Tensor) -> torch.Tensor:
    """Un-tile a 128x4 block-scale tensor back to its logical layout."""
    if scale.ndim != 2:
        return scale
    outer, inner = scale.shape
    if outer % 128 != 0 or inner % 4 != 0:
        return scale
    return (
        scale.reshape(outer // 128, inner // 4, 32, 4, 4)
        .permute(0, 3, 2, 1, 4)
        .contiguous()
        .reshape(outer, inner)
    )


def swap_bfl_packed_fp4_to_low_first(packed_weight: torch.Tensor) -> torch.Tensor:
    """Swap the two FP4 nibbles in each packed uint8 to low-first order."""
    return ((packed_weight & 0x0F) << 4) | (packed_weight >> 4)


def patch_modelopt_prequantized_loader() -> None:
    """Patch the diffusers ModelOpt quantizer to load BFL-packed NVFP4 weights."""
    from diffusers.quantizers.modelopt.modelopt_quantizer import NVIDIAModelOptQuantizer
    from diffusers.utils import get_module_from_name
    from modelopt.torch.quantization.qtensor import NVFP4QTensor, QTensorWrapper

    if getattr(NVIDIAModelOptQuantizer, "_flux2_mlperf_prequantized_loader_patch", False):
        return

    original_create = NVIDIAModelOptQuantizer.create_quantized_param

    def create_quantized_param(self, model, param_value, param_name, target_device, *args, **kwargs):
        state_dict = args[0] if args else {}
        dtype = kwargs.get("dtype") or torch.bfloat16
        module, tensor_name = get_module_from_name(model, param_name)
        base_name = param_name[: -len(".weight")] if param_name.endswith(".weight") else param_name
        weight_scale = state_dict.get(f"{base_name}.weight_scale")
        weight_scale_2 = state_dict.get(f"{base_name}.weight_scale_2")
        input_scale = state_dict.get(f"{base_name}.input_scale")

        if (
            self.pre_quantized
            and param_value.dtype == torch.uint8
            and weight_scale is not None
            and weight_scale_2 is not None
            and hasattr(module, "weight_quantizer")
        ):
            packed_weight = swap_bfl_packed_fp4_to_low_first(param_value.to(device=target_device))
            original_shape = torch.Size((*packed_weight.shape[:-1], packed_weight.shape[-1] * 2))
            module._parameters[tensor_name] = QTensorWrapper(
                NVFP4QTensor(original_shape, dtype, packed_weight)
            )
            weight_scale = from_128x4_tiled_scale(weight_scale)
            module.weight_quantizer._set_buffer("_scale", weight_scale.to(device=target_device))
            module.weight_quantizer._set_buffer("_double_scale", weight_scale_2.to(device=target_device))
            module.weight_quantizer._dequantize = True
            module.register_buffer("weight_scale", weight_scale.to(device=target_device))
            module.register_buffer("weight_scale_2", weight_scale_2.to(device=target_device))
            if input_scale is not None and hasattr(module, "input_quantizer"):
                module.input_quantizer.amax = input_scale.to(device=target_device).float() * (6.0 * 448.0)
                module.register_buffer("input_scale", input_scale.to(device=target_device))
            return

        if self.pre_quantized:
            module._parameters[tensor_name] = torch.nn.Parameter(
                param_value.to(device=target_device),
                requires_grad=False,
            )
            return

        return original_create(self, model, param_value, param_name, target_device, *args, **kwargs)

    NVIDIAModelOptQuantizer.create_quantized_param = create_quantized_param
    NVIDIAModelOptQuantizer._flux2_mlperf_prequantized_loader_patch = True


# --------------------------------------------------------------------------- #
# FP8 pre-quantized loader patch + cpp-extension warning silencer
# --------------------------------------------------------------------------- #
def patch_modelopt_fp8_prequantized_loader() -> None:
    """Patch the diffusers ModelOpt quantizer to load BFL FP8 weights.

    The BFL FP8 checkpoint stores each quantized linear as a float8_e4m3fn
    weight plus scalar weight_scale / input_scale dequant scales. ModelOpt's
    fake-quant export path wants a real-valued weight plus a quantizer amax,
    so we dequantize (weight * weight_scale) and program the amax values; the
    export then re-emits FP8 via tensorrt.quantize_op Q/DQ nodes.
    """
    from diffusers.quantizers.modelopt.modelopt_quantizer import NVIDIAModelOptQuantizer
    from diffusers.utils import get_module_from_name

    if getattr(NVIDIAModelOptQuantizer, "_flux2_fp8_prequantized_loader_patch", False):
        return

    original_create = NVIDIAModelOptQuantizer.create_quantized_param

    def create_quantized_param(self, model, param_value, param_name, target_device, *args, **kwargs):
        state_dict = args[0] if args else {}
        dtype = kwargs.get("dtype") or torch.bfloat16
        module, tensor_name = get_module_from_name(model, param_name)
        base_name = param_name[: -len(".weight")] if param_name.endswith(".weight") else param_name
        weight_scale = state_dict.get(f"{base_name}.weight_scale")
        input_scale = state_dict.get(f"{base_name}.input_scale")

        if (
            self.pre_quantized
            and param_value.dtype == torch.float8_e4m3fn
            and weight_scale is not None
            and hasattr(module, "weight_quantizer")
        ):
            weight_scale = weight_scale.to(device=target_device).float()
            dequantized = (
                param_value.to(device=target_device).float() * weight_scale
            ).to(dtype)
            module._parameters[tensor_name] = torch.nn.Parameter(
                dequantized, requires_grad=False
            )
            module.weight_quantizer.amax = (weight_scale * FP8_E4M3_MAX).squeeze()
            if input_scale is not None and hasattr(module, "input_quantizer"):
                input_scale = input_scale.to(device=target_device).float()
                module.input_quantizer.amax = (input_scale * FP8_E4M3_MAX).squeeze()
            return

        if self.pre_quantized:
            module._parameters[tensor_name] = torch.nn.Parameter(
                param_value.to(device=target_device),
                requires_grad=False,
            )
            return

        return original_create(self, model, param_value, param_name, target_device, *args, **kwargs)

    NVIDIAModelOptQuantizer.create_quantized_param = create_quantized_param
    NVIDIAModelOptQuantizer._flux2_fp8_prequantized_loader_patch = True


def _silence_cpp_extension_compiler_warning() -> None:
    """Drop torch's noisy 'Error checking compiler version for cl' warning.

    The deployed embedded env has no MSVC ``cl`` on PATH, so torch's
    cpp_extension ABI probe fails and dumps a multi-line traceback to stdout.
    Inference runs via TensorRT and never JIT-builds C++ extensions, so the
    failed probe is harmless noise — drop just that one message.
    """
    target = logging.getLogger("torch.utils.cpp_extension")
    if getattr(target, "_flux2_cpp_ext_filtered", False):
        return

    class _CppExtFilter(logging.Filter):
        def filter(self, record: logging.LogRecord) -> bool:
            return "Error checking compiler version" not in record.getMessage()

    target.addFilter(_CppExtFilter())
    target._flux2_cpp_ext_filtered = True


# --------------------------------------------------------------------------- #
# Quantizer enable/disable helpers (shared)
# --------------------------------------------------------------------------- #
def disable_unneeded_quantizers(
    model: torch.nn.Module,
    state_dict: dict[str, torch.Tensor],
    *,
    quantize_activations: bool,
) -> None:
    """Disable quantizers for layers absent from the quantized state dict."""
    quantized_linear_names = {
        key[: -len(".weight_scale")]
        for key in state_dict
        if key.endswith(".weight_scale")
    }
    for name, module in model.named_modules():
        if not hasattr(module, "weight_quantizer"):
            continue
        if name not in quantized_linear_names:
            module.input_quantizer.disable()
            module.weight_quantizer.disable()
            module.output_quantizer.disable()
        elif not quantize_activations:
            module.input_quantizer.disable()


def disable_quantizer_tree(quantizer: torch.nn.Module, seen: set[int]) -> int:
    """Recursively disable an enabled quantizer and its children."""
    if id(quantizer) in seen:
        return 0
    seen.add(id(quantizer))
    disabled = 0
    if hasattr(quantizer, "disable") and hasattr(quantizer, "is_enabled") and getattr(quantizer, "is_enabled"):
        quantizer.disable()
        disabled += 1
    for child in quantizer.children():
        disabled += disable_quantizer_tree(child, seen)
    return disabled


def disable_uncalibrated_activation_quantizers(model: torch.nn.Module) -> dict[str, object]:
    """Disable input quantizers that have no calibrated amax (would error)."""
    info: dict[str, object] = {"quantizers_disabled": 0, "examples": [], "_seen": set()}
    for name, module in model.named_modules():
        quantizer = getattr(module, "input_quantizer", None)
        if not isinstance(quantizer, torch.nn.Module):
            continue
        if not getattr(quantizer, "is_enabled", False):
            continue
        if getattr(quantizer, "amax", None) is not None:
            continue
        before = int(info["quantizers_disabled"])
        info["quantizers_disabled"] = before + disable_quantizer_tree(quantizer, info["_seen"])
        if int(info["quantizers_disabled"]) > before and len(info["examples"]) < 8:
            info["examples"].append(f"{name}.input_quantizer" if name else "input_quantizer")
    info.pop("_seen", None)
    return info


# --------------------------------------------------------------------------- #
# Shared ModelOpt transformer loader + per-format wrappers
# --------------------------------------------------------------------------- #
def _load_modelopt_transformer(
    checkpoint_path: str | Path,
    *,
    base_model: str,
    cache_dir: str | Path | None,
    local_files_only: bool,
    dtype: torch.dtype,
    quantize_activations: bool,
    quant_type: str,
    modelopt_config: Any,
    convert_fn: Callable[[dict[str, torch.Tensor]], dict[str, torch.Tensor]],
    patch_fn: Callable[[], None],
) -> torch.nn.Module:
    """Build a meta Flux2 transformer, apply ModelOpt quantizers, load weights."""
    import gc

    import modelopt.torch.opt as mto
    from accelerate import init_empty_weights
    from diffusers import Flux2Transformer2DModel, NVIDIAModelOptConfig
    from diffusers.models.model_loading_utils import load_model_dict_into_meta
    from diffusers.quantizers.auto import DiffusersAutoQuantizer
    from modelopt.torch.opt import enable_huggingface_checkpointing
    from safetensors.torch import load_file

    patch_fn()
    enable_huggingface_checkpointing()

    raw_state_dict = load_file(str(checkpoint_path), device="cpu")
    state_dict = convert_fn(raw_state_dict)
    del raw_state_dict

    quantization_config = NVIDIAModelOptConfig(
        quant_type=quant_type,
        modelopt_config=modelopt_config,
    )
    hf_quantizer = DiffusersAutoQuantizer.from_config(quantization_config, pre_quantized=True)
    hf_quantizer.validate_environment()
    dtype = hf_quantizer.update_torch_dtype(dtype)

    config = Flux2Transformer2DModel.load_config(
        base_model,
        subfolder="transformer",
        cache_dir=cache_dir,
        local_files_only=local_files_only,
    )
    with init_empty_weights():
        transformer = Flux2Transformer2DModel.from_config(config)

    mto.apply_mode(transformer, mode=[("quantize", modelopt_config)])
    disable_unneeded_quantizers(
        transformer,
        state_dict,
        quantize_activations=quantize_activations,
    )

    model_state_dict = transformer.state_dict()
    unexpected_keys = [key for key in state_dict if key not in model_state_dict]
    load_model_dict_into_meta(
        transformer,
        state_dict,
        dtype=dtype,
        device_map={"": "cpu"},
        hf_quantizer=hf_quantizer,
        keep_in_fp32_modules=[],
        unexpected_keys=unexpected_keys,
    )
    hf_quantizer.postprocess_model(transformer)
    transformer.hf_quantizer = hf_quantizer
    transformer.eval()

    gc.collect()
    torch.cuda.empty_cache()
    return transformer


def load_fp8_transformer(
    checkpoint_path: str | Path,
    *,
    base_model: str,
    cache_dir: str | Path | None,
    local_files_only: bool,
    dtype: torch.dtype,
    quantize_activations: bool,
) -> torch.nn.Module:
    """Build a meta Flux2 transformer, apply FP8 quantizers, load the weights."""
    _silence_cpp_extension_compiler_warning()
    import modelopt.torch.quantization as mtq

    return _load_modelopt_transformer(
        checkpoint_path,
        base_model=base_model,
        cache_dir=cache_dir,
        local_files_only=local_files_only,
        dtype=dtype,
        quantize_activations=quantize_activations,
        quant_type="FP8",
        modelopt_config=mtq.FP8_DEFAULT_CFG,
        convert_fn=convert_flux2_fp8_state_dict,
        patch_fn=patch_modelopt_fp8_prequantized_loader,
    )


def load_nvfp4_transformer(
    checkpoint_path: str | Path,
    *,
    base_model: str,
    cache_dir: str | Path | None,
    local_files_only: bool,
    dtype: torch.dtype,
    quantize_activations: bool,
) -> torch.nn.Module:
    """Build a meta Flux2 transformer, apply NVFP4 quantizers, load the weights."""
    import modelopt.torch.quantization as mtq

    return _load_modelopt_transformer(
        checkpoint_path,
        base_model=base_model,
        cache_dir=cache_dir,
        local_files_only=local_files_only,
        dtype=dtype,
        quantize_activations=quantize_activations,
        quant_type="NVFP4",
        modelopt_config=mtq.NVFP4_DEFAULT_CFG,
        convert_fn=convert_flux2_nvfp4_state_dict,
        patch_fn=patch_modelopt_prequantized_loader,
    )


@register_opt("fp8")
def apply_fp8(runtime: "Runtime", params: dict[str, Any]) -> dict[str, Any]:
    """No-op — real FP8 ModelOpt work happens in the model loader."""
    target = params.get("target", "transformer")
    return {"target": target, "applied_at_load_time": True}


@register_opt("nvfp4")
def apply_nvfp4(runtime: "Runtime", params: dict[str, Any]) -> dict[str, Any]:
    """No-op — real NVFP4 ModelOpt work happens in the model loader."""
    target = params.get("target", "transformer")
    return {"target": target, "applied_at_load_time": True}
