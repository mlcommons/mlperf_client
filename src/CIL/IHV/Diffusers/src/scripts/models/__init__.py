"""Model loaders. Each module exposes a `load(runtime) -> pipe` Loader callable."""
# flux2_klein_amd not eager-imported: it pulls onnxruntime (AMD-only env);
# flux2_klein.load() imports it lazily for the ryzenai path.
from models import flux2_klein, generic

__all__ = ["flux2_klein", "generic"]
