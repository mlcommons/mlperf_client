"""Vendor-agnostic PyInstaller contribution: diffusers stack + our scripts."""
from __future__ import annotations


def contribute() -> dict:
    return {
        "hidden_packages": [
            "diffusers", "transformers", "accelerate", "safetensors",
            "tokenizers", "sentencepiece", "huggingface_hub",
            "optimum", "optimum.quanto",
            # Our top-level scripts loaded by the standalone runner.
            "diffusers_inference", "quantize_model",
        ],
        "metadata_packages": [
            "diffusers", "transformers", "accelerate", "safetensors",
            "tokenizers", "huggingface_hub",
            "numpy", "packaging", "regex", "requests",
            "filelock", "pyyaml", "tqdm", "importlib_metadata",
            "pillow", "torch", "torchvision",
            "optimum-quanto",
        ],
        # include_py_files=True keeps source alongside bytecode so
        # torch._dynamo's tracer can read it via inspect.getsource.
        "data_packages": ["diffusers", "transformers"],
        "excludes": [
            "tkinter", "matplotlib", "matplotlib.tests",
            "pytest", "IPython", "jupyter",
            "numpy.tests", "pandas",
        ],
    }
