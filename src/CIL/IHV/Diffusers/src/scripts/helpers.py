"""Diffusers-pipeline helpers shared by all diffusers-based runtimes.

No inheritance. Each function does one thing: classify a model
reference (HF id vs single-file vs directory); resolve a model
reference to a (ref, cache, auto-download) tuple; build the kwargs
dict for `from_pretrained` / `from_single_file`; load a pipeline;
build the kwargs for `pipe(...)`; apply a gpu_mode recipe; log
component placement; attach per-submodule timing hooks.
"""
from __future__ import annotations

import logging
import os
import sys
from contextlib import contextmanager
from pathlib import Path
from typing import Any, Iterable, Iterator

import torch

from runtime import RunSpec
from timing.hooks import ModuleTimingHooks
from timing.timer import Timer

log = logging.getLogger(__name__)


_DEFAULT_AUTO_CACHE = os.environ.get(
    "MLPERF_DIFFUSERS_CACHE",
    str(Path.cwd() / ".cache" / "models"),
)


def is_frozen_or_embedded() -> bool:
    """True in a PyInstaller bundle (`sys.frozen`) or the C++-embedded
    interpreter (`sys._diffusers_embedded`): no spawnable `sys.executable`."""
    return bool(getattr(sys, "frozen", False)
                or getattr(sys, "_diffusers_embedded", False))


def is_hf_id(model_path: str) -> bool:
    """True if `model_path` looks like a HuggingFace `org/name` repo id.

    Used to skip `os.path.abspath()` on values that are not local paths.
    A repo id has exactly one `/`, no backslash, no leading absolute
    marker, no `.safetensors`/`.ckpt` suffix, and does not exist on disk.
    An optional ``@revision`` suffix is tolerated.
    """
    if not model_path:
        return False
    if os.path.isabs(model_path) or os.path.exists(model_path):
        return False
    if model_path.endswith((".safetensors", ".ckpt")):
        return False
    if "\\" in model_path:
        return False
    return model_path.split("@", 1)[0].count("/") == 1


def resolve_model_ref(
    model_path: str,
    *,
    default_model_id: str | None,
    download_cache: str | None,
) -> tuple[str, bool, str | None]:
    """Return (ref, auto_download, cache_dir) for a diffusers/HF-style model.

    If `model_path` is set, use it as the ref and only auto-download when
    `download_cache` is provided. If empty, fall back to `default_model_id`
    (auto-download to `download_cache` or the package default cache).
    """
    path = (model_path or "").strip()
    if path:
        return path, (download_cache is not None), download_cache
    if not default_model_id:
        raise ValueError(
            "no model_path provided and no default_model_id; "
            "set model_path or pick a model with a registered default id."
        )
    log.info(
        "model_path empty — auto-downloading default model id %s",
        default_model_id,
    )
    cache = download_cache or _DEFAULT_AUTO_CACHE
    return default_model_id, True, cache


def hf_kwargs(
    *,
    dtype: torch.dtype,
    auto_download: bool,
    cache_dir: str | None,
) -> dict[str, Any]:
    kw: dict[str, Any] = {"torch_dtype": dtype}
    if not auto_download:
        kw["local_files_only"] = True
        return kw
    cd = Path(cache_dir or _DEFAULT_AUTO_CACHE).expanduser()
    cd.mkdir(parents=True, exist_ok=True)
    kw["cache_dir"] = str(cd)
    kw["local_files_only"] = False
    return kw


def load_diffusers_pipeline(
    cls: Any,
    ref: str,
    *,
    dtype: torch.dtype,
    auto_download: bool,
    cache_dir: str | None,
    device: torch.device | None = None,
    extra_kwargs: dict[str, Any] | None = None,
    move_to_device: bool = True,
) -> Any:
    """Load a diffusers pipeline from a HF id, local dir, or .safetensors file."""
    base_kwargs = hf_kwargs(
        dtype=dtype, auto_download=auto_download, cache_dir=cache_dir,
    )
    if extra_kwargs:
        base_kwargs.update(extra_kwargs)

    ref_path = Path(ref)
    is_hf_ref = is_hf_id(ref)
    if not auto_download and not is_hf_ref and not ref_path.exists():
        raise FileNotFoundError(
            f"model_path missing: {ref}. Pass a download cache, or leave "
            "model_path empty to auto-download the default."
        )

    if not is_hf_ref and ref_path.is_file() and ref_path.suffix == ".safetensors":
        pipe = cls.from_single_file(str(ref_path), **base_kwargs)
    else:
        pipe = cls.from_pretrained(ref, **base_kwargs)

    if move_to_device and device is not None:
        pipe = pipe.to(device)
    if hasattr(pipe, "set_progress_bar_config"):
        pipe.set_progress_bar_config(disable=True)
    return pipe


def diffusers_call_kwargs(
    run: RunSpec,
    *,
    pipe: Any = None,
    generator: torch.Generator | None = None,
    drop: Iterable[str] = (),
) -> dict[str, Any]:
    """Build the keyword-argument dict for `pipe(**kwargs)`.

    When `pipe` is provided, kwargs are filtered against the pipeline's
    `__call__` signature so unsupported args (e.g. negative_prompt on
    FLUX) are silently dropped. This replaces hardcoded drop lists.
    """
    kw: dict[str, Any] = dict(
        prompt=run.prompt,
        width=run.width,
        height=run.height,
        num_inference_steps=run.steps,
        guidance_scale=run.guidance_scale,
        output_type="pil",
    )
    if run.negative_prompt:
        kw["negative_prompt"] = run.negative_prompt
    if generator is not None:
        kw["generator"] = generator
    for k in drop:
        kw.pop(k, None)
    if pipe is not None:
        import inspect
        try:
            sig = inspect.signature(pipe.__call__)
            accepted = set(sig.parameters)
            kw = {k: v for k, v in kw.items() if k in accepted}
        except (ValueError, TypeError):
            pass
    return kw


def strip_nonjson(kwargs: dict[str, Any]) -> dict[str, Any]:
    return {k: v for k, v in kwargs.items()
            if isinstance(v, (str, int, float, bool, type(None)))}


@contextmanager
def attach_diffusers_module_timing(
    pipe: Any,
    timer: Timer,
    *,
    module_attrs: Iterable[str] | None = None,
) -> Iterator[dict[str, list[float]]]:
    """Hook the pipeline's submodules and yield {attr: [ms,...]} on exit."""
    if module_attrs is None:
        hooks = ModuleTimingHooks(timer)
    else:
        hooks = ModuleTimingHooks(timer, module_attrs=module_attrs)
    hooks.attach(pipe)
    captured: dict[str, list[float]] = {}
    try:
        yield captured
        hooks.flush()
        captured.update(_aggregate_records(timer.records))
    finally:
        hooks.detach()


def _aggregate_records(records: list[Any]) -> dict[str, list[float]]:
    """Group timing records by submodule attribute name."""
    out: dict[str, list[float]] = {}
    for rec in records:
        if not rec.name.startswith("submodule."):
            continue
        attr = rec.name.split(".", 1)[1]
        out.setdefault(attr, []).append(rec.elapsed_ms)
    return out


def apply_gpu_mode(
    pipe: Any,
    *,
    device: torch.device,
    gpu_mode: str,
    device_id: int = 0,
) -> bool:
    """Place the pipeline according to `gpu_mode`. Returns True if cpu_offload
    or sequential_offload was enabled (callers may need to know to seed
    generators on the cpu device).

    Modes:
      - ""/"direct": move the pipeline to `device` outright.
      - "cpu_offload": diffusers' enable_model_cpu_offload (one module at a
        time on the GPU).
      - "sequential_offload": enable_sequential_cpu_offload (per-layer).
    """
    mode = (gpu_mode or "").strip().lower() or "direct"
    is_cuda = device.type == "cuda"
    is_mps = device.type == "mps"

    if mode == "cpu_offload":
        if is_cuda:
            pipe.enable_model_cpu_offload(gpu_id=device_id)
        elif is_mps:
            pipe.enable_model_cpu_offload()
        else:
            raise ValueError(
                f"gpu_mode=cpu_offload not supported for device {device!s}")
        offloaded = True
    elif mode == "sequential_offload":
        if is_cuda:
            pipe.enable_sequential_cpu_offload(gpu_id=device_id)
        elif is_mps:
            pipe.enable_sequential_cpu_offload()
        else:
            raise ValueError(
                f"gpu_mode=sequential_offload not supported for device "
                f"{device!s}")
        offloaded = True
    elif mode == "direct":
        pipe.to(device)
        offloaded = False
    else:
        raise ValueError(
            f"unknown gpu_mode {gpu_mode!r}; choose direct, cpu_offload, "
            f"or sequential_offload")

    if is_mps:
        # Chunked attention cuts peak unified-memory pressure on Apple.
        try:
            pipe.enable_attention_slicing()
        except (AttributeError, NotImplementedError):
            pass

    return offloaded


def log_component_devices(pipe: Any) -> None:
    """Print one row per pipeline component: name, class, device, dtype, params.

    Goes to stderr so the output is visible in the harness logs even when
    stdout is captured by the parent process.
    """
    if pipe is None:
        return
    components = getattr(pipe, "components", None) or {}
    rows: list[tuple[str, str, str, str, str]] = []
    for name, mod in components.items():
        if not isinstance(mod, torch.nn.Module):
            rows.append((name, type(mod).__name__, "-", "-", "-"))
            continue
        p = next(mod.parameters(), None)
        if p is None:
            p = next(mod.buffers(), None)
        if p is not None:
            dev = str(p.device)
            dt = str(p.dtype)
        else:
            # TRT-compiled modules expose no params/buffers; fall back to the
            # module's own device/dtype attributes.
            dev = str(getattr(mod, "device", "?"))
            dt = str(getattr(mod, "dtype", "?"))
        n_params = sum(x.numel() for x in mod.parameters())
        if n_params == 0:
            stored = getattr(mod, "_param_count", 0)
            n_params = stored if isinstance(stored, int) else 0
        rows.append((name, type(mod).__name__, dev, dt,
                     f"{n_params/1e6:.1f}M"))
    if not rows:
        return
    width = max(len(r[0]) for r in rows)
    print("[diffusers] pipeline components:", file=sys.stderr, flush=True)
    for name, cls, dev, dt, n in rows:
        print(f"  {name:<{width}}  {cls:<32}  {dev:<10}  {dt:<16}  {n}",
              file=sys.stderr, flush=True)
