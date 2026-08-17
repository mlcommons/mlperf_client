"""Build the PyInstaller standalone Diffusers runner. Dev-only.

Usage: python build_standalone.py [--output-dir DIR]
"""

import argparse
import os
import shutil
import subprocess
import sys


def ensure_pyinstaller():
    try:
        import PyInstaller  # noqa: F401
    except ImportError:
        print("PyInstaller not found. Installing...")
        subprocess.check_call([sys.executable, "-m", "pip", "install", "pyinstaller"])


def main():
    parser = argparse.ArgumentParser(description="Build Diffusers standalone executable")
    parser.add_argument("--output-dir", type=str, default=None,
                        help="Custom output directory (default: dist/ next to this script)")
    parser.add_argument("--clean", action="store_true",
                        help="Clean build/dist directories before building")
    args = parser.parse_args()

    tools_dir = os.path.dirname(os.path.abspath(__file__))
    spec_file = os.path.join(tools_dir, "standalone.spec")

    if not os.path.exists(spec_file):
        print(f"ERROR: Spec file not found: {spec_file}")
        sys.exit(1)

    ensure_pyinstaller()

    build_dir = os.path.join(tools_dir, "build")
    dist_dir = args.output_dir or os.path.join(tools_dir, "dist")

    if args.clean:
        for d in [build_dir, dist_dir]:
            if os.path.exists(d):
                print(f"Cleaning {d}...")
                shutil.rmtree(d)

    cmd = [
        sys.executable, "-m", "PyInstaller",
        spec_file,
        "--distpath", dist_dir,
        "--workpath", build_dir,
        "--noconfirm",
    ]

    print(f"Building standalone executable...")
    print(f"  Spec: {spec_file}")
    print(f"  Output: {dist_dir}")
    print()

    result = subprocess.run(cmd, cwd=tools_dir)
    if result.returncode != 0:
        print("\nERROR: PyInstaller build failed.")
        sys.exit(1)

    exe_name = "diffusers_standalone.exe" if sys.platform == "win32" else "diffusers_standalone"

    stub = os.path.join(build_dir, "standalone", exe_name)
    if os.path.exists(stub):
        os.remove(stub)

    output_dir = os.path.join(dist_dir, "standalone")
    exe_path = os.path.join(output_dir, exe_name)

    if os.path.exists(exe_path):
        print(f"\nBuild successful!")
        print(f"  Executable: {exe_path}")
        print(f"  Directory:  {output_dir}")
        print()
        print("To use for dev testing:")
        print(f'  1. Copy the entire "{output_dir}" folder to a deploy location')
        print(f"  2. Run with a config:")
        print(f'     {exe_name} --config run_config.json')
        print(f"     or one-shot:")
        print(f'     {exe_name} --model <hf_id_or_dir> --prompt "..." --steps 4')
    else:
        print(f"\nWARNING: Expected executable not found at {exe_path}")
        print(f"Check {dist_dir} for output.")


if __name__ == "__main__":
    main()
