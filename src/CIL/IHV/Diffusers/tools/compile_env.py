"""Freeze a Python environment to bytecode -- the PyInstaller-equivalent step.

Compiles every .py under <root> to a .pyc with PEP 552 UNCHECKED_HASH
invalidation. A normal (timestamp) .pyc is invalidated when the source
mtime changes -- it would silently recompile after a zip/extract.
UNCHECKED_HASH .pyc carry no mtime (never re-validated, so they survive
zip/extract) and no timestamp (byte-reproducible -> macOS codesign-safe).

Source .py is left in place: torch._dynamo / torch.jit read their own
source via inspect.getsource at runtime.

Usage: python compile_env.py <env_root> [<env_root> ...]
"""

import compileall
import sys
from py_compile import PycInvalidationMode


def freeze(root: str) -> bool:
    print(f"[compile_env] freezing {root}", flush=True)
    return compileall.compile_dir(
        root,
        force=True,        # overwrite any stale timestamp-based .pyc
        quiet=1,           # report failures only
        workers=0,         # use every core
        optimize=0,        # match PyInstaller's default optimization level
        invalidation_mode=PycInvalidationMode.UNCHECKED_HASH,
    )


def main(argv=None):
    roots = list(sys.argv[1:] if argv is None else argv)
    if not roots:
        print("usage: compile_env.py <env_root> [<env_root> ...]",
              file=sys.stderr)
        return 2
    for root in roots:
        # compile_dir returns False if any file failed (e.g. stdlib
        # badsyntax_* test fixtures) -- harmless, it recompiles on import.
        # Warn, never abort.
        if not freeze(root):
            print(f"[compile_env] note: some files under {root} did not "
                  "compile; they fall back to import-time compilation",
                  file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
