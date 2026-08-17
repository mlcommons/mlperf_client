"""Abstract Vendor interface — one vendor's runtime concerns."""
from __future__ import annotations

import abc
from typing import Any, Callable, TYPE_CHECKING

import torch

if TYPE_CHECKING:
    from timing.timer import Timer


_DTYPE_TABLE: dict[str, torch.dtype] = {
    "float16": torch.float16, "fp16": torch.float16, "half": torch.float16,
    "bfloat16": torch.bfloat16, "bf16": torch.bfloat16,
    "float32": torch.float32, "fp32": torch.float32, "float": torch.float32,
}


def parse_dtype(name: str) -> torch.dtype:
    key = (name or "").strip().lower()
    if key not in _DTYPE_TABLE:
        raise ValueError(
            f"unsupported dtype {name!r}. "
            f"Choose from {sorted(_DTYPE_TABLE)}"
        )
    return _DTYPE_TABLE[key]


class Vendor(abc.ABC):
    """One vendor. Owns device / dtype / timer / generator concerns."""

    display_name: str = "abstract"
    backend: str = "abstract"  # torch backend key: "cuda" / "mps"

    @abc.abstractmethod
    def validate(self, deps_dir: str | None = None) -> None: ...

    @abc.abstractmethod
    def configure_globals(self) -> dict[str, Any]: ...

    @abc.abstractmethod
    def resolve_device(self, requested: str | None) -> torch.device: ...

    def normalize_dtype(self, name: str) -> torch.dtype:
        return parse_dtype(name)

    def default_dtype(self) -> str:
        return "float16"

    def _device_sync_fn(self) -> Callable[[], None]:
        return lambda: None

    def make_e2e_timer(self) -> "Timer":
        from timing.timer import HostTimer
        return HostTimer(sync_fn=self._device_sync_fn())

    @abc.abstractmethod
    def make_module_timer(self) -> "Timer": ...

    @abc.abstractmethod
    def make_generator(self, seed: int,
                       *, prefer_cpu: bool = False) -> torch.Generator:
        """Return a seeded torch.Generator on the vendor's preferred device.

        When `prefer_cpu=True` (e.g. cpu_offload / sequential_offload is
        enabled) callers ask for a CPU generator instead, because latents
        initialise on CPU under those modes and a non-CPU generator
        forces an extra device-sync per step.
        """
        ...

    def release_memory(self) -> None:
        import gc
        gc.collect()

    def system_info(self) -> dict[str, Any]:
        return {"display_name": self.display_name}
