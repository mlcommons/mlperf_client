"""Torch-TensorRT-RTX optimization for NVIDIA pipelines."""
from __future__ import annotations

import logging
import tempfile
from collections.abc import Mapping
from contextlib import contextmanager, nullcontext
from dataclasses import dataclass
from pathlib import Path
from typing import Any, TYPE_CHECKING

import torch

from registry import register_opt

if TYPE_CHECKING:
    from runtime import Runtime

log = logging.getLogger(__name__)


# Fixed compile shapes for FLUX.2 Klein 1024x1024 / 4 steps. Swap out later.
FIXED_HEIGHT = 1024
FIXED_WIDTH = 1024
FIXED_MAX_SEQUENCE_LENGTH = 512
FIXED_SEED = 1234
FIXED_STEPS = 4  # documented; steps do not change per-call transformer shapes.

NVFP4_DEQUANTIZE_OP = "flux2_klein::nvfp4_dequantize_weight_opaque"


@dataclass
class _TorchTRTCachePaths:
    cache_dir: Path | None
    runtime_cache_path: Path
    engine_cache_dir: Path | None
    temporary_dir: tempfile.TemporaryDirectory | None = None


def nvfp4_dtype_code(dtype: torch.dtype) -> int:
    return {torch.float16: 0, torch.bfloat16: 1, torch.float32: 2}[dtype]


def nvfp4_dtype_from_code(code: int) -> torch.dtype:
    return {0: torch.float16, 1: torch.bfloat16, 2: torch.float32}[int(code)]


def register_nvfp4_weight_export_op() -> None:
    """Register the opaque NVFP4 dequantize op used as the export bridge."""
    if getattr(register_nvfp4_weight_export_op, "_registered", False):
        return
    try:
        torch.library.define(
            NVFP4_DEQUANTIZE_OP,
            "(Tensor packed_weight, Tensor scale, Tensor double_scale, int dtype_code) -> Tensor",
            tags=(torch.Tag.nondeterministic_bitwise,),
        )
    except RuntimeError as exc:
        if "already" not in str(exc).lower():
            raise

    def impl(
        packed_weight: torch.Tensor,
        scale: torch.Tensor,
        double_scale: torch.Tensor,
        dtype_code: int,
    ):
        from modelopt.torch.quantization.qtensor.nvfp4_tensor import NVFP4QTensor

        dtype = nvfp4_dtype_from_code(dtype_code)
        original_shape = torch.Size((*packed_weight.shape[:-1], packed_weight.shape[-1] * 2))
        qtensor = NVFP4QTensor(original_shape, dtype, packed_weight)
        return qtensor.dequantize(
            dtype=dtype,
            scale=scale,
            double_scale=double_scale,
            block_sizes={-1: 16, "type": "dynamic", "scale_bits": (4, 3)},
        )

    def fake(
        packed_weight: torch.Tensor,
        scale: torch.Tensor,
        double_scale: torch.Tensor,
        dtype_code: int,
    ):
        del scale, double_scale
        dtype = nvfp4_dtype_from_code(dtype_code)
        return torch.empty(
            (*packed_weight.shape[:-1], packed_weight.shape[-1] * 2),
            dtype=dtype,
            device=packed_weight.device,
        )

    try:
        torch.library.impl(NVFP4_DEQUANTIZE_OP, ["cpu", "cuda"])(impl)
    except RuntimeError as exc:
        if "already" not in str(exc).lower():
            raise
    try:
        torch.library.register_fake(NVFP4_DEQUANTIZE_OP)(fake)
    except RuntimeError as exc:
        if "already" not in str(exc).lower():
            raise
    register_nvfp4_weight_export_op._registered = True


def register_nvfp4_weight_converter() -> None:
    """Register the Torch-TensorRT converter that emits native FP4 dequant layers."""
    if getattr(register_nvfp4_weight_converter, "_registered", False):
        return
    register_nvfp4_weight_export_op()

    import tensorrt_rtx as trt
    from torch_tensorrt.dynamo._SourceIR import SourceIR
    from torch_tensorrt.dynamo.conversion._ConverterRegistry import dynamo_tensorrt_converter
    from torch_tensorrt.dynamo.conversion.converter_utils import get_trt_tensor, set_layer_name

    def trt_dtype_from_code(code: int):
        if int(code) == 0:
            return trt.DataType.HALF
        if int(code) == 1:
            return trt.DataType.BF16
        if int(code) == 2:
            return trt.DataType.FLOAT
        raise ValueError(f"Unsupported NVFP4 output dtype code: {code}")

    @dynamo_tensorrt_converter(
        torch.ops.flux2_klein.nvfp4_dequantize_weight_opaque.default,
        supports_dynamic_shapes=True,
    )
    def converter(ctx, target, args, kwargs, name):
        del kwargs
        packed_weight, scale, double_scale, dtype_code = args
        output_type = trt_dtype_from_code(dtype_code)

        # TensorRT-RTX expects constant weight buffers host-resident during build.
        packed_weight = packed_weight.detach().cpu().contiguous()
        scale = scale.detach().cpu().contiguous()
        double_scale = double_scale.detach().cpu().contiguous()

        packed_fp4 = get_trt_tensor(
            ctx,
            packed_weight,
            name + "_packed_fp4",
            target_quantized_type=trt.DataType.FP4,
        )
        scale_fp8 = get_trt_tensor(
            ctx,
            scale,
            name + "_scale_fp8",
            target_quantized_type=trt.DataType.FP8,
        )
        global_scale = get_trt_tensor(ctx, double_scale, name + "_global_scale")

        scale_layer = ctx.net.add_dequantize(scale_fp8, global_scale, output_type)
        scale_layer.axis = -1
        scale_layer.to_type = output_type
        set_layer_name(scale_layer, target, name + "_dequantize_scale", SourceIR.ATEN)
        dequantized_scale = scale_layer.get_output(0)

        data_layer = ctx.net.add_dequantize(packed_fp4, dequantized_scale, output_type)
        data_layer.axis = -1
        data_layer.to_type = output_type
        set_layer_name(data_layer, target, name + "_dequantize_data", SourceIR.ATEN)
        return data_layer.get_output(0)

    register_nvfp4_weight_converter._registered = True


def patch_torchtrt_constant_folding_for_nvfp4() -> None:
    """Keep the NVFP4 dequantize op out of Torch-TensorRT constant folding."""
    register_nvfp4_weight_export_op()

    from torch_tensorrt.dynamo.lowering.passes import constant_folding

    folder_cls = constant_folding._TorchTensorRTConstantFolder
    if getattr(folder_cls, "_flux2_mlperf_nvfp4_skip_patch", False):
        return

    original_init = folder_cls.__init__

    def init_with_nvfp4_skip(self, *args, **kwargs):
        original_init(self, *args, **kwargs)
        self.quantization_ops.add(
            torch.ops.flux2_klein.nvfp4_dequantize_weight_opaque.default
        )

    folder_cls.__init__ = init_with_nvfp4_skip
    folder_cls._flux2_mlperf_nvfp4_skip_patch = True


def patch_modelopt_nvfp4_export_bridge(dtype: torch.dtype) -> None:
    """Route ModelOpt NVFP4 dequantizers through the opaque export op."""
    register_nvfp4_weight_export_op()
    from modelopt.torch.quantization.nn.modules.tensor_quantizer import TensorQuantizer
    from modelopt.torch.quantization.qtensor import QTensorWrapper
    from modelopt.torch.quantization.utils import is_torch_export_mode

    if getattr(TensorQuantizer, "_flux2_mlperf_nvfp4_export_bridge", False):
        TensorQuantizer._flux2_mlperf_nvfp4_export_dtype_code = nvfp4_dtype_code(dtype)
        return

    original_forward = TensorQuantizer.forward

    def forward_with_bridge(self, inputs):
        is_nvfp4_dequantizer = (
            is_torch_export_mode()
            and getattr(self, "_dequantize", False)
            and getattr(self, "block_sizes", None) is not None
            and self.block_sizes.get("scale_bits") == (4, 3)
            and hasattr(self, "_scale")
            and hasattr(self, "_double_scale")
        )
        if is_nvfp4_dequantizer:
            packed_weight = inputs.data if isinstance(inputs, QTensorWrapper) else inputs
            if isinstance(packed_weight, torch.Tensor) and packed_weight.dtype == torch.uint8:
                dtype_code = getattr(
                    self,
                    "_flux2_mlperf_nvfp4_export_dtype_code",
                    getattr(TensorQuantizer, "_flux2_mlperf_nvfp4_export_dtype_code", 0),
                )
                return torch.ops.flux2_klein.nvfp4_dequantize_weight_opaque.default(
                    packed_weight,
                    self._scale,
                    self._double_scale,
                    int(dtype_code),
                )
        return original_forward(self, inputs)

    TensorQuantizer.forward = forward_with_bridge
    TensorQuantizer._flux2_mlperf_nvfp4_export_bridge = True
    TensorQuantizer._flux2_mlperf_nvfp4_export_dtype_code = nvfp4_dtype_code(dtype)




class _CompiledFlux2TransformerWrapper(torch.nn.Module):
    """Wraps the compiled transformer with the Flux2 forward signature."""

    def __init__(
        self,
        compiled: torch.nn.Module,
        original: torch.nn.Module,
        *,
        dtype: torch.dtype,
        device: str | torch.device,
        # temporary_cache_dir: tempfile.TemporaryDirectory | None,
        weight_streaming: Bool | False
    ) -> None:
        import copy
        super().__init__()
        self.compiled = compiled
        self.config = copy.deepcopy(original.config)
        self.dtype = dtype
        self.device = torch.device(device)
        # Keep the temp cache dir alive for the wrapper's lifetime.
        # self._temporary_cache_dir = temporary_cache_dir
        self.weight_streaming = weight_streaming
        self._offload_marker = torch.nn.Parameter(torch.empty((), device=self.device, dtype=dtype),requires_grad=False)

    def cache_context(self, *_args, **_kwargs):
        return nullcontext()
    
    def _apply(self, fn):
        result = super()._apply(fn)
        marker = self._offload_marker
        self.device = marker.device
        if marker.is_floating_point():
            self.dtype = marker.dtype
        return result


    def forward(
        self,
        hidden_states: torch.Tensor,
        encoder_hidden_states: torch.Tensor | None = None,
        timestep: torch.Tensor | None = None,
        img_ids: torch.Tensor | None = None,
        txt_ids: torch.Tensor | None = None,
        guidance: torch.Tensor | None = None,
        joint_attention_kwargs: dict[str, Any] | None = None,
        return_dict: bool = True,
        controlnet_block_samples=None,
        controlnet_single_block_samples=None,
        kv_cache=None,
        kv_cache_mode: str | None = None,
        num_ref_tokens: int = 0,
        ref_fixed_timestep: float = 0.0,
        **kwargs,
    ):
        if (
            kv_cache is not None
            or kv_cache_mode is not None
            or num_ref_tokens
            or ref_fixed_timestep
        ):
            raise NotImplementedError(
                "The Torch-TensorRT wrapper only supports the standard no-KV path."
            )
        if controlnet_block_samples is not None or controlnet_single_block_samples is not None:
            raise NotImplementedError(
                "The Torch-TensorRT wrapper does not support ControlNet inputs."
            )
        if kwargs:
            raise NotImplementedError(
                f"The Torch-TensorRT wrapper does not support extra arguments: {sorted(kwargs)}"
            )
        if guidance is None:
            guidance = torch.zeros_like(timestep)
        call_kwargs = dict(
            hidden_states=hidden_states.contiguous(),
            encoder_hidden_states=encoder_hidden_states.contiguous(),
            timestep=timestep.contiguous(),
            img_ids=img_ids.contiguous(),
            txt_ids=txt_ids.contiguous(),
            guidance=guidance.contiguous(),
            joint_attention_kwargs=joint_attention_kwargs or {},
            return_dict=return_dict,
        )

        res = self.compiled(**call_kwargs)
        return res


class _Flux2TextEncoderExportModule(torch.nn.Module):
    """Tensor-in/tensor-out view of the Qwen3 text encoder for export.

    The Flux2 pipeline only reads ``output.hidden_states[k]`` for ``k`` in
    ``text_encoder_out_layers`` (default 9, 18, 27). transformers v5 collects
    those via the ``@capture_outputs`` hook machinery, which relies on a
    ``ContextVar`` and is not traceable by ``torch.export``. So we run the
    decoder stack explicitly and return only the requested hidden states.

    ``hidden_states[k]`` is the output of decoder layer ``k - 1`` (index 0 is
    the embedding output), so we only run layers up to ``max(out_layers) - 1``
    and skip the final norm and the LM head entirely.
    """

    def __init__(
        self, text_encoder: torch.nn.Module, out_layers: tuple[int, ...],
    ) -> None:
        super().__init__()
        self.model = text_encoder.model  # Qwen3Model (no lm_head)
        self.out_layers = tuple(out_layers)
        self._needed_layers = {k - 1 for k in self.out_layers}
        self._last_layer = max(self._needed_layers)

    def forward(self, input_ids: torch.Tensor, attention_mask: torch.Tensor):
        from transformers.masking_utils import (
            create_causal_mask,
            create_sliding_window_causal_mask,
        )

        model = self.model
        inputs_embeds = model.embed_tokens(input_ids)
        position_ids = torch.arange(
            inputs_embeds.shape[1], device=inputs_embeds.device
        ).unsqueeze(0)

        mask_kwargs = {
            "config": model.config,
            "inputs_embeds": inputs_embeds,
            "attention_mask": attention_mask,
            "past_key_values": None,
            "position_ids": position_ids,
        }
        mask_mapping = {"full_attention": create_causal_mask(**mask_kwargs)}
        if model.has_sliding_layers:
            mask_mapping["sliding_attention"] = create_sliding_window_causal_mask(
                **mask_kwargs
            )

        position_embeddings = model.rotary_emb(inputs_embeds, position_ids)
        hidden_states = inputs_embeds
        captured: dict[int, torch.Tensor] = {}
        for i in range(self._last_layer + 1):
            hidden_states = model.layers[i](
                hidden_states,
                attention_mask=mask_mapping[model.config.layer_types[i]],
                position_embeddings=position_embeddings,
                position_ids=position_ids,
                past_key_values=None,
                use_cache=False,
            )
            if i in self._needed_layers:
                captured[i + 1] = hidden_states
        return tuple(captured[k] for k in self.out_layers)


class _Flux2VaeDecodeExportModule(torch.nn.Module):
    """Tensor-in/tensor-out view of the VAE decode path for export.

    Calls ``vae._decode`` directly to bypass the ``@apply_forward_hook``
    offload wrapper on ``decode``; ``use_tiling`` / ``use_slicing`` are off.
    """

    def __init__(self, vae: torch.nn.Module) -> None:
        super().__init__()
        self.vae = vae

    def forward(self, z: torch.Tensor) -> torch.Tensor:
        return self.vae._decode(z, return_dict=False)[0]


class _CompiledFlux2TextEncoderWrapper(torch.nn.Module):
    """Wraps the compiled text encoder with the Qwen3 forward signature.

    Reproduces the ``output.hidden_states[k]`` access the Flux2 pipeline relies
    on by placing each compiled hidden state back at its original layer index.
    """

    def __init__(
        self,
        compiled: torch.nn.Module,
        original: torch.nn.Module,
        *,
        out_layers: tuple[int, ...],
        dtype: torch.dtype,
        device: str | torch.device,
    ) -> None:
        import copy
        super().__init__()
        self.compiled = compiled
        self.config = copy.deepcopy(original.config)
        self.out_layers = tuple(out_layers)
        self.dtype = dtype
        self.device = torch.device(device)
        self._offload_marker = torch.nn.Parameter(
            torch.empty((), device=self.device, dtype=dtype), requires_grad=False
        )

    def _apply(self, fn):
        result = super()._apply(fn)
        marker = self._offload_marker
        self.device = marker.device
        if marker.is_floating_point():
            self.dtype = marker.dtype
        return result

    def forward(
        self,
        input_ids: torch.Tensor | None = None,
        attention_mask: torch.Tensor | None = None,
        output_hidden_states: bool = True,
        use_cache: bool = False,
        **kwargs,
    ):
        from transformers.modeling_outputs import BaseModelOutputWithPast

        outputs = self.compiled(input_ids.contiguous(), attention_mask.contiguous())
        # Only the indices in out_layers are ever read by the pipeline.
        hidden_states: list[torch.Tensor | None] = [None] * (max(self.out_layers) + 1)
        for layer_index, tensor in zip(self.out_layers, outputs):
            hidden_states[layer_index] = tensor
        return BaseModelOutputWithPast(
            last_hidden_state=outputs[-1],
            hidden_states=tuple(hidden_states),
        )


class _CompiledFlux2VaeWrapper(torch.nn.Module):
    """Wraps the compiled VAE decoder with the AutoencoderKLFlux2 decode API."""

    def __init__(
        self,
        compiled: torch.nn.Module,
        original: torch.nn.Module,
        *,
        dtype: torch.dtype,
        device: str | torch.device,
    ) -> None:
        super().__init__()
        self.compiled = compiled
        self.config = original.config
        # The pipeline un-normalizes latents with these BatchNorm stats before
        # calling decode, so the wrapper must keep them accessible.
        self.bn = original.bn
        self.dtype = dtype
        self.device = torch.device(device)
        self._offload_marker = torch.nn.Parameter(
            torch.empty((), device=self.device, dtype=dtype), requires_grad=False
        )

    def _apply(self, fn):
        result = super()._apply(fn)
        marker = self._offload_marker
        self.device = marker.device
        if marker.is_floating_point():
            self.dtype = marker.dtype
        return result

    def decode(self, z: torch.Tensor, return_dict: bool = True, generator=None):
        from diffusers.models.autoencoders.vae import DecoderOutput

        image = self.compiled(z.contiguous())
        if isinstance(image, (tuple, list)):
            image = image[0]
        if not return_dict:
            return (image,)
        return DecoderOutput(sample=image)

    def forward(self, sample: torch.Tensor, return_dict: bool = True, **kwargs):
        return self.decode(sample, return_dict=return_dict)


def build_flux2_dummy_inputs(
    pipe: Any,
    *,
    dtype: torch.dtype,
    device: torch.device,
    height: int = FIXED_HEIGHT,
    width: int = FIXED_WIDTH,
    max_sequence_length: int = FIXED_MAX_SEQUENCE_LENGTH,
    seed: int = FIXED_SEED,
) -> dict[str, Any]:
    """Build fixed dummy transformer inputs for the TRT compile/export step."""
    generator = torch.Generator(device=device).manual_seed(seed)
    num_channels_latents = pipe.transformer.config.in_channels // 4
    hidden_states, img_ids = pipe.prepare_latents(
        batch_size=1,
        num_latents_channels=num_channels_latents,
        height=height,
        width=width,
        dtype=dtype,
        device=device,
        generator=generator,
        latents=None,
    )
    encoder_hidden_states = torch.randn(
        (1, max_sequence_length, pipe.transformer.config.joint_attention_dim),
        dtype=dtype,
        device=device,
    )
    txt_ids = pipe._prepare_text_ids(encoder_hidden_states).to(device)
    timestep = torch.ones((1,), dtype=dtype, device=device)
    guidance = torch.zeros((1,), dtype=dtype, device=device)
    return {
        "hidden_states": hidden_states.contiguous(),
        "encoder_hidden_states": encoder_hidden_states.contiguous(),
        "timestep": timestep.contiguous(),
        "img_ids": img_ids.contiguous(),
        "txt_ids": txt_ids.contiguous(),
        "guidance": guidance.contiguous(),
        "joint_attention_kwargs": {},
        "return_dict": False,
    }


def build_textencoder_dummy_inputs(
    pipe: Any,
    *,
    dtype: torch.dtype,
    device: torch.device,
    max_sequence_length: int = FIXED_MAX_SEQUENCE_LENGTH,
    prompt: str = "a photo",
) -> dict[str, Any]:
    """Build fixed dummy text-encoder inputs for the TRT compile/export step.

    Mirrors the pipeline's tokenization (chat template + max-length padding) so
    the exported graph traces the padded-attention path; the resulting engine
    is then correct for any prompt of the same length.
    """
    del dtype  # input_ids / attention_mask are integer tensors
    tokenizer = pipe.tokenizer
    text = tokenizer.apply_chat_template(
        [{"role": "user", "content": prompt}],
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
    return {
        "input_ids": inputs["input_ids"].to(device),
        "attention_mask": inputs["attention_mask"].to(device),
    }


def build_vae_dummy_inputs(
    pipe: Any,
    *,
    dtype: torch.dtype,
    device: torch.device,
    height: int = FIXED_HEIGHT,
    width: int = FIXED_WIDTH,
) -> dict[str, Any]:
    """Build a fixed dummy latent for the VAE decode compile/export step.

    The shape matches the pipeline's tensor right before ``vae.decode``:
    ``(1, latent_channels, latent_h, latent_w)``.
    """
    vae_scale_factor = pipe.vae_scale_factor
    latent_channels = pipe.vae.config.latent_channels
    latent_height = 2 * (int(height) // (vae_scale_factor * 2))
    latent_width = 2 * (int(width) // (vae_scale_factor * 2))
    z = torch.randn(
        (1, latent_channels, latent_height, latent_width),
        dtype=dtype,
        device=device,
    )
    return {"z": z.contiguous()}


def _prepare_torchtrt_cache(cache_dir: str | Path | None) -> _TorchTRTCachePaths:
    if cache_dir is None:
        temporary_dir = tempfile.TemporaryDirectory(prefix="flux2_torchtrt_")
        return _TorchTRTCachePaths(
            cache_dir=None,
            runtime_cache_path=Path(temporary_dir.name) / "runtime_cache.bin",
            engine_cache_dir=None,
            temporary_dir=temporary_dir,
        )

    cache_dir = Path(cache_dir)
    cache_dir.mkdir(parents=True, exist_ok=True)
    engine_cache_dir = cache_dir / "engine_cache"
    engine_cache_dir.mkdir(parents=True, exist_ok=True)
    return _TorchTRTCachePaths(
        cache_dir=cache_dir,
        runtime_cache_path=cache_dir / "runtime_cache.bin",
        engine_cache_dir=engine_cache_dir,
    )


def _base_compile_options(weight_streaming: bool) -> dict[str, Any]:
    return {
        "pass_through_build_failures": True,
        "truncate_double": True,
        "min_block_size": 1, 
        # "cuda_graph_strategy": "whole_graph_capture" if not weight_streaming else "",
        "enable_weight_streaming": weight_streaming,
        "offload_module_to_cpu":True,
    }


def _compile_options_for_cache(cache: _TorchTRTCachePaths, weight_streaming: bool) -> dict[str, Any]:
    options = _base_compile_options(weight_streaming)
    options["runtime_cache_path"] = str(cache.runtime_cache_path)
    if cache.cache_dir is not None and cache.engine_cache_dir is not None:
        options.update(
            {
                "cache_built_engines": True,
                "reuse_cached_engines": True,
                "engine_cache_dir": str(cache.engine_cache_dir),
                "engine_cache_size": 8 * 1024**3,
            }
        )
    return options


def _engine_total_vram_required(compiled) -> int:
    """VRAM (bytes) the compiled module's TensorRT engine(s) require: serialized
    engine weights + execution-context device memory (activation scratch),
    summed across engine blocks. Best-effort — returns 0 if undeterminable.
    """
    try:
        import tensorrt_rtx as trt
    except Exception:
        return 0

    total = 0
    named = compiled.named_modules() if hasattr(compiled, "named_modules") else []
    for _, sub in named:
        eng = getattr(sub, "serialized_engine", None)
        if not eng:
            continue
        total += len(eng)  # serialized weights
        try:
            runtime = trt.Runtime(trt.Logger(trt.Logger.ERROR))
            engine = runtime.deserialize_cuda_engine(eng)
            if engine is not None:
                dev = getattr(engine, "device_memory_size_v2", None)
                if dev is None:
                    dev = getattr(engine, "device_memory_size", 0)
                total += dev  # execution-context scratch
            del engine, runtime
        except Exception:
            pass  # weights already counted; skip scratch for this engine
    import gc
    gc.collect()
    torch.cuda.empty_cache()
    return total


def compile_transformer_export(
    transformer: torch.nn.Module,
    sample_inputs: Mapping[str, Any],
    *,
    dtype: torch.dtype,
    device: str | torch.device,
    cache_dir: str | Path | None = None,
    use_cuda_graphs: bool = True,
    quant = "fp8", 
    gpu_mode = "direct"
) -> torch.nn.Module:
    """Export + compile the NVFP4 transformer with Torch-TensorRT-RTX.

    When `use_cuda_graphs` is set the returned wrapper replays a captured CUDA
    graph on every denoising step (fixed shapes), cutting kernel-launch overhead.
    """
    from modelopt.torch.quantization.utils import export_torch_mode
    from torch.export._trace import _export
    import torch_tensorrt

    logging.getLogger("torch_tensorrt.dynamo.conversion.converter_utils").setLevel(logging.ERROR)

    if quant == "nvfp4":
        register_nvfp4_weight_converter()
        patch_modelopt_nvfp4_export_bridge(dtype)
        patch_torchtrt_constant_folding_for_nvfp4()
    weight_streaming = True
    # cache = _prepare_torchtrt_cache(cache_dir)
    options = _base_compile_options(weight_streaming=weight_streaming)

    # Lower the allowed execution memory budget by 50% to force weight streaming
    # TRT will allocate activations in VRAM and stream the rest of the weights from CPU

    log.info(f"[trt-rtx] transformer with {options}")
    export_kwargs = dict(sample_inputs)
    original = transformer.eval()
    with export_torch_mode():
        exported_program = _export(
            original,
            args=(),
            kwargs=export_kwargs,
            strict=False,
            prefer_deferred_runtime_asserts_over_guards=True,
        )

    compiled = torch_tensorrt.dynamo.compile(
        exported_program,
        inputs=export_kwargs,
        **options,
    )
    compiled.device = torch.device(device)
    del exported_program

    log.info("[tensorrt_rtx] Compiled Transformer")
    wrapper = _CompiledFlux2TransformerWrapper(
        compiled,
        original,
        dtype=dtype,
        device=device,
        # temporary_cache_dir=cache.temporary_dir,
        weight_streaming=weight_streaming,
    )
    del original
    
    import gc; gc.collect(); torch.cuda.empty_cache()
    wrapper.total_vram_required = _engine_total_vram_required(compiled)
    return wrapper


@contextmanager
def _quiet_constant_fold_warnings():
    """Silence Torch-TensorRT's benign "both operands ... are constant" notices.

    Static-shape export of ops such as ``F.interpolate(scale_factor=...)`` (the
    VAE decoder upsamplers) leaves shape arithmetic whose operands are all
    compile-time constants. TensorRT folds it at build time, so the per-op
    UserWarning is log noise rather than a real issue.
    """
    import warnings

    with warnings.catch_warnings():
        warnings.filterwarnings(
            "ignore",
            message="Both operands of the binary elementwise op",
            category=UserWarning,
        )
        yield


def compile_textencoder_export(
    text_encoder: torch.nn.Module,
    sample_inputs: Mapping[str, Any],
    *,
    out_layers: tuple[int, ...],
    dtype: torch.dtype,
    device: str | torch.device,
) -> torch.nn.Module:
    """Export + compile the Qwen3 text encoder with Torch-TensorRT-RTX."""
    from torch.export._trace import _export
    import torch_tensorrt

    export_module = _Flux2TextEncoderExportModule(text_encoder, out_layers).eval()
    args = (sample_inputs["input_ids"], sample_inputs["attention_mask"])
    weight_streaming = False
    options = _base_compile_options(weight_streaming=weight_streaming)
    options["enabled_precisions"] = {torch.float16, torch.bfloat16}

    log.info(f"[trt-rtx] text_encoder with {options}")
    exported_program = _export(
        export_module,
        args=args,
        kwargs={},
        strict=False,
        prefer_deferred_runtime_asserts_over_guards=True,
    )
    with _quiet_constant_fold_warnings():
        compiled = torch_tensorrt.dynamo.compile(
            exported_program,
            inputs=list(args),
            **options,
        )
        compiled.device = torch.device(device)
        del exported_program
        import gc; gc.collect(); torch.cuda.empty_cache()

    log.info("[tensorrt_rtx] Compiled Text Encoder")
    wrapper = _CompiledFlux2TextEncoderWrapper(
        compiled,
        text_encoder,
        out_layers=out_layers,
        dtype=dtype,
        device=device,
    )
    del text_encoder
    import gc; gc.collect(); torch.cuda.empty_cache()
    wrapper.total_vram_required = _engine_total_vram_required(compiled)
    return wrapper


def compile_vae_export(
    vae: torch.nn.Module,
    sample_inputs: Mapping[str, Any],
    *,
    dtype: torch.dtype,
    device: str | torch.device,
) -> torch.nn.Module:
    """Export + compile the VAE decode path with Torch-TensorRT-RTX."""
    from torch.export._trace import _export
    import torch_tensorrt

    export_module = _Flux2VaeDecodeExportModule(vae).eval()
    args = (sample_inputs["z"],)
    weight_streaming = False
    options = _base_compile_options(weight_streaming=weight_streaming)
    options["enabled_precisions"] = {torch.float16, torch.bfloat16}

    log.info(f"[trt-rtx] vae with {options}")
    exported_program = _export(
        export_module,
        args=args,
        kwargs={},
        strict=False,
        prefer_deferred_runtime_asserts_over_guards=True,
    )
    with _quiet_constant_fold_warnings():
        compiled = torch_tensorrt.dynamo.compile(
            exported_program,
            inputs=list(args),
            **options,
        )
        compiled.device = torch.device(device)
        del exported_program
        import gc; gc.collect(); torch.cuda.empty_cache()

    log.info("[tensorrt_rtx] Compiled VAE")
    wrapper = _CompiledFlux2VaeWrapper(
        compiled,
        vae,
        dtype=dtype,
        device=device,
    )
    del vae
    import gc; gc.collect(); torch.cuda.empty_cache()
    wrapper.total_vram_required = _engine_total_vram_required(compiled)
    return wrapper


def _apply_flux2_transformer(
    runtime: "Runtime", params: dict[str, Any],
) -> dict[str, Any]:
    height = int(params.get("height", FIXED_HEIGHT))
    width = int(params.get("width", FIXED_WIDTH))
    max_sequence_length = int(params.get("max_sequence_length", FIXED_MAX_SEQUENCE_LENGTH))
    seed = int(params.get("seed", FIXED_SEED))
    cache_dir = params.get("cache_dir")
    use_cuda_graphs = bool(params.get("use_cuda_graphs", True))
    gpu_mode = getattr(runtime, "gpu_mode", "")

    sample_inputs = build_flux2_dummy_inputs(
        runtime.pipe,
        dtype=runtime.dtype,
        device=runtime.device,
        height=height,
        width=width,
        max_sequence_length=max_sequence_length,
        seed=seed,
    )
    # FP8 ModelOpt uses native TensorRT FP8 Q/DQ (no NVFP4 dequant patches).
    quant = (getattr(runtime, "quantization", "") or "").strip().lower()
 
    compiled_mod = compile_transformer_export(
        runtime.pipe.transformer.eval(),
        sample_inputs,
        dtype=runtime.dtype,
        device=runtime.device,
        cache_dir=cache_dir,
        use_cuda_graphs=use_cuda_graphs,
        quant=quant,
        gpu_mode=gpu_mode
    )
    runtime.pipe.transformer = compiled_mod

    import gc
    gc.collect()
    torch.cuda.empty_cache()
    return {
        "compiled": "transformer",
        "height": height,
        "width": width,
        "max_sequence_length": max_sequence_length,
        "seed": seed,
        "cache_dir": str(cache_dir) if cache_dir is not None else None,
        "use_cuda_graphs": use_cuda_graphs,
        "quant": quant
    }


def _apply_generic(runtime: "Runtime", params: dict[str, Any]) -> dict[str, Any]:
    """Export + compile text_encoder and vae via torch_tensorrt.dynamo.compile.

    The submodules are ModelOpt/TensorRT targets, not valid eager modules, so a
    compile failure is raised rather than silently falling back to eager. The
    caller (``apply``) is responsible for making every target GPU-resident on a
    single device before this runs (see its offload handling). The legacy
    torch.compile path is preserved as ``_apply_generic_old``.
    """
    height = int(params.get("height", FIXED_HEIGHT))
    width = int(params.get("width", FIXED_WIDTH))
    max_sequence_length = int(
        params.get("max_sequence_length", FIXED_MAX_SEQUENCE_LENGTH)
    )
    out_layers = tuple(params.get("text_encoder_out_layers", (9, 18, 27)))
    dtype = runtime.dtype
    device = runtime.device

    compiled: list[str] = []

    total_memory_gb = runtime.hardware.get("total_memory_gb", 0.0)
    text_encoder = getattr(runtime.pipe, "text_encoder", None)
    if text_encoder is not None and total_memory_gb >= 16:
        sample_inputs = build_textencoder_dummy_inputs(
            runtime.pipe,
            dtype=dtype,
            device=device,
            max_sequence_length=max_sequence_length,
        )
        runtime.pipe.text_encoder = compile_textencoder_export(
            text_encoder.eval(),
            sample_inputs,
            out_layers=out_layers,
            dtype=dtype,
            device=device,
        )
        compiled.append("text_encoder")
    elif text_encoder is not None:
        log.info(
            "[trt-rtx] skipping text_encoder compile (%.1f GB VRAM < 16 GB)",
            total_memory_gb,
        )

    vae = getattr(runtime.pipe, "vae", None)
    if vae is not None:
        sample_inputs = build_vae_dummy_inputs(
            runtime.pipe,
            dtype=dtype,
            device=device,
            height=height,
            width=width,
        )
        runtime.pipe.vae = compile_vae_export(
            vae.eval(),
            sample_inputs,
            dtype=dtype,
            device=device,
        )
        compiled.append("vae")

    import gc
    gc.collect()
    torch.cuda.empty_cache()

    log.warning(f"[trt-rtx] Compiled {compiled}")
    return {"compiled": compiled, "backend": "torch_tensorrt.dynamo"}


def _dedup_trt_conversion_warnings() -> None:
    from opts.nvidia_quantize import _DedupLogFilter

    target = logging.getLogger("torch_tensorrt [TensorRT Conversion Context]")
    if not any(isinstance(f, _DedupLogFilter) for f in target.filters):
        target.addFilter(_DedupLogFilter())


def _is_modelopt_quantized(transformer: Any) -> bool:
    """True for a ModelOpt-quantized (fp8/nvfp4) transformer.

    Both ModelOpt loaders set `hf_quantizer`; a plain bf16 transformer has
    neither that attribute nor `weight_quantizer` submodules, so it is left
    uncompiled here.
    """
    if transformer is None:
        return False
    if getattr(transformer, "hf_quantizer", None) is not None:
        return True
    return any(hasattr(m, "weight_quantizer") for m in transformer.modules())


@register_opt("tensorrt_rtx")
def apply(runtime: "Runtime", params: dict[str, Any]) -> dict[str, Any]:
    """Compile pipe submodules via Torch-TensorRT-RTX (additive).

    Compiles text_encoder + vae via torch_tensorrt.dynamo.compile, and
    additionally exports + compiles the transformer (with CUDA graphs) when it
    is a ModelOpt-quantized (fp8/nvfp4) FLUX.2 transformer. Returns a combined
    info dict: {"submodules": <generic result>, "transformer": <result>}.
    """
    if not torch.cuda.is_available():
        log.warning("tensorrt_rtx: skipping (CUDA not available)")
        return {"skipped": True, "reason": "no cuda"}

    try:
        import torch_tensorrt  # noqa: F401
    except (ImportError, OSError) as exc:
        log.warning(
            "tensorrt_rtx: skipping (torch_tensorrt unavailable: %s)", exc
        )
        return {"skipped": True, "reason": f"torch_tensorrt unavailable: {exc}"}

    if runtime.pipe is None:
        raise RuntimeError("tensorrt_rtx: runtime.pipe is None — load first")

    _dedup_trt_conversion_warnings()

    # Torch-TensorRT builds GPU engines, so every target must be GPU-resident on
    # a single device while it is exported/compiled. Under cpu_offload /
    # sequential_offload the loader left the modules on CPU behind accelerate
    # hooks (which also orphan themselves once we swap in compiled wrappers).
    # Detach those hooks and bring the pipe onto the GPU to compile, then
    # re-apply the offload recipe so the hooks re-attach to the compiled
    # wrappers — their ``_offload_marker`` lets accelerate move them like any
    # other module.
    gpu_mode = (getattr(runtime, "gpu_mode", "") or "").strip().lower()
    offloaded = gpu_mode in ("cpu_offload", "sequential_offload")
    if offloaded:
        runtime.pipe.remove_all_hooks()
        runtime.pipe.to(runtime.device)

    info: dict[str, Any] = {"submodules": _apply_generic(runtime, params)}

    # transformer: only the FLUX.2-specific export/compile path, and only when
    # it is a ModelOpt-quantized (fp8/nvfp4) Flux2 transformer; a plain bf16
    # transformer is left uncompiled (it is a valid eager module). A ModelOpt
    # transformer is NOT valid eager, so a compile failure is raised here rather
    # than swallowed into an eager fallback.
    transformer = getattr(runtime.pipe, "transformer", None)
    if _is_modelopt_quantized(transformer):
        info["transformer"] = _apply_flux2_transformer(runtime, params)
    else:
        info["transformer"] = {
            "skipped": True,
            "reason": "transformer not ModelOpt-quantized",
        }

    if offloaded:
        from helpers import apply_gpu_mode

        apply_gpu_mode(
            runtime.pipe,
            device=runtime.device,
            gpu_mode=gpu_mode,
            device_id=(runtime.device.index or 0),
        )

    return info
