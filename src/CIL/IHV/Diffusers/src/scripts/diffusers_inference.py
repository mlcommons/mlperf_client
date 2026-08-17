"""Diffusers EP entry point.

Façade dispatching into the modular path under runtimes/, vendors/,
models/, opts/, helpers, runtime, registry. Public surface
(`ImageInference.load/generate/unload`) is consumed by `python_bridge.cpp`
and `standalone_runner.py`.
"""
import os as _os
import signal as _signal


# Multiprocessing shims must run before any torch import. torch's
# resource_tracker re-execs sys.executable (= the host binary in our
# embedded interpreter) with python flags, which SIGPIPE-kills us on
# macOS. Ignore SIGPIPE + no-op the tracker spawn.
if hasattr(_signal, "SIGPIPE"):
    # signal.signal() only works on the main thread; off it (embedded
    # interpreter) python_bridge.cpp installs the SIG_IGN handler instead.
    try:
        _signal.signal(_signal.SIGPIPE, _signal.SIG_IGN)
    except ValueError:
        pass


def _silence_multiprocessing_for_embedded():
    """Make multiprocessing.resource_tracker inert in the embedded interpreter.

    sys.executable is the C++ host binary, not Python, so the tracker's
    Popen([sys.executable, ...]) re-execs the host and the run hangs. The
    process is short-lived, so leaked-semaphore cleanup is moot.
    """
    try:
        import multiprocessing.resource_tracker as rt

        tracker = rt._resource_tracker
        if getattr(tracker, "_mlperf_inert", False):
            return

        def _inert(*args, **kwargs):
            return None

        for _name in ("ensure_running", "_send", "register", "unregister"):
            try:
                setattr(tracker, _name, _inert)
            except Exception:
                pass
        for _name in ("ensure_running", "register", "unregister"):
            try:
                setattr(rt, _name, _inert)
            except Exception:
                pass
        tracker._mlperf_inert = True
    except Exception:
        pass


# Run at import — before torch / diffusers pull in multiprocessing.
_silence_multiprocessing_for_embedded()

import numpy as np

# backend ("CUDA"/"MPS") -> device_vendor ("NVIDIA"/"APPLE"). Today
# backend uniquely picks the vendor; if a vendor ever owns more than
# one backend (ROCm, DirectML, ...), the EP config will need a
# separate `device_vendor` field and this map can be retired.
_BACKEND_TO_VENDOR = {
    "CUDA": "NVIDIA",
    "MPS": "APPLE",
    "RYZENAI": "AMD",
}


class ImageInference:
    """Façade over the modular Runtime path. Public surface consumed by
    `python_bridge.cpp` and `standalone_runner.py`."""

    # Set on first failure to a private mkdtemp() dir; reused after.
    _failure_log_dir = None

    def __init__(self):
        self._runtime = None
        self._vendor = None

    def load(self, *args, **kwargs):
        try:
            return self._load_impl(*args, **kwargs)
        except BaseException:
            self._dump_failure_log("load failed", args, kwargs)
            raise

    def _load_impl(
        self,
        model_path: str,
        backend: str,
        device_id: int = 0,
        model_base_name: str = "",
        gpu_mode: str = "",
        quantization: str = "",
        component_files: dict = None,
        pipeline_class: str = "",
        hf_base_id: str = "",
        optimizations_json: str = "",
        download_cache: str = None,
    ):
        _silence_multiprocessing_for_embedded()

        # The C++ harness passes the config's Model.FilePath verbatim, which
        # by convention points at <model_dir>/model_index.json. Diffusers'
        # from_pretrained() wants the containing directory, not the file.
        # standalone_runner.py strips this upstream; we mirror it here so
        # the modular path works regardless of caller.
        if model_path and not _os.path.isdir(model_path):
            base = _os.path.basename(model_path).lower()
            if base in ("model_index.json", "config.json"):
                model_path = _os.path.dirname(model_path)

        import runtimes  # noqa: F401  fires @register_runtime decorators
        import opts  # noqa: F401  fires @register_opt decorators
        from vendors import get_vendor
        from registry import resolve_runtime
        from helpers import log_component_devices

        backend_str = (backend or "").strip().upper()
        vendor_name = _BACKEND_TO_VENDOR.get(backend_str)
        if vendor_name is None:
            raise ValueError(
                f"unknown backend {backend!r}. Supported: "
                f"{sorted(_BACKEND_TO_VENDOR)}"
            )
        vendor = get_vendor(device_vendor=vendor_name)

        deps_dir = ((component_files or {}).get("__deps_dir__") or "").strip() or None
        vendor.validate(deps_dir=deps_dir)
        vendor.configure_globals()
        self._vendor = vendor

        requested = None
        if vendor.backend == "cuda":
            requested = f"cuda:{int(device_id or 0)}"
        elif vendor.backend == "mps":
            requested = "mps"
        elif vendor.backend == "ryzenai":
            requested = "npu"
        else:
            requested = "cpu"
        device = vendor.resolve_device(requested)
        dtype = vendor.normalize_dtype(vendor.default_dtype())

        cf = dict(component_files or {})
        if pipeline_class:
            cf["__pipeline_class__"] = pipeline_class
        if hf_base_id:
            cf["__hf_base_id__"] = hf_base_id

        cls = resolve_runtime(backend=vendor.backend)
        self._runtime = cls(
            model_path=model_path,
            model_base_name=model_base_name,
            vendor=vendor,
            device=device,
            dtype=dtype,
            gpu_mode=gpu_mode,
            quantization=quantization,
            component_files=cf,
            download_cache=download_cache,
        )

        self._runtime.load()

        opt_specs = self._parse_optimizations(optimizations_json)
        self._runtime.apply_optimizations(opt_specs)
        log_component_devices(self._runtime.pipe)

    @staticmethod
    def _parse_optimizations(json_str: str):
        """Parse C++ JSON string into OptimizationSpec list, or None for defaults."""
        if not json_str:
            return None
        import json
        from runtime import OptimizationSpec
        try:
            raw = json.loads(json_str)
        except (json.JSONDecodeError, TypeError):
            return None
        if not isinstance(raw, list):
            return None
        specs = []
        for entry in raw:
            if isinstance(entry, str):
                specs.append(OptimizationSpec(type=entry))
            elif isinstance(entry, dict) and "type" in entry:
                params = {k: v for k, v in entry.items() if k != "type"}
                specs.append(OptimizationSpec(
                    type=entry["type"], params=params))
        return specs or None

    def generate(
        self,
        prompt: str,
        negative_prompt: str = "",
        width: int = 1024,
        height: int = 1024,
        num_inference_steps: int = 28,
        guidance_scale: float = 3.5,
        seed: int = -1,
        num_images_per_prompt: int = 1,
        seeds: "list[int] | None" = None,
        step_callback=None,
        submodule_callback=None,
    ) -> "np.ndarray":
        from runtime import RunSpec

        if self._runtime is None or self._vendor is None:
            raise RuntimeError(
                "ImageInference.generate called before load")

        seed_int = int(seed)
        if seed_int >= 0:
            gpu_mode = (self._runtime.gpu_mode or "").strip().lower()
            prefer_cpu = gpu_mode in (
                "cpu_offload", "sequential_offload")
            generator = self._vendor.make_generator(
                seed_int, prefer_cpu=prefer_cpu)
        else:
            generator = None
        run = RunSpec(
            prompt=prompt,
            negative_prompt=negative_prompt or None,
            width=int(width),
            height=int(height),
            steps=int(num_inference_steps),
            guidance_scale=float(guidance_scale),
            seed=int(seed),
            num_images_per_prompt=int(num_images_per_prompt),
            seeds=tuple(int(s) for s in (seeds or ())),
        )

        if submodule_callback is not None:
            from timing.timer import flush_pending
            module_timer = self._vendor.make_module_timer()
            with self._runtime.instrument(module_timer) as captured:
                out = self._runtime.generate(
                    run, generator=generator,
                    step_callback=step_callback,
                    submodule_callback=submodule_callback)
            flush_pending(module_timer)
            self._emit_submodule_timings(captured, submodule_callback)
        else:
            out = self._runtime.generate(
                run, generator=generator,
                step_callback=step_callback)

        images = out.get("images") or []
        if not images:
            raise RuntimeError(f"runtime.generate returned no images: {type(out)}")
        # Stack all generated images -> shape [N, H, W, 3]. N=1 stays
        # byte-compatible with the previous single-image [H, W, 3] callers.
        return np.stack([np.array(im) for im in images])

    @staticmethod
    def _emit_submodule_timings(captured, callback):
        """Fire the C++ submodule callback for each recorded timing."""
        for name, durations in captured.items():
            for ms in durations:
                callback(name, ms)

    def unload(self):
        if self._runtime is not None:
            self._runtime.unload()
            self._runtime = None
        if self._vendor is not None:
            self._vendor.release_memory()
            self._vendor = None

    @staticmethod
    def _dump_failure_log(label, args, kwargs):
        import traceback as _tb
        import tempfile
        log_dir = ImageInference._failure_log_dir
        if log_dir is None or not _os.path.isdir(log_dir):
            # mkdtemp() creates a 0o700 dir with an unpredictable name, so
            # the log is not exposed in a world-writable temp directory.
            log_dir = tempfile.mkdtemp(prefix="diffusers_inference_")
            ImageInference._failure_log_dir = log_dir
        log_path = _os.path.join(log_dir, "diffusers_inference_load.log")
        try:
            with open(log_path, "a") as f:
                f.write(f"=== {label} ===\n")
                f.write(f"args={args!r}\nkwargs={kwargs!r}\n")
                _tb.print_exc(file=f)
                f.write("\n")
        except Exception:
            pass
