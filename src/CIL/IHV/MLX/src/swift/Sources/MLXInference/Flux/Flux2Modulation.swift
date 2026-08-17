// Flux2Modulation.swift - AdaLN Modulation for Flux.2
// Copyright 2025 Vincent Gourbin

import Foundation
import MLX
import MLXNN

/// Modulation parameters for a single AdaLN operation
public struct ModulationParams {
    /// Shift parameter for centering
    public let shift: MLXArray
    /// Scale parameter for variance
    public let scale: MLXArray
    /// Gate parameter for residual connection
    public let gate: MLXArray

    public init(shift: MLXArray, scale: MLXArray, gate: MLXArray) {
        self.shift = shift
        self.scale = scale
        self.gate = gate
    }
}

/// Adaptive Layer Normalization Modulation
///
/// Generates (shift, scale, gate) parameters from timestep embeddings
/// for adaptive layer normalization in transformer blocks.
public class Flux2Modulation: Module, @unchecked Sendable {
    let dim: Int
    let numSets: Int
    @ModuleInfo var linear: Linear

    /// Initialize modulation layer
    /// - Parameters:
    ///   - dim: Model dimension (6144 for Flux.2)
    ///   - numSets: Number of modulation parameter sets (e.g., 6 for double-stream, 3 for single)
    public init(dim: Int, numSets: Int) {
        self.dim = dim
        self.numSets = numSets
        // Each set has 3 parameters: shift, scale, gate
        // So output dim = dim * 3 * numSets
        // IMPORTANT: bias=false to match diffusers checkpoint
        self.linear = Linear(dim, dim * 3 * numSets, bias: false)
    }

    /// Generate modulation parameters
    /// - Parameter embedding: Timestep/conditioning embedding [B, dim]
    /// - Returns: Array of ModulationParams, one for each set
    public func callAsFunction(_ embedding: MLXArray) -> [ModulationParams] {
        Flux2Debug.verbose("Modulation input shape: \(embedding.shape)")

        // Apply SiLU activation before projection (common in DiT-style models)
        let activated = silu(embedding)
        Flux2Debug.verbose("After silu: \(activated.shape)")

        // Project to all modulation parameters
        let allParams = linear(activated)  // [B, dim * 3 * numSets]
        Flux2Debug.verbose("After linear (dim=\(dim), numSets=\(numSets)): \(allParams.shape)")

        // Split into individual parameter sets
        var result: [ModulationParams] = []

        for i in 0..<numSets {
            let startIdx = i * dim * 3
            let shift = allParams[0..., startIdx..<(startIdx + dim)]
            let scale = allParams[0..., (startIdx + dim)..<(startIdx + dim * 2)]
            let gate = allParams[0..., (startIdx + dim * 2)..<(startIdx + dim * 3)]

            Flux2Debug.verbose("Set \(i) - shift: \(shift.shape), scale: \(scale.shape), gate: \(gate.shape)")

            result.append(ModulationParams(shift: shift, scale: scale, gate: gate))
        }

        return result
    }

    /// Generate modulation parameters as a flat array for efficient processing
    /// - Parameter embedding: Timestep/conditioning embedding [B, dim]
    /// - Returns: Tensor [B, numSets, 3, dim] containing all shift/scale/gate
    public func forwardFlat(_ embedding: MLXArray) -> MLXArray {
        let activated = silu(embedding)
        let allParams = linear(activated)  // [B, dim * 3 * numSets]

        // Reshape to [B, numSets, 3, dim]
        let batchSize = allParams.shape[0]
        return allParams.reshaped([batchSize, numSets, 3, dim])
    }
}

/// Apply AdaLN modulation to normalized input
/// - Parameters:
///   - x: Normalized input [B, S, dim]
///   - shift: Shift parameter [B, dim]
///   - scale: Scale parameter [B, dim]
/// - Returns: Modulated output [B, S, dim]
public func applyModulation(
    _ x: MLXArray,
    shift: MLXArray,
    scale: MLXArray
) -> MLXArray {
    Flux2Debug.verbose("applyModulation - x: \(x.shape), shift: \(shift.shape), scale: \(scale.shape)")

    // Expand shift/scale for sequence dimension
    // x: [B, S, dim], shift/scale: [B, dim]
    let shiftExpanded = shift.expandedDimensions(axis: 1)  // [B, 1, dim]
    let scaleExpanded = scale.expandedDimensions(axis: 1)  // [B, 1, dim]

    Flux2Debug.verbose("Expanded - shift: \(shiftExpanded.shape), scale: \(scaleExpanded.shape)")

    // Apply: x * (1 + scale) + shift
    return x * (1 + scaleExpanded) + shiftExpanded
}

/// Apply gating to residual connection
/// - Parameters:
///   - residual: Residual to be gated [B, S, dim]
///   - gate: Gate parameter [B, dim]
/// - Returns: Gated residual [B, S, dim]
public func applyGate(_ residual: MLXArray, gate: MLXArray) -> MLXArray {
    let gateExpanded = gate.expandedDimensions(axis: 1)  // [B, 1, dim]
    return residual * gateExpanded
}

/// Adaptive Layer Norm with continuous embedding (for final output)
public class AdaLayerNormContinuous: Module, @unchecked Sendable {
    let dim: Int
    let norm: LayerNorm
    @ModuleInfo var linear: Linear

    public init(dim: Int, eps: Float = 1e-6) {
        self.dim = dim
        self.norm = LayerNorm(dimensions: dim, eps: eps, affine: false)
        // Projects conditioning to shift and scale (no bias to match checkpoint)
        self._linear = ModuleInfo(wrappedValue: Linear(dim, dim * 2, bias: false))
    }

    /// Apply adaptive layer norm
    /// - Parameters:
    ///   - x: Input tensor [B, S, dim]
    ///   - conditioning: Conditioning embedding [B, dim]
    /// - Returns: Normalized and modulated output [B, S, dim]
    public func callAsFunction(_ x: MLXArray, conditioning: MLXArray) -> MLXArray {
        // Get scale and shift from conditioning
        // IMPORTANT: diffusers order is linear(silu(x)), NOT silu(linear(x))
        // And the split order is (scale, shift), NOT (shift, scale)
        let params = linear(silu(conditioning))  // [B, dim * 2]
        let scale = params[0..., 0..<dim]
        let shift = params[0..., dim...]

        // Normalize
        let normalized = norm(x)

        // Apply modulation
        return applyModulation(normalized, shift: shift, scale: scale)
    }
}

/// Modulation for double-stream blocks
/// Generates 6 sets of parameters: 2 for image attention, 2 for image FFN, 2 for text
public class DoubleStreamModulation: Module, @unchecked Sendable {
    let imgModulation: Flux2Modulation
    let txtModulation: Flux2Modulation

    public init(dim: Int) {
        // Image branch: attn (shift, scale, gate) + ffn (shift, scale, gate) = 2 sets * 3
        self.imgModulation = Flux2Modulation(dim: dim, numSets: 2)
        // Text branch: attn (shift, scale, gate) + ffn (shift, scale, gate) = 2 sets * 3
        self.txtModulation = Flux2Modulation(dim: dim, numSets: 2)
    }

    public func callAsFunction(_ embedding: MLXArray) -> (img: [ModulationParams], txt: [ModulationParams]) {
        let imgParams = imgModulation(embedding)
        let txtParams = txtModulation(embedding)
        return (img: imgParams, txt: txtParams)
    }
}

/// Modulation for single-stream blocks
/// Generates 2 sets: attention and FFN (fused in parallel attention)
public class SingleStreamModulation: Module, @unchecked Sendable {
    let modulation: Flux2Modulation

    public init(dim: Int) {
        // Single stream: combined attn+ffn modulation = 1 set * 3
        self.modulation = Flux2Modulation(dim: dim, numSets: 1)
    }

    public func callAsFunction(_ embedding: MLXArray) -> [ModulationParams] {
        modulation(embedding)
    }
}
