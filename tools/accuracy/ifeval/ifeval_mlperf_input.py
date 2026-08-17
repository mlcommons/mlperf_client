"""Build MLPerf input_file.json for IFEval with model-specific chat framing."""

from __future__ import annotations

import sys
from copy import deepcopy
from pathlib import Path
from typing import Any

_accuracy_root = Path(__file__).resolve().parent.parent
if str(_accuracy_root) not in sys.path:
    sys.path.insert(0, str(_accuracy_root))

from mlperf_common import format_chat_prompts  # noqa: E402


def build_input_file(
    template: dict[str, Any],
    prompts: list[str],
    run_template: dict[str, Any] | None = None,
    prompt_format: str = "auto",
    tokenizer=None,
    system_prompt: str | None = None,
    enable_thinking: bool | None = None,
) -> dict[str, Any]:
    """Build an input_file.json dict for IFEval.

    prompt_format:
      - "auto": pre-apply the tokenizer's chat template here (Python), honoring
        system_prompt and enable_thinking.
      - "raw": pass prompt text through unchanged.
      - "app": don't template here; emit raw/object-form prompts plus
        apply_chat_template=true and enable_thinking so the C++ app applies the
        chat template (and thinking toggle) at inference time.
    """
    cfg = deepcopy(template)
    mc = cfg.setdefault("model_config", {})
    search = mc.setdefault(
        "search",
        {
            "method": "greedy",
            "num_beams": 1,
            "temperature": 0,
            "top_k": 1,
            "stop_on_eos": True,
            "max_length": 4096,
        },
    )
    for k, v in {
        "method": "greedy",
        "num_beams": 1,
        "temperature": 0,
        "top_k": 1,
        "stop_on_eos": True,
        "max_length": 4096,
    }.items():
        search.setdefault(k, v)

    if (prompt_format or "auto").strip().lower() == "app":
        # Delegate chat templating to the C++ app (see txt2txt_executor.cpp).
        cfg["apply_chat_template"] = True
        if enable_thinking is not None:
            cfg["enable_thinking"] = enable_thinking
        if system_prompt:
            cfg["prompts"] = [{"system": system_prompt, "user": p} for p in prompts]
        else:
            cfg["prompts"] = list(prompts)
        return cfg

    out_prompts = format_chat_prompts(
        tokenizer, prompts, system_prompt=system_prompt, prompt_format=prompt_format,
        enable_thinking=enable_thinking,
    )

    cfg["prompts"] = out_prompts
    return cfg
