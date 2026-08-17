// Gradient checkpointing is a *training-time* memory optimization: it discards
// intermediate activations on the forward pass and recomputes them during
// backprop. For forward-only inference (our use), it is a no-op — the wrapped
// function computes identical results. We therefore replace the upstream
// Cmlx-based `checkpoint` with a passthrough, avoiding the low-level C interop.

import MLX

func checkpoint(_ f: @escaping ([MLXArray]) -> [MLXArray]) -> ([MLXArray]) -> [MLXArray] { f }

func checkpoint(_ f: @escaping (MLXArray) -> MLXArray) -> (MLXArray) -> MLXArray { f }
