"""Forward hooks that record per-submodule timings on a diffusers pipeline."""
from __future__ import annotations

from typing import Any, Iterable

import torch
import torch.nn as nn

from timing.timer import Timer, TimingScope, flush_pending


_DEFAULT_MODULE_ATTRS: tuple[str, ...] = (
    "text_encoder", "text_encoder_2",
    "tokenizer", "tokenizer_2",
    "unet", "transformer", "vae", "scheduler",
)


class ModuleTimingHooks:
    """Pre/post forward hooks emit `submodule.<attr>` records into a Timer."""

    def __init__(
        self,
        timer: Timer,
        module_attrs: Iterable[str] = _DEFAULT_MODULE_ATTRS,
    ) -> None:
        self._timer = timer
        self._module_attrs = tuple(module_attrs)
        self._handles: list[torch.utils.hooks.RemovableHandle] = []
        self._active_scopes: dict[int, TimingScope] = {}
        self._call_counts: dict[str, int] = {}

    def attach(self, pipe: Any) -> list[str]:
        hooked: list[str] = []
        for attr in self._module_attrs:
            mod = getattr(pipe, attr, None)
            if mod is None or not isinstance(mod, nn.Module):
                continue
            self._call_counts[attr] = 0
            self._handles.append(
                mod.register_forward_pre_hook(self._make_pre_hook(attr)))
            self._handles.append(
                mod.register_forward_hook(self._make_post_hook(attr)))
            hooked.append(attr)
        return hooked

    def detach(self) -> None:
        for h in self._handles:
            h.remove()
        self._handles.clear()
        self._active_scopes.clear()
        self._call_counts.clear()

    def _make_pre_hook(self, attr: str):
        def pre(module: nn.Module, _inputs: Any) -> None:
            scope = TimingScope(
                f"submodule.{attr}",
                module=attr,
                call_index=self._call_counts[attr],
            )
            scope.start(self._timer)
            self._active_scopes[id(module)] = scope
            self._call_counts[attr] += 1
        return pre

    def _make_post_hook(self, attr: str):
        def post(module: nn.Module, _inputs: Any, _output: Any) -> None:
            scope = self._active_scopes.pop(id(module), None)
            if scope is not None:
                scope.stop(self._timer)
        return post

    def flush(self) -> None:
        flush_pending(self._timer)

    @property
    def call_counts(self) -> dict[str, int]:
        return dict(self._call_counts)
