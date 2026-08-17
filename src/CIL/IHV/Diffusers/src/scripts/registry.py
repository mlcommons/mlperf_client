"""Runtime and optimization registries for the embedded Diffusers EP.

Two registries:

- `@register_runtime(backend=...)` records a Runtime subclass under a
  torch-backend key ("cuda" or "mps"). This is distinct from the EP
  config's `device_type` ("GPU"/"NPU") — `get_vendor` maps
  device_vendor/device_type to a Vendor.

- `@register_opt(name)` records an optimization handler callable
  `(runtime, params) -> dict | None`. Runtime.apply_optimizations looks
  handlers up by name; unregistered names log a warning and are skipped.
"""
from __future__ import annotations

from typing import Any, Callable, TYPE_CHECKING, Type

if TYPE_CHECKING:
    from runtime import Runtime


def _normalize(name: str) -> str:
    if not isinstance(name, str) or not name.strip():
        raise ValueError(f"backend name must be a non-empty string, got {name!r}")
    return name.strip().lower()


_RUNTIMES: dict[str, Type["Runtime"]] = {}


def register_runtime(
    *, backend: str,
) -> Callable[[Type["Runtime"]], Type["Runtime"]]:
    key = _normalize(backend)

    def deco(cls: Type["Runtime"]) -> Type["Runtime"]:
        cls.backend = key
        if key in _RUNTIMES:
            raise ValueError(f"runtime for backend {key!r} already registered")
        _RUNTIMES[key] = cls
        return cls

    return deco


def resolve_runtime(backend: str) -> Type["Runtime"]:
    """Return the Runtime class registered for `backend`."""
    key = _normalize(backend)
    cls = _RUNTIMES.get(key)
    if cls is None:
        raise KeyError(
            f"no runtime registered for backend {key!r}. "
            "Register one with @register_runtime(backend=...)."
        )
    return cls


OptHandler = Callable[[Any, dict[str, Any]], dict[str, Any] | None]
"""Optimization handler: `(runtime, params) -> info dict (or None)`."""


_OPTS: dict[str, OptHandler] = {}


def register_opt(name: str) -> Callable[[OptHandler], OptHandler]:
    """Register an optimization handler under `name`.

    Handlers receive the active Runtime instance and the spec's `params`
    dict, and may return an info dict for the result record. Handlers
    that depend on optional packages (e.g. torch_tensorrt) should guard
    their imports inside the function body and raise ImportError so the
    handler is registered but fails clearly when invoked.
    """
    key = name.strip()
    if not key:
        raise ValueError("optimization name must be a non-empty string")

    def deco(fn: OptHandler) -> OptHandler:
        if key in _OPTS:
            raise ValueError(f"optimization {key!r} already registered")
        _OPTS[key] = fn
        return fn

    return deco


def get_opt(name: str) -> OptHandler | None:
    return _OPTS.get(name.strip())
