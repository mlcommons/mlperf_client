"""Per-vendor PyInstaller spec contributors.

Each vendor module exports ``contribute()`` returning a dict with optional
keys: hidden_packages, metadata_packages, data_packages, binary_packages,
binary_files, runtime_hooks, excludes. ``aggregate()`` merges them in
input order. Missing packages are skipped silently.
"""
from __future__ import annotations

import os
from typing import Any

from PyInstaller.utils.hooks import (
    collect_data_files,
    collect_dynamic_libs,
    collect_submodules,
    copy_metadata,
)


def _safe(fn, *a, **kw):
    try:
        return fn(*a, **kw)
    except Exception:
        return []


def aggregate(contributions: list[dict[str, Any]]) -> dict[str, Any]:
    """Merge per-vendor contribution dicts into resolved PyInstaller inputs."""
    hidden_pkgs: list[str] = []
    meta_pkgs: list[str] = []
    data_pkgs: list[str] = []
    binary_pkgs: list[str] = []
    binary_files: list[tuple[str, str]] = []
    runtime_hooks: list[str] = []
    excludes: list[str] = []

    for c in contributions:
        hidden_pkgs += c.get("hidden_packages", [])
        meta_pkgs += c.get("metadata_packages", [])
        data_pkgs += c.get("data_packages", [])
        binary_pkgs += c.get("binary_packages", [])
        binary_files += c.get("binary_files", [])
        runtime_hooks += c.get("runtime_hooks", [])
        excludes += c.get("excludes", [])

    hiddenimports: list[str] = []
    for pkg in hidden_pkgs:
        hiddenimports.extend(_safe(collect_submodules, pkg))

    datas: list[tuple[str, str]] = []
    for pkg in meta_pkgs:
        datas.extend(_safe(copy_metadata, pkg))
    for pkg in data_pkgs:
        datas.extend(_safe(collect_data_files, pkg, include_py_files=True))

    binaries: list[tuple[str, str]] = []
    for pkg in binary_pkgs:
        binaries.extend(_safe(collect_dynamic_libs, pkg))
    binaries.extend(binary_files)

    return {
        "hiddenimports": hiddenimports,
        "datas": datas,
        "binaries": binaries,
        "runtime_hooks": [h for h in runtime_hooks if os.path.isfile(h)],
        "excludes": excludes,
    }
