"""Timing primitives.

`CudaTimer` records via `torch.cuda.Event` pairs so per-call cost isn't
amortized into a `torch.cuda.synchronize()` on every scope. `HostTimer`
uses `time.perf_counter()` with an explicit device-sync callback at
scope edges (MPS path). Both fill a shared `TimingRecord` list, and
`flush_pending()` resolves any deferred CUDA event pairs into ms.
"""
from __future__ import annotations

import time
from contextlib import contextmanager
from dataclasses import dataclass, field
from typing import Callable, Iterator

import torch


@dataclass
class TimingRecord:
    name: str
    elapsed_ms: float
    metadata: dict = field(default_factory=dict)


class Timer:
    """Base class shared by CudaTimer and HostTimer."""

    has_pending_events: bool = False

    def __init__(self) -> None:
        self._records: list[TimingRecord] = []

    @property
    def records(self) -> list[TimingRecord]:
        return self._records

    def reset(self) -> None:
        self._records.clear()

    def add(self, record: TimingRecord) -> None:
        self._records.append(record)

    @contextmanager
    def scope(self, name: str, **metadata) -> Iterator[None]:  # pragma: no cover
        raise NotImplementedError

    def begin_scope(self, name: str, **metadata):
        return _HostHandle(
            name=name, metadata=dict(metadata), t0=time.perf_counter(),
        )

    def end_scope(self, handle) -> None:
        elapsed = (time.perf_counter() - handle.t0) * 1000.0
        self._records.append(TimingRecord(
            name=handle.name, elapsed_ms=elapsed, metadata=handle.metadata,
        ))


@dataclass
class _HostHandle:
    name: str
    metadata: dict
    t0: float


class CudaTimer(Timer):
    """CUDA-event-based timer; defers `elapsed_time()` to flush_pending()."""

    has_pending_events = True

    def __init__(self) -> None:
        super().__init__()
        self._cuda = torch.cuda.is_available()

    @contextmanager
    def scope(self, name: str, **metadata) -> Iterator[None]:
        if self._cuda:
            torch.cuda.synchronize()
            start = torch.cuda.Event(enable_timing=True)
            end = torch.cuda.Event(enable_timing=True)
            start.record()
            try:
                yield
            finally:
                end.record()
                torch.cuda.synchronize()
                self._records.append(TimingRecord(
                    name=name,
                    elapsed_ms=start.elapsed_time(end),
                    metadata=metadata,
                ))
        else:
            t0 = time.perf_counter()
            try:
                yield
            finally:
                self._records.append(TimingRecord(
                    name=name,
                    elapsed_ms=(time.perf_counter() - t0) * 1000.0,
                    metadata=metadata,
                ))

    def begin_scope(self, name: str, **metadata):
        if not self._cuda:
            return super().begin_scope(name, **metadata)
        start = torch.cuda.Event(enable_timing=True)
        end = torch.cuda.Event(enable_timing=True)
        start.record()
        return _CudaHandle(
            name=name, metadata=dict(metadata), start=start, end=end,
        )

    def end_scope(self, handle) -> None:
        if isinstance(handle, _HostHandle):
            return super().end_scope(handle)
        # Defer elapsed_time() until flush_pending so we don't synchronize per call.
        handle.end.record()
        self._records.append(TimingRecord(
            name=handle.name,
            elapsed_ms=-1.0,
            metadata={
                "_pending_event_pair": (handle.start, handle.end),
                **handle.metadata,
            },
        ))


@dataclass
class _CudaHandle:
    name: str
    metadata: dict
    start: "torch.cuda.Event"
    end: "torch.cuda.Event"


class HostTimer(Timer):
    """Wall-clock timer with explicit device sync at scope edges (MPS path)."""

    has_pending_events = False

    def __init__(self, sync_fn: Callable[[], None] | None = None) -> None:
        super().__init__()
        self._sync = sync_fn or (lambda: None)

    @contextmanager
    def scope(self, name: str, **metadata) -> Iterator[None]:
        self._sync()
        t0 = time.perf_counter()
        try:
            yield
        finally:
            self._sync()
            self._records.append(TimingRecord(
                name=name,
                elapsed_ms=(time.perf_counter() - t0) * 1000.0,
                metadata=metadata,
            ))


class TimingScope:
    """Manual start/stop pair used by forward hooks; delegates to a Timer."""

    def __init__(self, name: str, **metadata) -> None:
        self.name = name
        self.metadata = metadata
        self._handle = None

    def start(self, timer: Timer) -> None:
        self._handle = timer.begin_scope(self.name, **self.metadata)

    def stop(self, timer: Timer) -> None:
        if self._handle is not None:
            timer.end_scope(self._handle)


def flush_pending(timer: Timer) -> None:
    """Resolve deferred CUDA event pairs into elapsed_ms. Safe on any timer."""
    if not getattr(timer, "has_pending_events", False) \
            or not torch.cuda.is_available():
        return
    pending = [r for r in timer.records if "_pending_event_pair" in r.metadata]
    if not pending:
        return
    torch.cuda.synchronize()
    for r in pending:
        start, end = r.metadata.pop("_pending_event_pair")
        r.elapsed_ms = start.elapsed_time(end)
