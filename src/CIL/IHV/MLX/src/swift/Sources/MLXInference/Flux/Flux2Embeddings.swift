// Flux2Embeddings.swift - Timestep and guidance embeddings
// Copyright 2025 Vincent Gourbin

import Foundation
import MLX
import MLXNN

/// Sinusoidal timestep embedding projection
public class Timesteps: Module, @unchecked Sendable {
    let numChannels: Int
    let flipSinToCos: Bool
    let downscaleFreqShift: Float
    let scale: Float

    public init(
        numChannels: Int,
        flipSinToCos: Bool = true,
        downscaleFreqShift: Float = 0.0,
        scale: Float = 1.0
    ) {
        self.numChannels = numChannels
        self.flipSinToCos = flipSinToCos
        self.downscaleFreqShift = downscaleFreqShift
        self.scale = scale
    }

    public func callAsFunction(_ timesteps: MLXArray) -> MLXArray {
        let halfDim = numChannels / 2
        let exponent = -log(Float(10000)) * MLXArray(0..<halfDim).asType(.float32)
        let exponentScaled = exponent / (Float(halfDim) - downscaleFreqShift)

        let emb = exp(exponentScaled)
        let timestepsExpanded = timesteps.expandedDimensions(axis: -1).asType(.float32)
        let embExpanded = emb.expandedDimensions(axis: 0)

        let sinEmb = sin(timestepsExpanded * embExpanded * scale)
        let cosEmb = cos(timestepsExpanded * embExpanded * scale)

        if flipSinToCos {
            return concatenated([cosEmb, sinEmb], axis: -1)
        } else {
            return concatenated([sinEmb, cosEmb], axis: -1)
        }
    }
}

/// MLP for embedding timesteps
public class TimestepEmbedding: Module, @unchecked Sendable {
    @ModuleInfo var linear1: Linear
    @ModuleInfo var linear2: Linear
    let activation: @Sendable (MLXArray) -> MLXArray

    public init(
        inChannels: Int,
        timeEmbedDim: Int,
        activationFn: String = "silu"
    ) {
        // No bias to match checkpoint
        self.linear1 = Linear(inChannels, timeEmbedDim, bias: false)
        self.linear2 = Linear(timeEmbedDim, timeEmbedDim, bias: false)

        switch activationFn {
        case "silu", "swish":
            self.activation = { silu($0) }
        case "gelu":
            self.activation = { gelu($0) }
        case "relu":
            self.activation = { relu($0) }
        default:
            self.activation = { silu($0) }
        }
    }

    public func callAsFunction(_ sample: MLXArray) -> MLXArray {
        var x = linear1(sample)
        x = activation(x)
        x = linear2(x)
        return x
    }
}

/// Combined timestep and guidance embeddings for Flux.2
///
/// Generates embeddings from timestep and guidance scale values.
/// Output dimension is the transformer's inner dimension (6144 for Flux.2).
public class Flux2TimestepGuidanceEmbeddings: Module, @unchecked Sendable {
    let timeProj: Timesteps
    let timestepEmbedder: TimestepEmbedding
    let guidanceEmbedder: TimestepEmbedding?
    let useGuidanceEmbeds: Bool

    /// Initialize timestep and guidance embeddings
    /// - Parameters:
    ///   - embeddingDim: Dimension of sinusoidal projection (256)
    ///   - timeEmbedDim: Output dimension for timestep MLP (6144)
    ///   - useGuidanceEmbeds: Whether to include guidance embedding
    public init(
        embeddingDim: Int = 256,
        timeEmbedDim: Int = 6144,
        useGuidanceEmbeds: Bool = true
    ) {
        self.timeProj = Timesteps(numChannels: embeddingDim)
        self.timestepEmbedder = TimestepEmbedding(
            inChannels: embeddingDim,
            timeEmbedDim: timeEmbedDim
        )

        self.useGuidanceEmbeds = useGuidanceEmbeds
        if useGuidanceEmbeds {
            self.guidanceEmbedder = TimestepEmbedding(
                inChannels: embeddingDim,
                timeEmbedDim: timeEmbedDim
            )
        } else {
            self.guidanceEmbedder = nil
        }
    }

    /// Generate timestep and optional guidance embeddings
    /// - Parameters:
    ///   - timestep: Current diffusion timestep [B]
    ///   - guidance: Guidance scale [B] (optional)
    /// - Returns: Combined embedding [B, timeEmbedDim]
    public func callAsFunction(
        timestep: MLXArray,
        guidance: MLXArray? = nil
    ) -> MLXArray {
        // Project timestep to sinusoidal embedding
        let tEmb = timeProj(timestep)
        // Pass through MLP
        var temb = timestepEmbedder(tEmb)

        // Add guidance embedding if enabled
        if useGuidanceEmbeds, let guidanceEmbedder = guidanceEmbedder, let g = guidance {
            let gEmb = timeProj(g)
            let gemb = guidanceEmbedder(gEmb)
            temb = temb + gemb
        }

        return temb
    }
}

/// Pooled text embedding projection for conditioning
public class PooledTextProjection: Module, @unchecked Sendable {
    @ModuleInfo var linear: Linear

    /// Initialize pooled text projection
    /// - Parameters:
    ///   - inputDim: Input dimension from text encoder
    ///   - outputDim: Output dimension (time embed dim)
    public init(inputDim: Int = 768, outputDim: Int = 6144) {
        self._linear = ModuleInfo(wrappedValue: Linear(inputDim, outputDim))
    }

    public func callAsFunction(_ pooledEmbedding: MLXArray) -> MLXArray {
        linear(pooledEmbedding)
    }
}
