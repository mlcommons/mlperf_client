"""Refresh the vendored MLCommons mobile IFEval scorer files.

This updates the scoring package code only. It intentionally does not overwrite
the locally kept `data/input_data.jsonl`, because this tree uses the 541-prompt
dataset rather than mobile_open's 540-prompt copy.
"""

from __future__ import annotations

import os
import urllib.request

BASE = "https://raw.githubusercontent.com/mlcommons/mobile_open/main/llm/instruction_following_eval"

FILES = [
    "evaluation_lib.py",
    "evaluation_main.py",
    "instructions.py",
    "instructions_registry.py",
    "instructions_util.py",
    "README.md",
    "requirements.txt",
]


def main() -> None:
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "instruction_following_eval")
    for name in FILES:
        url = f"{BASE}/{name}"
        path = os.path.join(root, name)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        req = urllib.request.Request(url, headers={"User-Agent": "ifeval-fetch"})
        with urllib.request.urlopen(req, timeout=120) as r:
            data = r.read()
        with open(path, "wb") as f:
            f.write(data)
        print(f"{name}\t{len(data)} bytes")

    init_path = os.path.join(root, "__init__.py")
    with open(init_path, "w", encoding="utf-8") as f:
        f.write('"""Vendored from mlcommons/mobile_open llm/instruction_following_eval (Apache-2.0)."""\n')
    print("__init__.py\t(written)")
    print(f"OK -> {root}")


if __name__ == "__main__":
    main()
