"""PyInstaller runtime hook — frozen-exe environment shims.

Runs once at frozen-exe startup, before standalone_runner.py and before
any user imports of triton.

(1) Triton in-tree backend discovery: triton.backends.__init__
    reads TRITON_BACKENDS_IN_TREE at import time. Setting it to '1'
    makes it walk the bundled triton/backends/ directory instead of
    querying importlib.metadata.entry_points (which returns empty
    inside a frozen exe). Without this, @triton.jit raises
    "RuntimeError: 0 active drivers ([])".

(2) Triton C-compiler hint: triton.runtime.build looks for tcc.exe
    via sysconfig which doesn't resolve in a frozen exe. Point CC at
    the bundled tcc.exe directly.
"""
import os
import sys

os.environ.setdefault("TRITON_BACKENDS_IN_TREE", "1")


def _set_bundled_cc():
    meipass = getattr(sys, "_MEIPASS", None)
    if not meipass:
        return
    bundled_tcc = os.path.join(meipass, "triton", "runtime", "tcc", "tcc.exe")
    if os.path.isfile(bundled_tcc) and not os.environ.get("CC"):
        os.environ["CC"] = bundled_tcc


_set_bundled_cc()
