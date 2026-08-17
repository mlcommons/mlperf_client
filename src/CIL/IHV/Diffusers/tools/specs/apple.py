"""Apple / MPS-specific PyInstaller contribution.

No extra native libs needed: PyTorch's MPS backend is built into the
standard macOS arm64 wheel.
"""
from __future__ import annotations


def contribute() -> dict:
    return {
        "hidden_packages": [],
        "metadata_packages": [],
        "data_packages": [],
        "binary_packages": [],
        "binary_files": [],
        "runtime_hooks": [],
    }
