// Flux2FusedKernels.swift - Custom Metal kernels for performance-critical operations
// Copyright 2025 Vincent Gourbin
//
// The RoPE kernel implementation is inspired by mzbac/flux2.swift
// (https://github.com/mzbac/flux2.swift), licensed under Apache 2.0.
// This implementation has been adapted for [B, H, S, D] tensor layout
// (original uses [B, S, H, D]) and integrated with our caching system.

import Foundation
import MLX
import MLXFast

/// Collection of fused Metal kernels for performance-critical transformer operations
///
/// These kernels fuse multiple operations into a single GPU dispatch, reducing
/// kernel launch overhead and improving memory locality.
public enum Flux2FusedKernels {
    /// Set to true to disable fused kernels (for debugging/comparison)
    /// Can also be set via environment variable FLUX2_DISABLE_FUSED_KERNELS=1
    nonisolated(unsafe) public static var disabled: Bool = {
        ProcessInfo.processInfo.environment["FLUX2_DISABLE_FUSED_KERNELS"] == "1"
    }()
    /// Number of attention heads processed per thread for RoPE kernel
    private static let ropeHeadsPerThread = 4

    /// Custom Metal kernel for applying rotary position embeddings
    ///
    /// This kernel fuses the rotation operation (x * cos + rotated_x * sin) into a single
    /// GPU kernel, processing multiple heads per thread for efficiency.
    ///
    /// Input layout: [B, H, S, D] where B=batch, H=heads, S=sequence, D=head_dim
    /// Cos/Sin layout: [S, D] or [B, S, D]
    private static let ropeKernel = MLXFast.metalKernel(
        name: "flux2_apply_rope_bhsd",
        inputNames: ["x", "cos", "sin", "batch", "heads", "seq", "head_dim", "rotary_batch_stride"],
        outputNames: ["out"],
        source: """
            uint dp = thread_position_in_grid.x;           // dimension pair index (0..half_dim-1)
            uint seq_idx = thread_position_in_grid.y;      // sequence position
            uint z = thread_position_in_grid.z;            // batch * head_groups

            uint half_dim = uint(head_dim / 2);
            if (dp >= half_dim || seq_idx >= uint(seq)) {
                return;
            }

            int head_groups = (heads + (N - 1)) / N;
            int batch_idx = int(z) / head_groups;
            int head_group = int(z) - batch_idx * head_groups;
            if (batch_idx >= batch) {
                return;
            }

            int head_base = head_group * N;

            // cos/sin layout: [S, D] if rotary_batch_stride=0, else [B, S, D]
            ulong row_base = (ulong(seq_idx) + ulong(batch_idx) * ulong(rotary_batch_stride)) * ulong(head_dim);
            device const float* cos_row = cos + row_base;
            device const float* sin_row = sin + row_base;

            uint d0 = dp * 2;
            float c = cos_row[d0];
            float s = sin_row[d0];

            // For [B, H, S, D] layout:
            // x[batch][head][seq][dim] = x[batch * H * S * D + head * S * D + seq * D + dim]
            ulong stride_S = ulong(head_dim);
            ulong stride_H = ulong(seq) * stride_S;
            ulong stride_B = ulong(heads) * stride_H;

            for (int i = 0; i < N; ++i) {
                int head_idx = head_base + i;
                if (head_idx >= heads) {
                    break;
                }

                ulong base = ulong(batch_idx) * stride_B + ulong(head_idx) * stride_H + ulong(seq_idx) * stride_S + ulong(d0);
                float x0 = float(x[base]);
                float x1 = float(x[base + 1]);

                // Apply rotation: [x0, x1] * [[c, -s], [s, c]]
                // Result: [x0*c - x1*s, x0*s + x1*c]
                // But diffusers uses: x * cos + rotate_half(x) * sin
                // where rotate_half gives [-x1, x0] for consecutive pairs
                // So: [x0, x1] -> [x0*c - x1*s, x1*c + x0*s]
                out[base] = T(x0 * c - x1 * s);
                out[base + 1] = T(x1 * c + x0 * s);
            }
            """
    )

    /// Apply rotary embeddings using the fused Metal kernel
    ///
    /// - Parameters:
    ///   - x: Input tensor [B, H, S, D] in bfloat16, float16, or float32
    ///   - cos: Cosine embeddings [S, D] or [B, S, D] in float32
    ///   - sin: Sine embeddings [S, D] or [B, S, D] in float32
    /// - Returns: Rotated tensor [B, H, S, D] in same dtype as input, or nil if kernel can't be used
    public static func applyRotaryEmb(
        _ x: MLXArray,
        cos: MLXArray,
        sin: MLXArray
    ) -> MLXArray? {
        // Allow disabling for debugging
        guard !disabled else { return nil }

        // Only use kernel on GPU
        guard Device.defaultDevice().deviceType == .gpu else {
            return nil
        }

        // Validate dtypes
        guard x.dtype == .bfloat16 || x.dtype == .float16 || x.dtype == .float32,
              cos.dtype == .float32,
              sin.dtype == .float32
        else {
            return nil
        }

        // Validate x shape: [B, H, S, D]
        guard x.ndim == 4 else {
            return nil
        }

        let batch = x.dim(0)
        let heads = x.dim(1)
        let seq = x.dim(2)
        let headDim = x.dim(3)

        // Validate cos/sin shape: [S, D] or [B, S, D]
        guard (cos.ndim == 2 || cos.ndim == 3), cos.ndim == sin.ndim else { return nil }

        let batchStride: Int32
        if cos.ndim == 2 {
            // [S, D] - shared across batch
            guard cos.dim(0) == seq, sin.dim(0) == seq else { return nil }
            guard cos.dim(1) == headDim, sin.dim(1) == headDim else { return nil }
            batchStride = 0
        } else if cos.ndim == 3 {
            // [B, S, D] - per-batch embeddings
            guard cos.dim(0) == batch, sin.dim(0) == batch else { return nil }
            guard cos.dim(1) == seq, sin.dim(1) == seq else { return nil }
            guard cos.dim(2) == headDim, sin.dim(2) == headDim else { return nil }
            batchStride = Int32(seq)
        } else {
            return nil
        }

        // Head dimension must be even for rotation
        guard headDim % 2 == 0 else {
            return nil
        }

        let halfDim = headDim / 2
        let headsPerThread = ropeHeadsPerThread
        let headGroups = (heads + headsPerThread - 1) / headsPerThread

        // Thread group sizing
        let tgX = max(min(32, halfDim), 1)
        let tgY = max(min(4, seq), 1)

        let out = ropeKernel(
            [
                x,
                cos,
                sin,
                Int32(batch),
                Int32(heads),
                Int32(seq),
                Int32(headDim),
                batchStride,
            ],
            template: [
                ("T", x.dtype),
                ("N", headsPerThread),
            ],
            grid: (halfDim, seq, batch * headGroups),
            threadGroup: (tgX, tgY, 1),
            outputShapes: [x.shape],
            outputDTypes: [x.dtype],
            stream: .gpu
        )
        return out[0]
    }

    /// Check if the fused RoPE kernel is available for the given configuration
    public static func isRopeKernelAvailable(
        xDtype: DType,
        xNdim: Int,
        cosNdim: Int,
        sinNdim: Int
    ) -> Bool {
        guard Device.defaultDevice().deviceType == .gpu else { return false }
        guard xDtype == .bfloat16 || xDtype == .float16 || xDtype == .float32 else { return false }
        guard xNdim == 4 else { return false }
        guard (cosNdim == 2 || cosNdim == 3), cosNdim == sinNdim else { return false }
        return true
    }
}
