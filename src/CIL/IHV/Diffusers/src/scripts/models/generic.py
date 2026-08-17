"""Generic loader for any diffusers pipeline class.

Three paths, in this order:

  1. **Assembler** — when `component_files["__pipeline_class__"]` and
     `component_files["__hf_base_id__"]` are both set: introspect the
     pipeline class's `__init__` signature, build each role from the
     matching local file (when listed in `component_files`) or from the
     HF base id snapshot, and construct the pipeline directly with the
     resulting kwargs. Powers Z-Image-Turbo and other multi-component
     setups that mix safetensors + HF cache.

  2. **Single-file fallback** — when the main path is a `.safetensors`
     / `.ckpt` and no assembler context was provided: try a known list
     of `from_single_file` candidates with auto-discovered companion
     components.

  3. **`from_pretrained`** — otherwise: route directly to a HF
     repo / local diffusers directory. The class is picked from
     `__pipeline_class__` if set, else `DiffusionPipeline`.

Companion-component `.safetensors` next to a single-file checkpoint are
auto-discovered by filename pattern in path 2.
"""
from __future__ import annotations

import importlib
import inspect
import logging
import os
import re
from typing import Any, TYPE_CHECKING

from helpers import apply_gpu_mode, hf_kwargs, resolve_model_ref

if TYPE_CHECKING:
    from runtime import Runtime

log = logging.getLogger(__name__)


_COMPONENT_PATTERNS = {
    "text_encoder":   re.compile(r"(text.?encoder|clip(?!_v)|qwen|t5|bert)", re.I),
    "text_encoder_2": re.compile(r"(text.?encoder.?2|clip_l)", re.I),
    "vae":            re.compile(r"vae", re.I),
}

_SINGLE_FILE_FALLBACKS: tuple[str, ...] = (
    "DiffusionPipeline",
    "ZImagePipeline",
    "StableDiffusionPipeline",
    "StableDiffusionXLPipeline",
    "FluxPipeline",
)

_PIPELINE_CLASS_KEY = "__pipeline_class__"
_HF_BASE_ID_KEY = "__hf_base_id__"


def load(runtime: "Runtime") -> Any:
    pipeline_class = (runtime.component_files.get(_PIPELINE_CLASS_KEY) or "").strip()
    hf_base_id = (runtime.component_files.get(_HF_BASE_ID_KEY) or "").strip()

    if pipeline_class and hf_base_id:
        pipe = _load_via_components(runtime, pipeline_class, hf_base_id)
    else:
        ref, auto_download, cache_dir = resolve_model_ref(
            runtime.model_path,
            default_model_id=None,
            download_cache=runtime.download_cache,
        )
        runtime.model_ref = ref
        runtime.auto_download = auto_download

        base_kwargs = hf_kwargs(
            dtype=runtime.dtype, auto_download=auto_download,
            cache_dir=cache_dir,
        )
        if os.path.isfile(ref) and ref.endswith((".safetensors", ".ckpt")):
            pipe = _load_single_file(runtime, ref, base_kwargs)
        else:
            pipe = _load_from_pretrained(runtime, ref, base_kwargs)

    if hasattr(pipe, "set_progress_bar_config"):
        pipe.set_progress_bar_config(disable=True)
    if runtime.device is not None:
        device_id = (runtime.device.index
                     if runtime.device.index is not None else 0)
        apply_gpu_mode(pipe, device=runtime.device,
                       gpu_mode=runtime.gpu_mode, device_id=device_id)
    return pipe


def _load_from_pretrained(
    runtime: "Runtime", ref: str, base_kwargs: dict[str, Any],
) -> Any:
    cls = _resolve_pipeline_class(runtime)
    return cls.from_pretrained(ref, **base_kwargs)


def _load_single_file(
    runtime: "Runtime", checkpoint_path: str, base_kwargs: dict[str, Any],
) -> Any:
    model_dir = os.path.dirname(checkpoint_path)
    component_files = _resolve_component_paths(
        model_dir,
        main_checkpoint=checkpoint_path,
        explicit=runtime.component_files,
    )

    declared_cls = _resolve_pipeline_class(runtime, default=None)
    candidates: list[Any] = []
    if declared_cls is not None:
        candidates.append(declared_cls)
    candidates.extend(_collect_fallbacks(exclude=declared_cls))

    errors: list[str] = []
    for cls in candidates:
        if not hasattr(cls, "from_single_file"):
            continue
        try:
            return cls.from_single_file(
                checkpoint_path, **base_kwargs, **component_files,
            )
        except Exception as exc:
            errors.append(f"{cls.__name__}: {exc}")

    detail = "\n  ".join(errors) or "no candidate class supports from_single_file"
    raise RuntimeError(
        f"could not load {checkpoint_path}:\n  {detail}"
    )


def _resolve_pipeline_class(
    runtime: "Runtime", *, default: Any | None = ...,
) -> Any:
    name = (runtime.component_files.get(_PIPELINE_CLASS_KEY) or "").strip()
    if not name:
        if default is ...:
            from diffusers import DiffusionPipeline
            return DiffusionPipeline
        return default
    return _resolve_pipeline_class_by_name(name)


def _resolve_pipeline_class_by_name(name: str) -> Any:
    diffusers = importlib.import_module("diffusers")
    cls = getattr(diffusers, name, None)
    if cls is None:
        raise ValueError(
            f"pipeline_class {name!r} not found in diffusers module."
        )
    return cls


def _collect_fallbacks(*, exclude: Any | None) -> list[Any]:
    diffusers = importlib.import_module("diffusers")
    out: list[Any] = []
    for name in _SINGLE_FILE_FALLBACKS:
        cls = getattr(diffusers, name, None)
        if cls is None or cls is exclude:
            continue
        out.append(cls)
    return out


def _resolve_component_paths(
    model_dir: str,
    *,
    main_checkpoint: str,
    explicit: dict[str, str] | None = None,
) -> dict[str, str]:
    """Build a {role: absolute path} dict for companion-component files."""
    resolved: dict[str, str] = {}

    if explicit:
        for role, filename in explicit.items():
            if role.startswith("__"):
                continue
            path = os.path.join(model_dir, filename)
            if os.path.exists(path):
                resolved[role] = path

    main_name = os.path.basename(main_checkpoint)
    already = {os.path.basename(p) for p in resolved.values()}

    for fname in sorted(os.listdir(model_dir)):
        if fname == main_name or fname in already:
            continue
        if not fname.endswith((".safetensors", ".ckpt")):
            continue
        for role, pattern in _COMPONENT_PATTERNS.items():
            if role not in resolved and pattern.search(fname):
                resolved[role] = os.path.join(model_dir, fname)
                already.add(fname)
                break

    return resolved


def _load_via_components(
    runtime: "Runtime", pipeline_class_name: str, hf_base_id: str,
) -> Any:
    """Build a pipeline by introspecting its __init__ signature.

    Each role in the signature is fed from one of:
      - `runtime.component_files[role]` if set and the file exists
        (single-file safetensors → `from_single_file` with the HF
        config for LDM conversion; otherwise treat as a directory of
        diffusers shards / config and load via `from_pretrained`)
      - the matching subfolder of the HF snapshot otherwise

    Tokenizers are special-cased: AutoTokenizer with `subfolder=`
    needs a real config.json that diffusers' tokenizer subfolders
    don't have, so we resolve the snapshot path directly.
    """
    pipe_cls = _resolve_pipeline_class_by_name(pipeline_class_name)
    runtime.model_ref = runtime.model_path or hf_base_id

    sig = inspect.signature(pipe_cls.__init__)
    roles: list[tuple[str, Any]] = []
    for name, param in sig.parameters.items():
        if name == "self":
            continue
        ann = (None if param.annotation is inspect.Parameter.empty
               else param.annotation)
        roles.append((name, ann))

    component_files = {
        k: v for k, v in (runtime.component_files or {}).items()
        if not k.startswith("__")
    }

    # If the EP/standalone config doesn't pin the backbone, the main
    # model_path (likely a .safetensors checkpoint) feeds it.
    backbone_role = _pick_backbone_role(roles)
    if (runtime.model_path
            and backbone_role
            and backbone_role not in component_files
            and os.path.isfile(runtime.model_path)
            and runtime.model_path.endswith((".safetensors", ".ckpt"))):
        component_files[backbone_role] = runtime.model_path

    kwargs: dict[str, Any] = {}
    for role, role_type in roles:
        kwargs[role] = _load_one_component(
            role=role, role_type=role_type,
            file_path=component_files.get(role),
            hf_base_id=hf_base_id,
            torch_dtype=runtime.dtype,
        )

    return pipe_cls(**kwargs)


def _pick_backbone_role(roles: list[tuple[str, Any]]) -> str | None:
    for name, role_type in roles:
        type_name = getattr(role_type, "__name__", "") or ""
        if "Transformer" in type_name or "UNet" in type_name:
            return name
    for name, _ in roles:
        if name in ("transformer", "unet"):
            return name
    return None


def _load_one_component(
    *, role: str, role_type: Any, file_path: str | None,
    hf_base_id: str, torch_dtype: Any,
) -> Any:
    type_name = getattr(role_type, "__name__", "") if role_type else ""

    if "Tokenizer" in type_name:
        return _load_tokenizer(role, hf_base_id)

    if file_path and os.path.exists(file_path):
        if os.path.isfile(file_path) and role_type is not None and hasattr(
                role_type, "from_single_file"):
            kwargs: dict[str, Any] = {"torch_dtype": torch_dtype}
            if hf_base_id:
                kwargs["config"] = hf_base_id
                kwargs["subfolder"] = role
                kwargs["local_files_only"] = True
            return role_type.from_single_file(file_path, **kwargs)
        if os.path.isfile(file_path):
            return _load_pretrained_from_file(
                role, role_type, hf_base_id, file_path, torch_dtype)
        # Local directory with diffusers shards / config.
        if hasattr(role_type, "from_pretrained"):
            return role_type.from_pretrained(
                file_path, torch_dtype=torch_dtype, local_files_only=True)

    return _load_pretrained_from_hf(role, role_type, hf_base_id, torch_dtype)


def _load_tokenizer(role: str, hf_base_id: str) -> Any:
    """AutoTokenizer with subfolder= fails offline (looks for config.json
    instead of tokenizer_config.json). Snapshot-resolve the dir directly."""
    from huggingface_hub import snapshot_download
    from transformers import AutoTokenizer

    if not hf_base_id:
        raise RuntimeError(
            f"cannot resolve tokenizer {role!r} without hf_base_id")
    snapshot = snapshot_download(hf_base_id, local_files_only=True)
    local_dir = os.path.join(snapshot, role)
    return AutoTokenizer.from_pretrained(local_dir, local_files_only=True)


def _resolve_pretrained_class(role_type: Any, hf_base_id: str, role: str):
    from transformers import AutoConfig

    if not hf_base_id:
        raise RuntimeError(
            f"component {role!r} requires hf_base_id (need a config).")
    config = AutoConfig.from_pretrained(
        hf_base_id, subfolder=role, local_files_only=True)

    if role_type is not None:
        type_name = getattr(role_type, "__name__", "") or ""
        if type_name and type_name not in (
                "PreTrainedModel", "PretrainedConfig"):
            return role_type, config

    from transformers.models.auto.modeling_auto import MODEL_MAPPING
    cls = MODEL_MAPPING.get(type(config), None)
    if cls is None:
        raise RuntimeError(
            f"no transformers AutoModel mapping for config "
            f"{type(config).__name__} (component {role!r})")
    return cls, config


def _load_pretrained_from_file(
    role: str, role_type: Any, hf_base_id: str, file_path: str,
    torch_dtype: Any,
) -> Any:
    from accelerate import init_empty_weights
    from safetensors.torch import load_file

    cls, config = _resolve_pretrained_class(role_type, hf_base_id, role)
    with init_empty_weights():
        model = cls(config)

    state_dict = load_file(file_path)
    # *ForCausalLM weights are keyed `model.<bare>`; strip to match base.
    target_keys = set(model.state_dict().keys())
    if (target_keys
            and not any(k.startswith("model.") for k in target_keys)
            and any(k.startswith("model.") for k in state_dict)):
        state_dict = {k[len("model."):]: v for k, v in state_dict.items()
                      if k.startswith("model.")}

    model.load_state_dict(state_dict, strict=False, assign=True)
    return model.to(dtype=torch_dtype)


def _load_pretrained_from_hf(
    role: str, role_type: Any, hf_base_id: str, torch_dtype: Any,
) -> Any:
    import torch

    if not hf_base_id:
        raise RuntimeError(
            f"component {role!r} was not provided locally and no "
            f"hf_base_id is set.")

    kwargs: dict[str, Any] = {"subfolder": role, "local_files_only": True}
    # ConfigMixin-only classes (schedulers) reject torch_dtype.
    if role_type is None or (isinstance(role_type, type)
                             and issubclass(role_type, torch.nn.Module)):
        kwargs["torch_dtype"] = torch_dtype

    if role_type is None:
        from diffusers import DiffusionPipeline
        return DiffusionPipeline.from_pretrained(hf_base_id, **kwargs)

    type_name = getattr(role_type, "__name__", "") or ""
    if type_name in ("PreTrainedModel", "PretrainedConfig"):
        from transformers import AutoModel
        return AutoModel.from_pretrained(hf_base_id, **kwargs)

    if hasattr(role_type, "from_pretrained"):
        return role_type.from_pretrained(hf_base_id, **kwargs)

    raise RuntimeError(
        f"component {role!r} ({type_name}) has no from_pretrained loader")
