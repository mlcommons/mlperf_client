# -*- mode: python ; coding: utf-8 -*-
"""PyInstaller spec for the Diffusers standalone runner (dev-only).

Onedir output (EXE + sibling libs under standalone/). Single-file isn't
viable: TensorRT 10 cu13 ships tensorrt_cu13_libs (1.9 GB) which alone
exceeds PyInstaller's ~2 GB single-file ZIP archive cap (signed int32 in
the CTOC trailer — pyinstaller#3939, #5264).

Composition: per-vendor contributors in tools/specs/<vendor>.py are
merged by tools/specs/__init__.py:aggregate(). Adding a new IHV is a
new tools/specs/<vendor>.py file — no edits here unless its packages
need new handling beyond hidden/metadata/data/binary/runtime_hook.
"""

import os
import sys

# SPECPATH is the directory containing this spec file (= tools/).
TOOLS_DIR = os.path.abspath(SPECPATH)
SCRIPTS_DIR = os.path.join(
    os.path.dirname(TOOLS_DIR), 'src', 'scripts'
)

# Make tools/specs importable.
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

from specs import aggregate  # noqa: E402
from specs import _common, apple, nvidia  # noqa: E402


# All vendors contribute regardless of platform; aggregator's _safe wrapper
# skips packages that aren't installed. So the same spec produces a clean
# bundle on Windows (NVIDIA libs collected, Apple no-op) and macOS (Apple
# no-op, NVIDIA libs absent → all skipped).
_contributions = [
    _common.contribute(),
    nvidia.contribute(tools_dir=TOOLS_DIR),
    apple.contribute(),
]
resolved = aggregate(_contributions)


a = Analysis(
    [os.path.join(SCRIPTS_DIR, 'standalone_runner.py')],
    pathex=[SCRIPTS_DIR],
    binaries=resolved["binaries"],
    datas=resolved["datas"],
    hiddenimports=resolved["hiddenimports"],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=resolved["runtime_hooks"],
    excludes=resolved["excludes"],
    noarchive=True,
)

pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name='diffusers_standalone',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False,
    console=True,
)

coll = COLLECT(
    exe,
    a.binaries,
    a.datas,
    strip=False,
    upx=False,
    upx_exclude=[],
    name='standalone',
)
