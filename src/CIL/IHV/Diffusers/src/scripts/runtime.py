"""Abstract Runtime and supporting dataclasses for the Diffusers EP.

A Runtime is one execution path for one model family on one backend
(e.g. NVIDIA + diffusers + flux2). It owns the loaded `pipe`, exposes
a single `generate()` method, and can apply registered optimizations
at load time. Concrete runtimes register themselves via
`@register_runtime(backend=...)` from `registry`.
"""
from __future__ import annotations

import abc
import logging
from contextlib import contextmanager
from dataclasses import dataclass, field
from typing import Any, Callable, Iterator, TYPE_CHECKING

import torch

if TYPE_CHECKING:
    from vendors.base import Vendor
    from timing.timer import Timer

log = logging.getLogger(__name__)


@dataclass(frozen=True)
class OptimizationSpec:
    """One optimization to apply at load time (e.g. tensorrt_rtx, mps_compile).

    `type` resolves to a handler registered via `@register_opt`; `params`
    is forwarded to the handler verbatim.
    """
    type: str
    params: dict[str, Any] = field(default_factory=dict)


@dataclass(frozen=True)
class RunSpec:
    """One image-generation request."""
    prompt: str
    negative_prompt: str | None = None
    width: int = 1024
    height: int = 1024
    steps: int = 28
    guidance_scale: float = 3.5
    seed: int = -1
    num_images_per_prompt: int = 1
    seeds: tuple[int, ...] = ()


Loader = Callable[["Runtime"], Any]
"""Receives the runtime, returns the loaded `pipe` object. May set
`runtime.model_ref` / `runtime.auto_download` to surface them in results."""


class Runtime(abc.ABC):
    """One (backend, library) execution path.

    `backend` is the torch backend the runtime targets — "cuda" or "mps".
    This is distinct from the EP config's `device_type` ("GPU"/"NPU");
    `get_vendor()` maps the pair to a concrete Vendor instance.

    Subclasses define `SUPPORTED_MODELS` (mapping model-family prefix to
    Loader) and implement `generate()`. They may override `instrument()`
    to surface per-submodule timings, and define `DEFAULT_OPTS` /
    `EXPECTED_SUBMODULES`.
    """

    backend: str = "ABSTRACT"
    family: str = "abstract"
    SUPPORTED_MODELS: dict[str, Loader] = {}
    DEFAULT_OPTS: list[OptimizationSpec] = []
    EXPECTED_SUBMODULES: dict[str, tuple[str, ...]] = {}

    def __init__(
        self,
        *,
        model_path: str,
        model_base_name: str,
        vendor: "Vendor",
        device: torch.device,
        dtype: torch.dtype,
        gpu_mode: str = "",
        quantization: str = "",
        component_files: dict[str, str] | None = None,
        download_cache: str | None = None,
    ) -> None:
        self.model_path = model_path
        self.model_base_name = model_base_name
        self.vendor = vendor
        self.device = device
        self.dtype = dtype
        self.gpu_mode = gpu_mode
        self.quantization = quantization
        self.component_files = component_files or {}
        self.download_cache = download_cache
        self.pipe: Any = None
        self.model_ref: str | None = model_path or None
        self.auto_download: bool = False
        self.hardware: dict[str, Any] = {}

    @property
    def expected_submodules(self) -> tuple[str, ...]:
        for prefix, attrs in self.EXPECTED_SUBMODULES.items():
            if self.model_base_name.startswith(prefix):
                return attrs
        return ()

    def load(self) -> None:
        self.query_hardware()
        loader = self._resolve_loader()
        self.pipe = loader(self)
        self.pipe.set_progress_bar_config(disable=True, leave=False)

    def query_hardware(self) -> None:
        """Populate `self.hardware` with device info before loading.

        Default no-op; each IHV runtime overrides with its own probe so
        loaders can gate features on the detected hardware.
        """
        return None

    def _resolve_loader(self) -> Loader:
        for prefix, loader in self.SUPPORTED_MODELS.items():
            if self.model_base_name.startswith(prefix):
                return loader
        raise ValueError(
            f"{type(self).__name__} has no loader for model_base_name="
            f"{self.model_base_name!r}. Known prefixes: "
            f"{sorted(self.SUPPORTED_MODELS)}"
        )

    @abc.abstractmethod
    def generate(
        self,
        run: RunSpec,
        *,
        generator: torch.Generator | None = None,
        step_callback: Callable[[int, int], None] | None = None,
        submodule_callback: Callable[[str, float], None] | None = None,
    ) -> Any: ...

    def unload(self) -> None:
        self.pipe = None

    def apply_optimizations(
        self,
        opts: list[OptimizationSpec] | None,
    ) -> list[dict[str, Any]]:
        """Apply each optimization, timed. Returns one info record per opt."""
        from registry import get_opt

        effective = list(opts) if opts is not None else list(self.DEFAULT_OPTS)
        info: list[dict[str, Any]] = []
        for spec in effective:
            handler = get_opt(spec.type)
            if handler is None:
                log.warning(
                    "%s: optimization %r is not registered — skipping",
                    type(self).__name__, spec.type,
                )
                info.append({
                    "type": spec.type, "skipped": True,
                    "reason": "unregistered",
                })
                continue
            timer = self.vendor.make_e2e_timer()
            log.info("applying optimization: %s %s", spec.type, spec.params)
            with timer.scope(f"setup.opt.{spec.type}"):
                result = handler(self, spec.params or {}) or {}
            info.append({
                "type": spec.type,
                "params": dict(spec.params or {}),
                "apply_ms": timer.records[-1].elapsed_ms,
                "info": result,
            })
        return info

    @contextmanager
    def instrument(self, module_timer: "Timer") -> Iterator[dict[str, list[float]]]:
        """Yield a dict to be populated with per-submodule timings on exit.

        Default: no instrumentation. Diffusers-using runtimes override this
        via `helpers.attach_diffusers_module_timing`.
        """
        captured: dict[str, list[float]] = {}
        yield captured

    def runtime_info(self) -> dict[str, Any]:
        return {
            "runtime": type(self).__name__,
            "backend": self.backend,
            "family": self.family,
            "model_base_name": self.model_base_name,
            "model_ref": self.model_ref,
            "auto_download": self.auto_download,
            "expected_submodules": list(self.expected_submodules),
        }
