"""Pack a self-contained Python environment for the Diffusers EP.

  --from-install <prefix>   reuse CMake-populated <prefix>/python.
  (default)                 fresh PBS download + pip install + zip.
"""

import argparse
import fnmatch
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile
from pathlib import Path


PBS_VERSION = "3.13.13"
PBS_RELEASE = "20260510"
PBS_BASE = ("https://github.com/astral-sh/python-build-standalone/"
            "releases/download")

CUDA_INDEX = "https://download.pytorch.org/whl/cu130"

THIS_DIR = Path(__file__).resolve().parent
ASSETS_DIR = THIS_DIR.parent / "src" / "assets"
INSTALL_DEPS_SCRIPT = THIS_DIR / "install_python_deps.py"
COMPILE_ENV_SCRIPT = THIS_DIR / "compile_env.py"


# .pyc / __pycache__ are kept on purpose -- shipping compile_env.py's frozen
# bytecode is the whole point. *.pdb are Windows debug symbols, no runtime use.
_COMMON_EXCLUDE_FILE_PATTERNS = (
    "*.pdb",
    "RECORD",  # breaks codesign on macOS
)
# PyInstaller and its hook bundle are build-only: the standalone target
# (build_standalone.cmake.in) needs them in the build env, but the runtime
# ZIP never freezes an exe -- they would be dead weight there, so drop them.
_COMMON_EXCLUDE_DIR_NAMES = ("PyInstaller", "_pyinstaller_hooks_contrib")
# Don't exclude tests/ or include/ recursively:
#   numpy.testing._private.utils imports numpy._core.tests._natype at runtime
#   torch._inductor MPS backend needs torch/include/c10/metal/*.h at runtime

# Embedded-Python minor version — keep in sync with python.cmake (PBS_VERSION).
_PY_MINOR = "3.13"

PLATFORM_MAP = {
    "windows_x64": {
        "triple": "x86_64-pc-windows-msvc",
        "python_bin": "python/python.exe",
        "vendor": "nvidia",
        "torch_index": CUDA_INDEX,
        "extra_pre_install": ["bitsandbytes"],
        "exclude_file_names": ("python.exe", "pythonw.exe", "python3.exe"),
        "exclude_file_patterns": _COMMON_EXCLUDE_FILE_PATTERNS,
        "exclude_dir_names": _COMMON_EXCLUDE_DIR_NAMES + ("Scripts",),
        "exclude_top_level_dirs": ("include",),
        # *.lib outside site-packages = compile-only import libs.
        "exclude_lib_outside_site_packages": True,
    },
    "macos_arm64": {
        "triple": "aarch64-apple-darwin",
        "python_bin": "python/bin/python3",
        "vendor": "apple",
        "torch_index": "",
        "extra_pre_install": [],
        "exclude_file_names": ("python3", f"python{_PY_MINOR}", "pythonw"),
        "exclude_file_patterns": _COMMON_EXCLUDE_FILE_PATTERNS,
        "exclude_dir_names": _COMMON_EXCLUDE_DIR_NAMES,
        # Top-level only; nested bin/share/include are needed (e.g.
        # torch/bin/torch_shm_manager, torch/include/c10/metal/*.h).
        "exclude_top_level_dirs": ("bin", "share", "include"),
        "exclude_lib_outside_site_packages": False,
        # Mirror to match CMake-install: EP binary uses @loader_path/libpython.
        "root_mirror_from_python": (f"lib/libpython{_PY_MINOR}.dylib",),
    },
}


def download(url, dest):
    print(f"[pack] downloading {url}")
    urllib.request.urlretrieve(url, dest)


def extract(archive, dest_dir):
    print(f"[pack] extracting {archive.name}")
    import tarfile
    if archive.name.endswith(".tar.gz"):
        with tarfile.open(archive, "r:gz") as tar:
            tar.extractall(dest_dir)
    elif archive.name.endswith(".tar.zst"):
        try:
            import zstandard, io
            with archive.open("rb") as f:
                dctx = zstandard.ZstdDecompressor()
                with dctx.stream_reader(f) as r, tarfile.open(
                        fileobj=io.BufferedReader(r), mode="r|") as tar:
                    tar.extractall(dest_dir)
        except ImportError:
            subprocess.run(["zstd", "-d", str(archive),
                            "-o", str(archive.with_suffix(""))], check=True)
            with tarfile.open(archive.with_suffix("")) as tar:
                tar.extractall(dest_dir)
    else:
        raise RuntimeError(f"unknown archive format: {archive}")


def install_packages(python_bin, vendor):
    """Delegate to install_python_deps.py — single source of truth for
    the install order (torch pin, torch_tensorrt --no-deps, etc.)."""
    subprocess.run([python_bin, "-I", "-m", "pip", "install", "--upgrade",
                    "pip", "--no-warn-script-location"], check=True)
    subprocess.run([python_bin, "-I", str(INSTALL_DEPS_SCRIPT),
                    "--vendor", vendor], check=True)


def write_freeze(python_bin, root):
    freeze_path = root.parent / "pip_freeze.txt"
    with freeze_path.open("w") as f:
        subprocess.run([python_bin, "-I", "-m", "pip", "freeze"],
                       stdout=f, check=True)


def _make_filter(root, plat):
    exclude_dirs = set(plat["exclude_dir_names"])
    exclude_top_level = set(plat.get("exclude_top_level_dirs", ()))
    exclude_files = set(plat["exclude_file_names"])
    exclude_patterns = plat["exclude_file_patterns"]
    skip_libs_outside_sp = plat["exclude_lib_outside_site_packages"]

    def should_skip(path):
        rel = path.relative_to(root)
        parts = rel.parts
        if any(p in exclude_dirs for p in parts):
            return True
        if parts and parts[0] in exclude_top_level:
            return True
        if path.is_file():
            name = path.name
            if name in exclude_files:
                return True
            for pat in exclude_patterns:
                if fnmatch.fnmatch(name, pat):
                    return True
            if (skip_libs_outside_sp and name.endswith(".lib")
                    and "site-packages" not in parts):
                return True
            if name == "RECORD" and path.parent.name.endswith(".dist-info"):
                return True
        return False

    return should_skip


def make_zip(root, output_path, plat):
    # Root mirrors: python*.dll (Windows) / libpython*.dylib (macOS) need to
    # land next to the python-linked EP binary.
    print(f"[pack] zipping {root} -> {output_path}")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    skip = _make_filter(root, plat)
    added = 0
    skipped_bytes = 0
    root_mirrored = set()
    extra_mirrors = plat.get("root_mirror_from_python", ())
    with zipfile.ZipFile(output_path, "w", zipfile.ZIP_DEFLATED,
                         allowZip64=True) as zf:
        for f in root.rglob("*"):
            if skip(f):
                if f.is_file():
                    try:
                        skipped_bytes += f.stat().st_size
                    except OSError:
                        pass
                continue
            if f.is_symlink():
                arc = str(f.relative_to(root.parent)).replace(os.sep, "/")
                zi = zipfile.ZipInfo(arc)
                zi.create_system = 3
                zi.external_attr = (0xA1FF << 16)
                zf.writestr(zi, os.readlink(f))
                added += 1
            elif f.is_file():
                zf.write(f, f.relative_to(root.parent))
                added += 1
                if (f.parent == root
                        and f.suffix.lower() == ".dll"
                        and f.name not in root_mirrored):
                    zf.write(f, f.name)
                    root_mirrored.add(f.name)
                    added += 1
        for rel in extra_mirrors:
            src = root / rel
            if src.is_file() and src.name not in root_mirrored:
                zf.write(src, src.name)
                root_mirrored.add(src.name)
                added += 1
    size_mib = output_path.stat().st_size / 1024 / 1024
    skipped_mib = skipped_bytes / 1024 / 1024
    print(f"[pack] {output_path}: {size_mib:.1f} MiB "
          f"({added} entries, skipped ~{skipped_mib:.1f} MiB)")
    if root_mirrored:
        print(f"[pack] mirrored at zip root: {sorted(root_mirrored)}")


def _python_exe_in(plat, python_root):
    """Path to the interpreter inside an env root (cross-platform)."""
    # plat["python_bin"] is "python/<rel>"; <rel> is the exe within the env.
    rel = plat["python_bin"].split("/", 1)[1]
    return Path(python_root) / rel


def freeze_env(plat, python_root):
    """Compile the env to unchecked-hash bytecode (see compile_env.py) so the
    ZIP ships frozen. Run with the env's own python so the emitted .pyc magic
    matches the interpreter that will import it."""
    python_exe = _python_exe_in(plat, python_root)
    subprocess.run([str(python_exe), "-I", str(COMPILE_ENV_SCRIPT),
                    str(python_root)], check=True)


def _freeze_and_zip(python_root, output, plat):
    """Freeze the env to bytecode, then pack it. Freezing here -- rather than
    trusting an upstream step -- guarantees every ZIP we emit is frozen, no
    matter what produced the env; compile_env.py is idempotent."""
    freeze_env(plat, python_root)
    make_zip(python_root, output, plat)


def _prepare_python_from_install(install_prefix, vendor=None):
    """Resolve the python/ dir for a CMake-installed Diffusers EP.

    Multi-vendor install layout is `<prefix>/<vendor>/python/`; single-vendor
    is `<prefix>/python/` (flat). When `vendor` is given we look at that
    subdir; when None we accept either layout for the prefix.
    """
    install_prefix = Path(install_prefix)
    if vendor:
        src = install_prefix / vendor / "python"
        if src.is_dir():
            return src
        raise RuntimeError(
            f"--from-install: vendor='{vendor}' but no 'python/' dir at {src}")
    # No vendor specified — accept flat single-vendor layout.
    src = install_prefix / "python"
    if src.is_dir():
        return src
    raise RuntimeError(
        f"--from-install: no 'python/' directory under {install_prefix}")


def _discover_vendor_subdirs(install_prefix):
    """Find every vendor subdir under a multi-vendor install prefix.

    A vendor subdir contains a `python/` dir (the embedded venv). Returns
    a list of subdir names (e.g. ['nvidia', 'apple']) or an empty list if
    the install is single-vendor (flat) or has no python install at all.
    """
    install_prefix = Path(install_prefix)
    if not install_prefix.is_dir():
        return []
    vendors = []
    for child in sorted(install_prefix.iterdir()):
        if child.is_dir() and (child / "python").is_dir():
            vendors.append(child.name)
    return vendors


def _prepare_python_fresh(plat, temp_dir):
    archive_name = (
        f"cpython-{PBS_VERSION}+{PBS_RELEASE}-{plat['triple']}-"
        f"install_only.tar.gz"
    )
    url = f"{PBS_BASE}/{PBS_RELEASE}/{archive_name}"
    archive_path = temp_dir / archive_name
    download(url, archive_path)
    extract(archive_path, temp_dir)
    python_root = temp_dir / "python"
    python_bin = temp_dir / plat["python_bin"]
    if not python_bin.exists():
        raise RuntimeError(f"python binary not found at {python_bin}")

    install_packages(str(python_bin), plat["vendor"])
    write_freeze(str(python_bin), python_root)
    return python_root


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--platform", required=True,
                   choices=list(PLATFORM_MAP.keys()))
    p.add_argument("--from-install", default=None, type=Path,
                   help="Reuse python/ from a CMake install. With "
                        "--vendor, packs <prefix>/<vendor>/python; without "
                        "--vendor, auto-discovers every vendor subdir under "
                        "<prefix> (one ZIP per vendor) or falls back to the "
                        "flat single-vendor <prefix>/python layout.")
    p.add_argument("--vendor", default=None,
                   help="Pack only this vendor's venv (e.g. 'nvidia'). "
                        "Only meaningful with --from-install. When omitted, "
                        "all enabled vendors are packed.")
    p.add_argument("--output", default=None,
                   help="Output ZIP path. With multi-vendor auto-discovery "
                        "this is ignored — each vendor's ZIP is written to "
                        "<prefix>/<vendor>/python_env.zip (cloud-mirror "
                        "layout). With --vendor or single-vendor layout, "
                        "defaults to ./python_env.zip.")
    p.add_argument("--keep-temp", action="store_true")
    args = p.parse_args(argv)

    plat = PLATFORM_MAP[args.platform]

    if args.from_install:
        prefix = Path(args.from_install)
        # Single-vendor lookup (explicit vendor or flat layout)
        if args.vendor or (prefix / "python").is_dir():
            python_root = _prepare_python_from_install(prefix, vendor=args.vendor)
            if args.output:
                output = Path(args.output)
            elif args.vendor:
                # Cloud-mirror: per-vendor ZIP sits next to its venv so a
                # `file://<prefix>/<vendor>/python_env.zip` URL in the EP-deps
                # config points at the same shape the cloud will serve.
                output = prefix / args.vendor / "python_env.zip"
            else:
                # Single-vendor flat layout: drop the ZIP alongside python/
                # under the install prefix (same cloud-mirror principle —
                # `file://<prefix>/python_env.zip` is what the EP-deps URL
                # resolves to in single-vendor mode).
                output = prefix / "python_env.zip"
            _freeze_and_zip(python_root, output, plat)
            return 0

        # Multi-vendor auto-discovery: pack every vendor subdir found.
        vendors = _discover_vendor_subdirs(prefix)
        if not vendors:
            raise RuntimeError(
                f"--from-install: no python/ at {prefix}/python and no "
                "vendor subdirs (<vendor>/python) discovered. Either the "
                "install is incomplete or it predates the multi-vendor "
                "layout.")
        print(f"[pack] multi-vendor install: packing {vendors}")
        if args.output:
            print("[pack] --output ignored in multi-vendor mode; each ZIP "
                  "lands at <prefix>/<vendor>/python_env.zip")
        for vendor in vendors:
            python_root = _prepare_python_from_install(prefix, vendor=vendor)
            output = prefix / vendor / "python_env.zip"
            _freeze_and_zip(python_root, output, plat)
        return 0

    # Fresh PBS-download + pip-install path. No install prefix to mirror
    # against, so we can't auto-place the ZIP next to anything sensible —
    # require an explicit --output to avoid silently writing to cwd.
    if not args.output:
        raise SystemExit(
            "--output is required when packing fresh (no --from-install). "
            "Without it the script would silently write python_env.zip into "
            "the current working directory.")
    output = Path(args.output)
    temp_dir = Path(tempfile.mkdtemp(prefix="diffusers_env_"))
    try:
        python_root = _prepare_python_fresh(plat, temp_dir)
        _freeze_and_zip(python_root, output, plat)
    finally:
        if args.keep_temp:
            print(f"[pack] keeping working directory: {temp_dir}")
        else:
            shutil.rmtree(temp_dir, ignore_errors=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
