"""Standalone harness for the Diffusers EP. Dev-only.

Usage:
    standalone_runner --config <run.json> [--prompts <prompts.json>]
    standalone_runner --model <hf_id_or_dir> --prompt "..." [--steps N]
"""

import argparse
import hashlib
import json
import math
import os
import statistics
import subprocess
import sys
import time
from pathlib import Path


_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if _SCRIPT_DIR not in sys.path:
    sys.path.insert(0, _SCRIPT_DIR)


def _load_json(path):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def _strip_file_uri(s):
    if not isinstance(s, str) or not s.startswith("file://"):
        return s
    s = s[len("file://"):]
    if os.name == "nt" and len(s) > 2 and s[0] == "/" and s[2] == ":":
        s = s[1:]
    return s


def _normalize_prompt(item):
    if isinstance(item, str):
        return item, ""
    if isinstance(item, dict):
        return item.get("positive", ""), item.get("negative", "")
    raise ValueError(f"unrecognized prompt entry: {item!r}")


def _resolve_relative(base_path, value):
    p = Path(_strip_file_uri(value))
    if not p.is_absolute():
        p = Path(base_path).resolve().parent / p
    return p


def _load_prompts_file(path):
    data = _load_json(path)
    img = (data.get("model_config") or {}).get("image_generation") or {}
    prompts = data.get("prompts") or []
    if not prompts:
        raise ValueError(f"{path}: no prompts")
    return img, prompts


def _maybe_pre_quantize(cfg):
    src = _strip_file_uri(cfg.get("Model", {}).get("FilePath", ""))
    q = cfg.get("Quantize")
    if not q:
        return src

    method = q.get("Method")
    out = _strip_file_uri(q.get("OutputDir", ""))
    if not method or not out:
        raise ValueError("Quantize requires 'Method' and 'OutputDir'")

    out_p = Path(out)
    if out_p.is_dir() and any(out_p.iterdir()):
        return str(out_p)

    out_p.mkdir(parents=True, exist_ok=True)
    args = ["--input", src, "--output", str(out_p), "--method", method]
    if q.get("Components"):
        args.append("--components")
        args.extend(q["Components"])

    if getattr(sys, "frozen", False):
        # Frozen: sys.executable is the EXE, not a python interpreter.
        import quantize_model
        rc = quantize_model.main(args)
    else:
        cmd = [sys.executable, str(Path(_SCRIPT_DIR) / "quantize_model.py"),
               *args]
        rc = subprocess.run(cmd).returncode
    if rc != 0:
        raise RuntimeError(f"quantize_model failed (rc={rc})")
    return str(out_p)


def _parse_args(argv=None):
    p = argparse.ArgumentParser(description="Diffusers EP standalone harness")
    p.add_argument("--config", type=str, default=None)
    p.add_argument("--prompts", type=str, default=None,
                   help="Prompts file. Defaults to cfg.InputFilePath.")
    p.add_argument("--model", type=str, default=None)
    p.add_argument("--base-name", type=str, default="")
    p.add_argument("--prompt", type=str, default=None)
    p.add_argument("--device-type", type=str, default="CUDA",
                   choices=("CUDA", "MPS"))
    p.add_argument("--device-id", type=int, default=0)
    p.add_argument("--gpu-mode", type=str, default="",
                   choices=("", "direct", "cpu_offload", "sequential_offload"))
    p.add_argument("--steps", type=int, default=4)
    p.add_argument("--guidance-scale", type=float, default=0.0)
    p.add_argument("--width", type=int, default=1024)
    p.add_argument("--height", type=int, default=1024)
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--output-dir", type=str, default=None,
                   help="Images + results.json destination. Overrides "
                        "cfg.OutputDir; defaults to ./out.")
    p.add_argument("-d", "--download", nargs="?", const="", default=None,
                   metavar="CACHE_DIR",
                   help="Enable HF model download. Optional cache dir; "
                        "defaults to MLPERF_DIFFUSERS_CACHE or "
                        ".cache/models.")
    return p.parse_args(argv)


def _stats(values):
    if not values:
        return {"avg": 0.0, "min": 0.0, "max": 0.0, "stdev": 0.0, "n": 0}
    return {
        "avg": sum(values) / len(values),
        "min": min(values),
        "max": max(values),
        "stdev": statistics.pstdev(values) if len(values) > 1 else 0.0,
        "n": len(values),
    }


def main(argv=None):
    args = _parse_args(argv)

    if args.config:
        cfg = _load_json(args.config)
        cfg_path = args.config
    elif args.model and args.prompt:
        cfg = {
            "Name": "cli",
            "Model": {"BaseName": args.base_name, "FilePath": args.model},
            "Device": {"backend": args.device_type,
                       "device_id": args.device_id,
                       "gpu_mode": args.gpu_mode},
            "Iterations": 1, "WarmUp": 0,
            "SaveImages": True,
        }
        cfg_path = os.getcwd()
    else:
        print("error: --config or (--model and --prompt) required",
              file=sys.stderr)
        return 2

    prompts_path = None
    if args.prompts:
        prompts_path = Path(args.prompts).resolve()
    elif cfg.get("InputFilePath"):
        prompts_path = _resolve_relative(cfg_path, cfg["InputFilePath"])

    if prompts_path:
        img_cfg, prompts = _load_prompts_file(prompts_path)
    else:
        img_cfg = {"width": args.width, "height": args.height,
                   "num_inference_steps": args.steps,
                   "guidance_scale": args.guidance_scale, "seed": args.seed}
        prompts = [args.prompt]

    from diffusers_inference import ImageInference

    model_cfg = cfg.get("Model", {})
    device_cfg = cfg.get("Device", {})

    effective_model = _maybe_pre_quantize(cfg)
    if not effective_model:
        raise ValueError("Model.FilePath is required")
    if effective_model.endswith("model_index.json"):
        effective_model = os.path.dirname(effective_model)

    width = int(img_cfg.get("width", 1024))
    height = int(img_cfg.get("height", 1024))
    steps = int(img_cfg.get("num_inference_steps", 4))
    guidance_scale = float(img_cfg.get("guidance_scale", 0.0))
    seed = int(img_cfg.get("seed", 42))
    num_images_per_prompt = int(img_cfg.get("num_images_per_prompt", 4))
    seeds = [int(s) for s in (img_cfg.get("seeds") or [seed])]

    iterations = int(cfg.get("Iterations", 1))
    warmup = int(cfg.get("WarmUp", 0))
    save_images = bool(cfg.get("SaveImages", True))
    output_dir = Path(args.output_dir or cfg.get("OutputDir") or "./out")
    output_dir.mkdir(parents=True, exist_ok=True)

    download_cache = None
    if args.download is not None:
        download_cache = (
            args.download
            or os.environ.get("MLPERF_DIFFUSERS_CACHE")
            or os.path.join(".", ".cache", "models")
        )

    pipe = ImageInference()
    opts_raw = device_cfg.get("optimizations")
    opts_json = json.dumps(opts_raw) if opts_raw else ""

    # CudaTimer wraps a torch.cuda.synchronize() around the scope when CUDA
    # is available so setup.load reflects actual GPU completion. Falls back
    # to wall clock on CPU/MPS.
    from timing.timer import CudaTimer
    load_timer = CudaTimer()
    with load_timer.scope("setup.load"):
        pipe.load(
            effective_model,
            device_cfg.get("backend", "CUDA"),
            int(device_cfg.get("device_id", 0)),
            model_base_name=model_cfg.get("BaseName", ""),
            gpu_mode=device_cfg.get("gpu_mode", ""),
            quantization=device_cfg.get("quantization", ""),
            component_files=device_cfg.get("components") or {},
            pipeline_class=device_cfg.get("pipeline", "") or "",
            hf_base_id=device_cfg.get("hf_base_id", "") or "",
            optimizations_json=opts_json,
            download_cache=download_cache,
        )
    load_seconds = load_timer.records[-1].elapsed_ms / 1000.0

    e2e_seconds = []
    first_step_seconds = []
    steps_per_second = []
    warmup_seconds_total = 0.0
    results = []
    submodule_timings: dict[str, list[float]] = {}

    def _submodule_cb(name: str, ms: float):
        submodule_timings.setdefault(name, []).append(ms)

    def _run_one(prompt_item, label, save, count_metrics):
        pos, neg = _normalize_prompt(prompt_item)
        step_ts = []
        gen_start = [None]

        def _cb(step, total):
            now = time.perf_counter()
            if gen_start[0] is None:
                gen_start[0] = now
            step_ts.append(now)

        cb = _submodule_cb if count_metrics else None

        e2e_timer = CudaTimer()
        with e2e_timer.scope("end_to_end"):
            image = pipe.generate(
                prompt=pos, negative_prompt=neg,
                width=width, height=height,
                num_inference_steps=steps,
                guidance_scale=guidance_scale,
                seed=seed,
                num_images_per_prompt=num_images_per_prompt,
                seeds=seeds,
                step_callback=_cb,
                submodule_callback=cb,
            )
        iter_e2e = e2e_timer.records[-1].elapsed_ms / 1000.0

        out_path = None
        if save:
            h = hashlib.md5(pos.encode("utf-8"), usedforsecurity=False).hexdigest()[:8]
            # image is [N, H, W, 3]; save each generated image with its seed.
            batch = image if image.ndim == 4 else image[None, ...]
            for k in range(batch.shape[0]):
                seed_k = seeds[k] if k < len(seeds) else seed
                img_path = output_dir / f"{label}_img{k}_{h}.rgb"
                with img_path.open("wb") as f:
                    f.write(batch[k].tobytes())
                with (output_dir / f"{label}_img{k}_{h}.json").open(
                        "w", encoding="utf-8") as f:
                    json.dump({"width": width, "height": height,
                               "format": "RGB", "channels": 3,
                               "prompt": pos, "negative_prompt": neg,
                               "seed": seed_k, "steps": steps}, f, indent=2)
                if out_path is None:
                    out_path = img_path

        if count_metrics and step_ts and gen_start[0] is not None:
            gen_dur = step_ts[-1] - gen_start[0]
            if gen_dur > 0.0:
                steps_per_second.append(len(step_ts) / gen_dur)
            first_step_seconds.append(step_ts[0] - gen_start[0])
            # generate() returns [N, H, W, 3]; count per-image e2e so
            # Images/Min reflects every image, not one N-image batch.
            n_imgs = image.shape[0] if getattr(image, "ndim", 0) == 4 else 1
            e2e_seconds.append(iter_e2e / max(1, n_imgs))

        return {"label": label, "prompt": pos,
                "end_to_end_seconds": iter_e2e,
                "output_path": str(out_path) if out_path else None}

    for w in range(warmup):
        for prompt in prompts:
            r = _run_one(prompt, f"warmup{w}", save=False, count_metrics=False)
            warmup_seconds_total += r["end_to_end_seconds"]

    for i in range(iterations):
        for j, prompt in enumerate(prompts):
            r = _run_one(prompt, f"iter{i}p{j}",
                         save=save_images, count_metrics=True)
            results.append(r)

    system_info = {}
    try:
        if hasattr(pipe, "_vendor") and pipe._vendor is not None:
            system_info = pipe._vendor.system_info()
    except Exception:
        pass

    submodule_summary = {}
    for name, durations in submodule_timings.items():
        submodule_summary[name] = {
            "total_ms": round(sum(durations), 3),
            "per_call_ms": _stats(durations),
            "calls": len(durations),
        }

    e2e_stats = _stats(e2e_seconds)
    summary = {
        "config_name": cfg.get("Name"),
        "model_path": effective_model,
        "device": device_cfg,
        "system_info": system_info,
        "iterations": iterations,
        "warmup": warmup,
        "setup_load_seconds": load_seconds,
        "setup_warmup_seconds": warmup_seconds_total,
        "end_to_end_seconds": {
            "avg": e2e_stats["avg"], "min": e2e_stats["min"],
            "max": e2e_stats["max"], "stdev": e2e_stats["stdev"],
        },
        "first_step_seconds": _stats(first_step_seconds)["avg"],
        "steps_per_second": _stats(steps_per_second)["avg"],
        "submodule_timings_ms": submodule_summary,
        "results": results,
    }
    with (output_dir / "results.json").open("w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2)

    # TEMP: human-readable summary mirroring the C++ runner's console output.
    e2e_avg = e2e_stats["avg"]
    images_per_min = (60.0 / e2e_avg) if e2e_avg > 0 else 0.0
    steps_per_sec = _stats(steps_per_second)["avg"]
    first_step = _stats(first_step_seconds)["avg"]
    print(
        f"Images/Min: {images_per_min:.2f}, Steps/Sec: {steps_per_sec:.2f}, "
        f"Avg End-to-End: {e2e_avg:.3f} s ({width}x{height}, {steps} steps)\n"
        f"E2E min/max/stdev: {e2e_stats['min']:.3f} / {e2e_stats['max']:.3f} / "
        f"{e2e_stats['stdev']:.3f} s, First-Step: {first_step:.3f} s\n"
        f"Setup load/warmup: {load_seconds:.3f} / {warmup_seconds_total:.3f} s"
    )

    pipe.unload()
    return 0


if __name__ == "__main__":
    _rc = main()
    if getattr(sys, "frozen", False):
        # Belt-and-suspenders against frozen-bundle teardown noise.
        # diffusers_inference._silence_multiprocessing_for_embedded already
        # neuters multiprocessing.resource_tracker upfront, but other atexit
        # handlers (torch's, future deps') can still re-exec sys.executable
        # (= the EXE, not python) and emit ModuleNotFoundError on shutdown.
        # Flush + os._exit past the cleanup so the parent never sees it.
        import logging
        logging.shutdown()
        sys.stdout.flush()
        sys.stderr.flush()
        os._exit(_rc or 0)
    raise SystemExit(_rc)
