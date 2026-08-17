"""Offline quantization tool. Writes a prequantized model directory."""

import argparse
import json
import os
import sys
import time
from pathlib import Path


def log(msg):
    print(f"[{time.strftime('%H:%M:%S')}] {msg}", flush=True)

_METHODS = (
    "quanto_int8", "quanto_int4", "quanto_float8",
    "torchao_int8", "torchao_fp8",
    "bnb_nf4",
)


def _parse_args(argv=None):
    p = argparse.ArgumentParser(
        description="Produce a prequantized model directory for the Diffusers EP.")
    p.add_argument("--input", required=True,
                   help="HuggingFace repo id or local model directory.")
    p.add_argument("--output", required=True,
                   help="Destination directory for the prequantized model.")
    p.add_argument("--method", required=True, choices=_METHODS)
    p.add_argument("--components", nargs="*", default=None,
                   help="Sub-folders to quantize. Defaults to "
                        "'transformer text_encoder' for Flux2 Klein, "
                        "'transformer' otherwise.")
    p.add_argument("--pipeline-class", default=None,
                   help="diffusers pipeline class; auto from model_index.json.")
    p.add_argument("--dtype", default="bfloat16",
                   choices=("bfloat16", "float16", "float32"))
    p.add_argument("--device", default=None,
                   help="Default: cuda if available, else cpu.")
    return p.parse_args(argv)


def _resolve_device(requested):
    import torch
    if requested:
        return requested
    if torch.cuda.is_available():
        return "cuda"
    if hasattr(torch.backends, "mps") and torch.backends.mps.is_available():
        return "mps"
    return "cpu"


def _resolve_dtype(name):
    import torch
    return {"bfloat16": torch.bfloat16,
            "float16": torch.float16,
            "float32": torch.float32}[name]


def _detect_pipeline_class(input_ref, override):
    import diffusers
    if override:
        cls = getattr(diffusers, override, None)
        if cls is None:
            raise ValueError(f"diffusers has no pipeline class '{override}'")
        return cls
    candidate = Path(input_ref) / "model_index.json"
    if candidate.is_file():
        with candidate.open("r", encoding="utf-8") as f:
            data = json.load(f)
        name = data.get("_class_name") or data.get("class_name")
        if name:
            cls = getattr(diffusers, name, None)
            if cls is not None:
                return cls
    return diffusers.DiffusionPipeline


def _default_components(input_ref):
    candidate = Path(input_ref) / "model_index.json"
    flux2 = False
    if candidate.is_file():
        try:
            data = json.loads(candidate.read_text(encoding="utf-8"))
            flux2 = "Flux2" in (data.get("_class_name") or "")
        except (OSError, ValueError):
            pass
    elif "flux.2-klein" in input_ref.lower() or "flux_2_klein" in input_ref.lower():
        flux2 = True
    return ["transformer", "text_encoder"] if flux2 else ["transformer"]


def _load_pipeline(input_ref, pipeline_cls, dtype):
    log(f"Loading source pipeline: {input_ref} ({pipeline_cls.__name__})")
    return pipeline_cls.from_pretrained(input_ref, torch_dtype=dtype)


def _quanto_qtype(method):
    from optimum.quanto import qint4, qint8, qfloat8
    return {
        "quanto_int4": qint4,
        "quanto_int8": qint8,
        "quanto_float8": qfloat8,
    }[method]


def _quantize_quanto(pipeline, components, method, output_dir, dtype):
    from optimum.quanto import freeze, quantize
    from optimum.quanto.quantize import quantization_map

    qtype = _quanto_qtype(method)
    pipeline.save_pretrained(output_dir, safe_serialization=True)

    for comp in components:
        module = getattr(pipeline, comp, None)
        if module is None:
            log(f"  skip: pipeline has no '{comp}'")
            continue
        log(f"  quantizing '{comp}' with quanto {method}")
        quantize(module, weights=qtype)
        freeze(module)

        comp_dir = Path(output_dir) / comp
        comp_dir.mkdir(parents=True, exist_ok=True)
        try:
            module.save_pretrained(comp_dir, safe_serialization=True)
        except AttributeError:
            from safetensors.torch import save_file
            save_file(module.state_dict(),
                      str(comp_dir / "model.safetensors"))
        qmap = quantization_map(module)
        with (comp_dir / "quanto_qmap.json").open("w", encoding="utf-8") as f:
            json.dump(qmap, f, indent=2)


def _quantize_torchao(pipeline, components, method, output_dir, dtype):
    from torchao.quantization import quantize_

    if method == "torchao_fp8":
        from torchao.quantization import Float8WeightOnlyConfig
        cfg = Float8WeightOnlyConfig()
    elif method == "torchao_int8":
        from torchao.quantization import Int8WeightOnlyConfig
        cfg = Int8WeightOnlyConfig()
    else:
        raise ValueError(f"unsupported torchao method: {method}")

    for comp in components:
        module = getattr(pipeline, comp, None)
        if module is None:
            log(f"  skip: pipeline has no '{comp}'")
            continue
        log(f"  torchao quantize '{comp}' with {method}")
        quantize_(module, cfg)

    pipeline.save_pretrained(output_dir, safe_serialization=True)


def _quantize_bnb_nf4(pipeline, components, method, output_dir, dtype):
    from diffusers import BitsAndBytesConfig

    bnb_cfg = BitsAndBytesConfig(
        load_in_4bit=True,
        bnb_4bit_quant_type="nf4",
        bnb_4bit_compute_dtype=dtype,
    )
    pipeline_cls = type(pipeline)
    src_path = getattr(pipeline, "_name_or_path", None) or ""
    bnb_pipe = pipeline_cls.from_pretrained(
        src_path, torch_dtype=dtype, quantization_config=bnb_cfg)
    bnb_pipe.save_pretrained(output_dir, safe_serialization=True)
    with (Path(output_dir) / "bnb_quant_config.json").open("w", encoding="utf-8") as f:
        json.dump({"load_in_4bit": True,
                   "bnb_4bit_quant_type": "nf4",
                   "bnb_4bit_compute_dtype": str(dtype)},
                  f, indent=2)


def main(argv=None):
    args = _parse_args(argv)
    device = _resolve_device(args.device)
    dtype = _resolve_dtype(args.dtype)

    log(f"input       = {args.input}")
    log(f"output      = {args.output}")
    log(f"method      = {args.method}")
    log(f"device      = {device}")
    log(f"dtype       = {args.dtype}")

    components = args.components or _default_components(args.input)
    log(f"components  = {components}")

    pipeline_cls = _detect_pipeline_class(args.input, args.pipeline_class)
    pipeline = _load_pipeline(args.input, pipeline_cls, dtype)

    output_dir = Path(args.output)
    if output_dir.exists() and any(output_dir.iterdir()):
        log(f"WARNING: {output_dir} is not empty -- contents may be overwritten")
    output_dir.mkdir(parents=True, exist_ok=True)

    if args.method.startswith("quanto_"):
        _quantize_quanto(pipeline, components, args.method, output_dir, dtype)
    elif args.method.startswith("torchao_"):
        _quantize_torchao(pipeline, components, args.method, output_dir, dtype)
    elif args.method == "bnb_nf4":
        _quantize_bnb_nf4(pipeline, components, args.method, output_dir, dtype)
    else:
        raise ValueError(f"unhandled method: {args.method}")

    log(f"Done. Prequantized model written to: {output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
