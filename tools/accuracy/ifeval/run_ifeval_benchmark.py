"""
Run IFEval via MLPerf + model-specific chat framing, then score with the vendored instruction_following_eval package.

Llama 3.1 prompts are rewritten to the same instruct chat framing as ``data/llama3/greedy-prompt_*.json``.
Phi 3.5 prompts are rewritten to the official ChatML-like format from the model card.

Resumable layouts (see IFEvalGroupBy in JSON config):

- per_prompt: k<key>/results.json - one MLPerf job per prompt (max checkpointing, reloads the model every time).
- instruction_signature: grp_<hash>/ - same as MMLU \"one folder per subject\": all prompts that
  share the same *set* of instruction IDs (order-independent), ~210 jobs on the standard set.
- category_set: grp_<hash>/ - coarser: group by sorted set of namespaces (keywords:, length_:, ...),
  ~65 jobs.
- fixed: job_00000/ ... - sequential batches of PromptsPerMLPerfJob (e.g. 10); one MLPerf invocation per batch, no semantic grouping.

MMLU analogy: TinyMMLU uses one MLPerf run per *subject* (many prompts per run, reload per subject).
IFEval \"instruction_signature\" is the closest match - one run per *constraint signature*, not
per arbitrary chunk of 10.

Resume: set \"RunID\" to the partial run and re-run; finished folders are skipped.
Failed MLPerf jobs are logged, a stub results.json is written, and the run continues (see mlperf_common.run_mlperf continue_on_job_failure).
IFEval passes skip_if_results_match_prompt_count so jobs with stub or failed-but-complete Output lists are not re-run.

From the repository root:
  python tools/accuracy/ifeval/run_ifeval_benchmark.py -c tools/accuracy/ifeval/ifeval-NVIDIA_llamacpp-CUDA_GPU.json
  python tools/accuracy/ifeval/run_ifeval_benchmark.py -c tools/accuracy/ifeval/ifeval-NVIDIA_WindowsML-NvTensorRtRtx_GPU.json
  python tools/accuracy/ifeval/run_ifeval_benchmark.py -c tools/accuracy/ifeval/ifeval-NVIDIA_llamacpp-CUDA_GPU_phi3.5.json
  python tools/accuracy/ifeval/run_ifeval_benchmark.py -c tools/accuracy/ifeval/ifeval-NVIDIA_ORTGenAI-DML_GPU_phi3.5.json
  python tools/accuracy/ifeval/run_ifeval_benchmark.py -c tools/accuracy/ifeval/ifeval-NVIDIA_WindowsML-NvTensorRtRtx_GPU_phi3.5.json
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
import time
from collections import defaultdict
from pathlib import Path

from loguru import logger
_ifeval_dir = Path(__file__).resolve().parent
_accuracy_root = _ifeval_dir.parent
for p in (_ifeval_dir, _accuracy_root):
    s = str(p)
    if s not in sys.path:
        sys.path.insert(0, s)

from mlperf_common import (  # noqa: E402
    download_tokenizer,
    generate_mlperf_config_file,
    get_run_id,
    read_last_json_line,
    run_mlperf,
)
from ifeval_mlperf_input import build_input_file  # noqa: E402

BENCHMARK_TYPE = "ifeval"

GROUP_MODES = frozenset({"per_prompt", "fixed", "instruction_signature", "category_set"})

evaluation_lib = None


def _file_uri(path: Path) -> str:
    return "file://" + str(path.resolve())


def _execution_provider_name(run_template: dict) -> str:
    try:
        return str(run_template["Scenarios"][0]["ExecutionProviders"][0].get("Name", "")).lower()
    except (KeyError, IndexError, TypeError):
        return ""


def _scenario_name(run_template: dict) -> str:
    try:
        return str(run_template["Scenarios"][0].get("Name", "")).lower()
    except (KeyError, IndexError, TypeError):
        return ""


def _is_windowsml(run_template: dict) -> bool:
    return _execution_provider_name(run_template) == "windowsml"


def _is_winml_llama31(run_template: dict) -> bool:
    return _scenario_name(run_template) == "llama3" and _is_windowsml(run_template)


def _apply_winml_local_model_uris(run_template: dict, local_dir: Path) -> None:
    onnx = local_dir / "model.onnx"
    weights = local_dir / "model.onnx.data"
    tok = local_dir / "tokenizer.zip"
    for p in (onnx, weights, tok):
        if not p.is_file():
            raise FileNotFoundError(f"WinML local bundle missing: {p}")
    for sc in run_template.get("Scenarios", []):
        for m in sc.get("Models", []):
            m["FilePath"] = _file_uri(onnx)
            m["DataFilePath"] = _file_uri(weights)
            m["TokenizerPath"] = _file_uri(tok)


def _prepare_winml_temp_root(run_dir: Path) -> Path:
    """
    Use a run-local TempPath for WinML jobs.
    """
    temp_root = run_dir / "_winml_temp"
    temp_root.mkdir(parents=True, exist_ok=True)
    return temp_root


def _load_prepared_run_template(config: dict, run_dir: Path, winml_llama31_local_bundle: Path | None) -> dict:
    with open(config["RunConfigPath"], encoding="utf-8") as f:
        run_template = json.load(f)

    if _is_windowsml(run_template):
        run_template.setdefault("SystemConfig", {})["TempPath"] = str(_prepare_winml_temp_root(run_dir))
        logger.info(f"WinML IFEval: TempPath = {run_template['SystemConfig']['TempPath']}")

    if _is_winml_llama31(run_template) and winml_llama31_local_bundle is not None:
        _apply_winml_local_model_uris(run_template, winml_llama31_local_bundle)
        logger.info(f"WinML IFEval: using local TRT bundle {winml_llama31_local_bundle}")

    return run_template


def semantic_sort_key(ex: evaluation_lib.InputExample, group_by: str) -> tuple:
    if group_by == "instruction_signature":
        return tuple(sorted(ex.instruction_id_list))
    if group_by == "category_set":
        cats = {i.split(":", 1)[0] for i in ex.instruction_id_list if ":" in i}
        return tuple(sorted(cats))
    raise ValueError(group_by)


def group_dir_hash(sort_key: tuple) -> str:
    return hashlib.sha256(repr(sort_key).encode("utf-8")).hexdigest()[:16]


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="IFEval benchmark via MLPerf (resumable per prompt)")
    p.add_argument("-c", "--config", required=True, help="JSON config (e.g. ifeval-NVIDIA_llamacpp-CUDA_GPU.json)")
    p.add_argument(
        "--instruction-following-eval-dir",
        required=True,
        help="Path to the instruction_following_eval package directory.",
    )
    p.add_argument(
        "--canonical-data-path",
        required=True,
        help="Path to the full canonical IFEval input_data.jsonl used for total-score denominators.",
    )
    p.add_argument(
        "--winml-llama31-local-bundle",
        help="Optional local WinML Llama 3.1 TRT bundle containing model.onnx, model.onnx.data, and tokenizer.zip.",
    )
    p.add_argument(
        "--output-dir",
        help="Optional override for OutputDir from the JSON config.",
    )
    p.add_argument("-r", "--run-config", help="Path to a vendor default config (overrides RunConfigPath in the config file)")
    p.add_argument("-p", "--program", help="Path to mlperf-windows.exe (overrides MLPerfProgramPath in the config file)")
    p.add_argument("-v", "--verbose", action="store_true")
    p.add_argument("--postprocess-only", action="store_true")
    p.add_argument("--run-id", type=int, help="Run ID for --postprocess-only")
    p.add_argument(
        "--result-only",
        nargs="+",
        metavar="RUN_DIR",
        help="Combine one or more existing partial IFEval run directories and score the merged responses without running MLPerf",
    )
    return p.parse_args()


def load_examples(data_path: Path) -> list[evaluation_lib.InputExample]:
    examples: list[evaluation_lib.InputExample] = []
    with open(data_path, encoding="utf-8") as f:
        for line in f:
            o = json.loads(line)
            examples.append(
                evaluation_lib.InputExample(
                    key=o["key"],
                    instruction_id_list=o["instruction_id_list"],
                    prompt=o["prompt"],
                    kwargs=o["kwargs"],
                )
            )
    return examples


def examples_to_jsonable(examples: list[evaluation_lib.InputExample]) -> list[dict]:
    return [
        {
            "key": e.key,
            "instruction_id_list": e.instruction_id_list,
            "prompt": e.prompt,
            "kwargs": e.kwargs,
        }
        for e in examples
    ]


def load_examples_from_meta(path: Path) -> list[evaluation_lib.InputExample]:
    with open(path, encoding="utf-8") as f:
        raw = json.load(f)
    return [
        evaluation_lib.InputExample(
            key=o["key"],
            instruction_id_list=o["instruction_id_list"],
            prompt=o["prompt"],
            kwargs=o["kwargs"],
        )
        for o in raw
    ]


def load_full_benchmark_examples(canonical_data_path: Path) -> list[evaluation_lib.InputExample]:
    if not canonical_data_path.is_file():
        raise SystemExit(f"Missing canonical IFEval dataset: {canonical_data_path}")
    return load_examples(canonical_data_path)


def load_model_responses(path: Path) -> dict[int, tuple[str, str]]:
    """
    Load key -> (prompt, response) from a model_responses.jsonl file.

    Later lines for the same key win so reruns can overwrite earlier partial data.
    """
    responses: dict[int, tuple[str, str]] = {}
    with open(path, encoding="utf-8") as f:
        for lineno, line in enumerate(f, start=1):
            line = line.strip()
            if not line:
                continue
            obj = json.loads(line)
            key = int(obj["key"])
            prompt = str(obj.get("prompt", ""))
            response = str(obj.get("response", "") or "").strip()
            if key in responses:
                logger.warning(f"{path}:{lineno}: duplicate key {key}; later response will overwrite earlier one")
            responses[key] = (prompt, response)
    return responses


def resolve_path(path_text: str | os.PathLike[str], base_dir: Path) -> Path:
    path = Path(path_text)
    if path.is_absolute():
        return path
    return (base_dir / path).resolve()


def prepare_imports(instruction_following_eval_dir: Path) -> None:
    global evaluation_lib

    package_dir = instruction_following_eval_dir.resolve()
    if not package_dir.is_dir():
        raise SystemExit(f"Missing instruction_following_eval directory: {package_dir}")

    parent = str(package_dir.parent)
    if parent not in sys.path:
        sys.path.insert(0, parent)

    from instruction_following_eval import evaluation_lib as loaded_evaluation_lib

    evaluation_lib = loaded_evaluation_lib


def _score_prompt_mapping(
    config: dict,
    benchmark_examples: list[evaluation_lib.InputExample],
    elapsed: float,
    run_dir: Path,
    group_by: str,
    prompt_to_response: dict[str, str],
    stats: dict[str, int],
    run_manifest: dict | None = None,
    covered_keys: set[int] | None = None,
) -> None:
    """
    Write scoring artifacts and the summary report for a completed or merged run.
    """
    strict_out: list[evaluation_lib.OutputExample] = []
    loose_out: list[evaluation_lib.OutputExample] = []
    benchmark_prompt_to_response = {inp.prompt: prompt_to_response.get(inp.prompt, "") for inp in benchmark_examples}
    for inp in benchmark_examples:
        strict_out.append(evaluation_lib.test_instruction_following_strict(inp, benchmark_prompt_to_response))
        loose_out.append(evaluation_lib.test_instruction_following_loose(inp, benchmark_prompt_to_response))

    strict_path = run_dir / "eval_results_strict.jsonl"
    loose_path = run_dir / "eval_results_loose.jsonl"
    evaluation_lib.write_outputs(str(strict_path), strict_out)
    evaluation_lib.write_outputs(str(loose_path), loose_out)

    num_total_prompts = len(benchmark_examples)
    covered_keys = {e.key for e in benchmark_examples} if covered_keys is None else set(covered_keys)
    covered_examples = [e for e in benchmark_examples if e.key in covered_keys]
    num_covered_prompts = len(covered_examples)
    num_responses = sum(1 for e in covered_examples if (prompt_to_response.get(e.prompt, "") or "").strip())
    strict_pass_count = sum(
        evaluation_lib.test_instruction_following_strict(inp, benchmark_prompt_to_response).follow_all_instructions
        for inp in covered_examples
    )
    loose_pass_count = sum(
        evaluation_lib.test_instruction_following_loose(inp, benchmark_prompt_to_response).follow_all_instructions
        for inp in covered_examples
    )

    def _ratio(num: int, den: int) -> float:
        return float(num / den) if den else 0.0

    strict_attempted_acc = _ratio(strict_pass_count, num_covered_prompts)
    loose_attempted_acc = _ratio(loose_pass_count, num_covered_prompts)
    strict_total_acc = _ratio(strict_pass_count, num_total_prompts)
    loose_total_acc = _ratio(loose_pass_count, num_total_prompts)
    response_attempted_cov = _ratio(num_responses, num_covered_prompts)
    response_total_cov = _ratio(num_responses, num_total_prompts)

    responses_path = run_dir / "model_responses.jsonl"
    with open(responses_path, "w", encoding="utf-8") as f:
        for e in benchmark_examples:
            r = benchmark_prompt_to_response.get(e.prompt, "")
            f.write(json.dumps({"key": e.key, "prompt": e.prompt, "response": r}, ensure_ascii=False) + "\n")

    with open(config["RunConfigPath"], encoding="utf-8") as f:
        run_cfg = json.load(f)

    manifest = run_manifest or {}
    report = {
        "Benchmark": "IFEval",
        "TotalDuration": elapsed,
        "NumPrompts": num_total_prompts,
        "NumAttemptedPrompts": num_covered_prompts,
        "NumCoveredPrompts": num_covered_prompts,
        "NumResponses": num_responses,
        "NumCompletedMLPerf": stats.get("completed", 0),
        "NumMissingResults": stats.get("missing", 0),
        "NumFailedBenchmark": stats.get("failed_benchmark", 0),
        "NumEmptyResponses": stats.get("empty_response", 0),
        "IFEvalGroupBy": group_by,
        "PromptsPerMLPerfJob": int(manifest.get("prompts_per_job", 1)),
        "NumMLPerfJobs": int(manifest.get("num_mlperf_jobs", 0)),
        "StrictAccuracy": strict_total_acc,
        "LooseAccuracy": loose_total_acc,
        "StrictAccuracyAttempted": strict_attempted_acc,
        "LooseAccuracyAttempted": loose_attempted_acc,
        "ResponseCoverageAttempted": response_attempted_cov,
        "ResponseCoverageTotal": response_total_cov,
        "StrictFollowAll": strict_pass_count,
        "LooseFollowAll": loose_pass_count,
        "RunConfigPath": config["RunConfigPath"],
        "RunDirectory": str(run_dir.resolve()),
        "Scenarios": run_cfg.get("Scenarios", []),
    }
    if manifest:
        report["RunManifest"] = manifest
        report["RunMode"] = manifest.get("mode", "run")
        if "sources" in manifest:
            report["NumResultSources"] = len(manifest.get("sources", []))
    report_path = run_dir / "ifeval_report.json"
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, ensure_ascii=False, indent=2)

    logger.info(
        f"MLPerf completed jobs: {stats.get('completed', 0)}/{num_covered_prompts} "
        f"(missing {stats.get('missing', 0)}, failed {stats.get('failed_benchmark', 0)})"
    )
    logger.info(
        f"IFEval responses: {num_responses}/{num_covered_prompts} covered "
        f"({response_attempted_cov:.4f}), {num_responses}/{num_total_prompts} total ({response_total_cov:.4f})"
    )
    logger.info(
        f"IFEval strict accuracy:  {strict_pass_count}/{num_covered_prompts} covered "
        f"({strict_attempted_acc:.4f}), {strict_pass_count}/{num_total_prompts} total ({strict_total_acc:.4f})"
    )
    logger.info(
        f"IFEval loose accuracy:   {loose_pass_count}/{num_covered_prompts} covered "
        f"({loose_attempted_acc:.4f}), {loose_pass_count}/{num_total_prompts} total ({loose_total_acc:.4f})"
    )
    logger.info(f"Wrote {report_path}")


def collect_responses(
    run_dir: Path,
    examples: list[evaluation_lib.InputExample],
    group_by: str,
) -> tuple[dict[str, str], dict[str, int]]:
    """Build prompt -> response; return stats."""
    stats = {"completed": 0, "missing": 0, "failed_benchmark": 0, "empty_response": 0}
    prompt_to_response: dict[str, str] = {}

    if group_by == "per_prompt":
        for ex in examples:
            rp = run_dir / f"k{ex.key}" / "results.json"
            if not rp.is_file() or rp.stat().st_size < 10:
                stats["missing"] += 1
                prompt_to_response[ex.prompt] = ""
                continue
            try:
                last = read_last_json_line(str(rp))
            except Exception as e:
                logger.warning(f"Invalid results {rp}: {e}")
                stats["failed_benchmark"] += 1
                prompt_to_response[ex.prompt] = ""
                continue
            outs = last.get("Output") or []
            text = (outs[0] or "").strip() if outs else ""
            if last.get("Benchmark Success") and text != "":
                stats["completed"] += 1
            elif text == "":
                stats["empty_response"] += 1
            else:
                stats["failed_benchmark"] += 1
            prompt_to_response[ex.prompt] = text
        return prompt_to_response, stats

    # fixed | instruction_signature | category_set - dirs with ifeval_job_meta.json
    job_dirs = sorted(
        p
        for p in run_dir.iterdir()
        if p.is_dir() and (p.name.startswith("job_") or p.name.startswith("grp_"))
    )
    # prompt_key -> (response text, whether MLPerf reported Benchmark Success for that job)
    key_state: dict[int, tuple[str, bool]] = {}
    for jd in job_dirs:
        meta_path = jd / "ifeval_job_meta.json"
        if not meta_path.is_file():
            continue
        with open(meta_path, encoding="utf-8") as f:
            meta = json.load(f)
        keys: list[int] = meta["keys"]
        rp = jd / "results.json"
        if not rp.is_file() or rp.stat().st_size < 10:
            continue
        try:
            last = read_last_json_line(str(rp))
        except Exception as e:
            logger.warning(f"{jd.name}: {e}")
            continue
        job_ok = bool(last.get("Benchmark Success"))
        outs = last.get("Output") or []
        skipped_prompt_sets = last.get("Skipped Prompts") or []
        skipped_prompt_idxs: set[int] = set()
        if skipped_prompt_sets and isinstance(skipped_prompt_sets[0], list):
            skipped_prompt_idxs = {int(i) for i in skipped_prompt_sets[0]}
        out_idx = 0
        for i, k in enumerate(keys):
            if i in skipped_prompt_idxs:
                text = ""
            else:
                text = (outs[out_idx] or "").strip() if out_idx < len(outs) else ""
                out_idx += 1
            key_state[k] = (text, job_ok)

    for ex in examples:
        st = key_state.get(ex.key)
        if st is None:
            stats["missing"] += 1
            prompt_to_response[ex.prompt] = ""
            continue
        text, job_ok = st
        prompt_to_response[ex.prompt] = text
        if text and job_ok:
            stats["completed"] += 1
        elif text == "":
            stats["empty_response"] += 1
            if not job_ok:
                stats["failed_benchmark"] += 1
        elif text and not job_ok:
            stats["failed_benchmark"] += 1
        else:
            stats["missing"] += 1

    return prompt_to_response, stats


def prepare_mlperf_jobs(
    config: dict,
    examples: list[evaluation_lib.InputExample],
    run_dir: Path,
    group_by: str,
    prompts_per_job: int,
    prompt_format: str,
    winml_llama31_local_bundle: Path | None,
    tokenizer=None,
    system_prompt: str | None = None,
    enable_thinking: bool | None = None,
) -> list[tuple[str, bool]]:
    run_dir.mkdir(parents=True, exist_ok=True)
    template = config["InputConfigTemplate"]

    manifest_path = run_dir / "ifeval_examples.json"
    with open(manifest_path, "w", encoding="utf-8") as f:
        json.dump(examples_to_jsonable(examples), f, ensure_ascii=False, indent=2)

    run_template = _load_prepared_run_template(config, run_dir, winml_llama31_local_bundle)

    all_pairs: list[tuple[str, bool]] = []

    if group_by == "per_prompt":
        for ex in examples:
            sub = run_dir / f"k{ex.key}"
            sub.mkdir(parents=True, exist_ok=True)
            inp = sub / "input_file.json"
            with open(inp, "w", encoding="utf-8") as f:
                json.dump(
                    build_input_file(template, [ex.prompt], prompt_format=prompt_format,
                                     tokenizer=tokenizer, system_prompt=system_prompt,
                                     enable_thinking=enable_thinking),
                    f,
                    ensure_ascii=False,
                    indent=2,
                )
            all_pairs.extend(
                generate_mlperf_config_file(
                    [str(inp.resolve())],
                    run_template,
                    base_dir_context=config,
                    skip_if_results_match_prompt_count=True,
                )
            )
        num_jobs = len(all_pairs)
    elif group_by == "fixed":
        if prompts_per_job < 1:
            raise ValueError("PromptsPerMLPerfJob must be >= 1 for IFEvalGroupBy=fixed")
        for ci in range(0, len(examples), prompts_per_job):
            chunk = examples[ci : ci + prompts_per_job]
            sub = run_dir / f"job_{ci // prompts_per_job:05d}"
            sub.mkdir(parents=True, exist_ok=True)
            with open(sub / "ifeval_job_meta.json", "w", encoding="utf-8") as f:
                json.dump(
                    {"keys": [e.key for e in chunk], "start_index": ci, "group_by": "fixed"},
                    f,
                    indent=2,
                )
            inp = sub / "input_file.json"
            prompts = [e.prompt for e in chunk]
            with open(inp, "w", encoding="utf-8") as f:
                json.dump(
                    build_input_file(template, prompts, prompt_format=prompt_format,
                                     tokenizer=tokenizer, system_prompt=system_prompt,
                                     enable_thinking=enable_thinking),
                    f,
                    ensure_ascii=False,
                    indent=2,
                )
            all_pairs.extend(
                generate_mlperf_config_file(
                    [str(inp.resolve())],
                    run_template,
                    base_dir_context=config,
                    skip_if_results_match_prompt_count=True,
                )
            )
        num_jobs = len(all_pairs)
    elif group_by in ("instruction_signature", "category_set"):
        buckets: dict[str, list[evaluation_lib.InputExample]] = defaultdict(list)
        for ex in examples:
            sk = semantic_sort_key(ex, group_by)
            h = group_dir_hash(sk)
            buckets[h].append(ex)
        for h in sorted(buckets.keys()):
            group_ex = sorted(buckets[h], key=lambda e: e.key)
            sk0 = semantic_sort_key(group_ex[0], group_by)
            sub = run_dir / f"grp_{h}"
            sub.mkdir(parents=True, exist_ok=True)
            with open(sub / "ifeval_job_meta.json", "w", encoding="utf-8") as f:
                json.dump(
                    {
                        "keys": [e.key for e in group_ex],
                        "group_by": group_by,
                        "sort_key_repr": repr(sk0),
                    },
                    f,
                    ensure_ascii=False,
                    indent=2,
                )
            inp = sub / "input_file.json"
            prompts = [e.prompt for e in group_ex]
            with open(inp, "w", encoding="utf-8") as f:
                json.dump(
                    build_input_file(template, prompts, prompt_format=prompt_format,
                                     tokenizer=tokenizer, system_prompt=system_prompt,
                                     enable_thinking=enable_thinking),
                    f,
                    ensure_ascii=False,
                    indent=2,
                )
            all_pairs.extend(
                generate_mlperf_config_file(
                    [str(inp.resolve())],
                    run_template,
                    base_dir_context=config,
                    skip_if_results_match_prompt_count=True,
                )
            )
        num_jobs = len(all_pairs)
    else:
        raise ValueError(f"Unknown IFEvalGroupBy: {group_by}")

    manifest = {
        "version": 2,
        "group_by": group_by,
        "prompts_per_job": prompts_per_job,
        "num_examples": len(examples),
        "num_mlperf_jobs": num_jobs,
        "written_at": time.time(),
    }
    with open(run_dir / "run_manifest.json", "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)

    return all_pairs


def postprocess(
    config: dict,
    examples: list[evaluation_lib.InputExample],
    elapsed: float,
    run_dir: Path,
    group_by: str,
    canonical_data_path: Path,
) -> None:
    manifest_path = run_dir / "run_manifest.json"
    run_manifest: dict = {}
    if manifest_path.is_file():
        with open(manifest_path, encoding="utf-8") as f:
            run_manifest = json.load(f)

    prompt_to_response, stats = collect_responses(run_dir, examples, group_by)
    _score_prompt_mapping(
        config,
        load_full_benchmark_examples(canonical_data_path),
        elapsed,
        run_dir,
        group_by,
        prompt_to_response,
        stats,
        run_manifest,
        covered_keys={e.key for e in examples},
    )


def combine_result_only_runs(
    config: dict,
    examples: list[evaluation_lib.InputExample],
    run_dir: Path,
    source_run_dirs: list[Path],
    canonical_data_path: Path,
) -> None:
    """
    Merge one or more partial runs by key and write a consolidated report.

    Later source directories have higher precedence for conflicting non-empty responses.
    """
    if not source_run_dirs:
        raise SystemExit("--result-only requires at least one source run directory")

    key_to_prompt: dict[int, str] = {e.key: e.prompt for e in examples}
    merged: dict[int, tuple[str, str]] = {}
    source_summaries: list[dict[str, object]] = []
    covered_keys: set[int] = set()

    for source_dir in source_run_dirs:
        source_dir = source_dir.resolve()
        responses_path = source_dir / "model_responses.jsonl"
        source_manifest: dict[str, object] = {}
        manifest_path = source_dir / "run_manifest.json"
        if manifest_path.is_file():
            with open(manifest_path, encoding="utf-8") as f:
                source_manifest = json.load(f)
        source_responses: dict[int, tuple[str, str]]
        source_non_empty = 0
        if responses_path.is_file():
            source_responses = load_model_responses(responses_path)
        else:
            source_examples_path = source_dir / "ifeval_examples.json"
            if not source_examples_path.is_file():
                raise SystemExit(f"Missing {responses_path} and {source_examples_path}; cannot combine results")
            source_examples = load_examples_from_meta(source_examples_path)
            source_group_by = str(source_manifest.get("group_by", "fixed") if source_manifest else "fixed").strip().lower()
            source_prompt_map, _ = collect_responses(source_dir, source_examples, source_group_by)
            source_responses = {ex.key: (ex.prompt, source_prompt_map.get(ex.prompt, "")) for ex in source_examples}

        source_examples_path = source_dir / "ifeval_examples.json"
        if source_examples_path.is_file():
            source_examples = load_examples_from_meta(source_examples_path)
            for ex in source_examples:
                covered_keys.add(ex.key)
        else:
            covered_keys.update(source_responses.keys())

        for key, (prompt, response) in source_responses.items():
            if key in key_to_prompt and prompt and prompt != key_to_prompt[key]:
                logger.warning(f"{source_dir.name}: key {key} prompt text differs from the master dataset")

            if response == "":
                continue

            source_non_empty += 1
            existing = merged.get(key)
            if existing is not None and existing[0] != response:
                logger.warning(
                    f"{source_dir.name}: key {key} has a conflicting non-empty response; later source overwrites earlier one"
                )
            merged[key] = (response, source_dir.as_posix())

        source_summaries.append(
            {
                "path": source_dir.as_posix(),
                "num_records": len(source_responses),
                "num_non_empty": source_non_empty,
                "num_attempted": len(source_responses),
                "manifest": source_manifest,
            }
        )

    prompt_to_response: dict[str, str] = {}
    stats = {"completed": 0, "missing": 0, "failed_benchmark": 0, "empty_response": 0}
    for ex in examples:
        merged_entry = merged.get(ex.key)
        response = (merged_entry[0] if merged_entry is not None else "").strip()
        prompt_to_response[ex.prompt] = response
        if response:
            stats["completed"] += 1
        else:
            stats["empty_response"] += 1
            stats["failed_benchmark"] += 1
            if merged_entry is None:
                stats["missing"] += 1

    combined_manifest = {
        "version": 3,
        "mode": "result_only",
        "num_examples": len(examples),
        "num_sources": len(source_run_dirs),
        "sources": source_summaries,
        "written_at": time.time(),
    }
    with open(run_dir / "run_manifest.json", "w", encoding="utf-8") as f:
        json.dump(combined_manifest, f, indent=2)

    with open(run_dir / "ifeval_examples.json", "w", encoding="utf-8") as f:
        json.dump(examples_to_jsonable(examples), f, ensure_ascii=False, indent=2)

    _score_prompt_mapping(
        config,
        load_full_benchmark_examples(canonical_data_path),
        0.0,
        run_dir,
        "result_only",
        prompt_to_response,
        stats,
        combined_manifest,
        covered_keys=covered_keys,
    )


def main() -> None:
    args = parse_args()
    if args.result_only and args.postprocess_only:
        raise SystemExit("--result-only cannot be combined with --postprocess-only")
    logger.remove()
    fmt = (
        "<green>{time:YYYY-MM-DD HH:mm:ss.SSS}</green> | "
        "<level>{level: <8}</level> | "
        "<cyan>{module}</cyan>:<cyan>{function}</cyan> - <level>{message}</level>"
    )
    logger.add(sys.stderr, format=fmt, level="DEBUG" if args.verbose else "INFO")

    invocation_cwd = Path.cwd()
    cfg_path = Path(args.config)
    if not cfg_path.is_absolute():
        cfg_path = (invocation_cwd / cfg_path).resolve()
    config_base = cfg_path.parent

    instruction_following_eval_dir = resolve_path(args.instruction_following_eval_dir, invocation_cwd)
    canonical_data_path = resolve_path(args.canonical_data_path, invocation_cwd)
    winml_llama31_local_bundle = (
        resolve_path(args.winml_llama31_local_bundle, invocation_cwd) if args.winml_llama31_local_bundle else None
    )
    output_dir_override = resolve_path(args.output_dir, invocation_cwd) if args.output_dir else None

    prepare_imports(instruction_following_eval_dir)

    with open(cfg_path, encoding="utf-8") as f:
        config = json.load(f)

    if args.program:
        config["MLPerfProgramPath"] = args.program
    if args.run_config:
        config["RunConfigPath"] = args.run_config

    config["IFEvalDataPath"] = str(resolve_path(config["IFEvalDataPath"], config_base))
    config["MLPerfProgramPath"] = str(resolve_path(config["MLPerfProgramPath"], config_base))
    config["RunConfigPath"] = str(resolve_path(config["RunConfigPath"], config_base))
    config["OutputDir"] = str(resolve_path(config["OutputDir"], config_base))
    if args.program:
        config["MLPerfProgramPath"] = str(resolve_path(args.program, invocation_cwd))
    if args.run_config:
        config["RunConfigPath"] = str(resolve_path(args.run_config, invocation_cwd))
    if output_dir_override is not None:
        config["OutputDir"] = str(output_dir_override)

    group_by = (config.get("IFEvalGroupBy") or "instruction_signature").strip().lower()
    if group_by not in GROUP_MODES:
        raise SystemExit(f"IFEvalGroupBy must be one of {sorted(GROUP_MODES)}, got {group_by!r}")

    prompts_per_job = max(1, int(config.get("PromptsPerMLPerfJob", 1) or 1))
    if group_by == "fixed" and prompts_per_job < 1:
        raise SystemExit("PromptsPerMLPerfJob must be >= 1 for IFEvalGroupBy=fixed")
    logger.info(f"IFEvalGroupBy={group_by}" + (f", PromptsPerMLPerfJob={prompts_per_job}" if group_by == "fixed" else ""))
    prompt_format = str(config.get("PromptFormat", "auto")).strip().lower()
    if prompt_format not in {"auto", "raw", "app"}:
        raise SystemExit("PromptFormat must be one of {'auto', 'raw', 'app'}")
    logger.info(f"PromptFormat={prompt_format}")

    _DEFAULT_SYSTEM_PROMPT = (
        "You are an AI assistant that helps people find information. "
        "Provide a detailed answer so user don't need to search outside to understand the answer."
    )
    system_prompt = config.get("SystemPrompt", _DEFAULT_SYSTEM_PROMPT)

    enable_thinking = config.get("EnableThinking", False)
    if enable_thinking is not None:
        logger.info(f"EnableThinking={enable_thinking} (forwarded to chat template if supported)")

    # Download tokenizer for chat template application (Python pre-apply only;
    # "raw" and "app" modes don't template in Python).
    tokenizer = None
    _tokenizer_temp_dir = None
    if prompt_format == "auto":
        with open(config["RunConfigPath"], encoding="utf-8") as f:
            _run_cfg = json.load(f)
        try:
            tokenizer, _tokenizer_temp_dir = download_tokenizer(_run_cfg)
            logger.info("Tokenizer loaded for chat template application")
        except Exception as e:
            logger.warning(f"Could not load tokenizer for chat template: {e}; prompts will not be wrapped")

    data_path = Path(config["IFEvalDataPath"])
    if not data_path.is_file():
        raise SystemExit(f"Missing dataset: {data_path.resolve()}")

    out_base = Path(config["OutputDir"])
    run_root = out_base / BENCHMARK_TYPE

    if args.result_only:
        source_run_dirs = [resolve_path(p, invocation_cwd) for p in args.result_only]
        for source_run_dir in source_run_dirs:
            if not source_run_dir.is_dir():
                raise SystemExit(f"Missing source run directory: {source_run_dir}")

        run_id = get_run_id(str(run_root), config.get("RunID"))
        run_dir = run_root / str(run_id)
        logger.info(f"Result-only output directory: {run_dir.resolve()}")

        examples = load_examples(data_path)
        logger.info(f"Loaded {len(examples)} IFEval prompts from dataset for result-only combine")
        run_dir.mkdir(parents=True, exist_ok=True)
        combine_result_only_runs(config, examples, run_dir, source_run_dirs, canonical_data_path)
        return

    if args.postprocess_only:
        if args.run_id is None:
            raise SystemExit("--run-id required with --postprocess-only")
        run_dir = run_root / str(args.run_id)
        meta = run_dir / "ifeval_examples.json"
        if not meta.is_file():
            raise SystemExit(f"Missing {meta}; cannot postprocess.")
        examples = load_examples_from_meta(meta)
        mpath = run_dir / "run_manifest.json"
        gb = group_by
        if mpath.is_file():
            with open(mpath, encoding="utf-8") as f:
                gb = json.load(f).get("group_by", gb)
        postprocess(config, examples, 0.0, run_dir, gb, canonical_data_path)
        return

    run_id = get_run_id(str(run_root), config.get("RunID"))
    run_dir = run_root / str(run_id)
    logger.info(f"Run directory: {run_dir.resolve()}")
    logger.info(f"Resume: re-run with \"RunID\": {run_id} in your config to continue this run.")

    mlperf_exe = Path(config["MLPerfProgramPath"])
    if not mlperf_exe.is_file():
        raise SystemExit(f"MLPerf not found: {mlperf_exe.resolve()}")

    examples = load_examples(data_path)
    logger.info(f"Loaded {len(examples)} IFEval prompts")

    generated = prepare_mlperf_jobs(
        config,
        examples,
        run_dir,
        group_by,
        prompts_per_job,
        prompt_format,
        winml_llama31_local_bundle,
        tokenizer=tokenizer,
        system_prompt=system_prompt,
        enable_thinking=enable_thinking,
    )
    pending = sum(1 for _, skip in generated if not skip)
    logger.info(f"MLPerf jobs: {len(generated)} total, {pending} still need to run")

    t0 = time.time()
    _, skipped = run_mlperf(
        str(mlperf_exe.resolve()),
        generated,
        skip_failed_prompts=False,
        continue_on_job_failure=True,
    )
    elapsed = time.time() - t0
    if skipped:
        logger.warning(f"MLPerf internal skip/repair path reports: {skipped}")

    postprocess(config, examples, elapsed, run_dir, group_by, canonical_data_path)


if __name__ == "__main__":
    main()

