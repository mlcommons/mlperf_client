// ResnetBlock.swift - ResNet blocks for VAE
// Copyright 2025 Vincent Gourbin

import Foundation
import MLX
import MLXNN

/// Group Normalization for VAE (NHWC format for MLX)
public class GroupNorm: Module, @unchecked Sendable {
    let numGroups: Int
    let numChannels: Int
    let eps: Float
    let weight: MLXArray
    let bias: MLXArray

    public init(numGroups: Int, numChannels: Int, eps: Float = 1e-6) {
        self.numGroups = numGroups
        self.numChannels = numChannels
        self.eps = eps
        self.weight = MLXArray.ones([numChannels])
        self.bias = MLXArray.zeros([numChannels])
    }

    public func callAsFunction(_ x: MLXArray) -> MLXArray {
        // x shape: [B, H, W, C] (NHWC format for MLX)
        let shape = x.shape
        let B = shape[0]
        let H = shape[1]
        let W = shape[2]
        let C = shape[3]

        // Mixed precision: compute statistics in Float32 for numerical stability
        let inputDtype = x.dtype
        let xFloat = x.asType(.float32)

        // Reshape to [B, H, W, G, C/G]
        let reshaped = xFloat.reshaped([B, H, W, numGroups, C / numGroups])

        // Compute mean and variance per group (over H, W, and C/G within each group)
        let mean = MLX.mean(reshaped, axes: [1, 2, 4], keepDims: true)
        let variance = MLX.mean(pow(reshaped - mean, 2), axes: [1, 2, 4], keepDims: true)

        // Normalize in Float32
        let normalized = (reshaped - mean) / sqrt(variance + eps)

        // Reshape back to [B, H, W, C] and convert to original dtype
        let out = normalized.reshaped([B, H, W, C]).asType(inputDtype)

        // Apply weight and bias [1, 1, 1, C] for NHWC
        let weightReshaped = weight.reshaped([1, 1, 1, C])
        let biasReshaped = bias.reshaped([1, 1, 1, C])

        return out * weightReshaped + biasReshaped
    }
}

/// Batch Normalization for VAE latent space (Flux.2 specific) - NHWC format for MLX
public class BatchNorm2d: Module, @unchecked Sendable {
    let numFeatures: Int
    let eps: Float
    let momentum: Float
    let trackRunningStats: Bool

    let weight: MLXArray
    let bias: MLXArray
    var runningMean: MLXArray
    var runningVar: MLXArray

    public init(
        numFeatures: Int,
        eps: Float = 1e-5,
        momentum: Float = 0.1,
        trackRunningStats: Bool = true
    ) {
        self.numFeatures = numFeatures
        self.eps = eps
        self.momentum = momentum
        self.trackRunningStats = trackRunningStats

        self.weight = MLXArray.ones([numFeatures])
        self.bias = MLXArray.zeros([numFeatures])
        self.runningMean = MLXArray.zeros([numFeatures])
        self.runningVar = MLXArray.ones([numFeatures])
    }

    public func callAsFunction(_ x: MLXArray, training: Bool = false) -> MLXArray {
        // x shape: [B, H, W, C] (NHWC format for MLX)
        let shape = x.shape
        let C = shape[3]

        // Mixed precision: compute statistics in Float32 for numerical stability
        let inputDtype = x.dtype
        let xFloat = x.asType(.float32)

        if training {
            // Compute batch statistics (over B, H, W) in Float32
            let mean = MLX.mean(xFloat, axes: [0, 1, 2])
            let variance = MLX.mean(pow(xFloat - mean.reshaped([1, 1, 1, C]), 2), axes: [0, 1, 2])

            // Update running stats (keep in Float32)
            if trackRunningStats {
                runningMean = (1 - momentum) * runningMean + momentum * mean
                runningVar = (1 - momentum) * runningVar + momentum * variance
            }

            // Normalize with batch stats in Float32, then convert back
            let normalized = (xFloat - mean.reshaped([1, 1, 1, C])) / sqrt(variance.reshaped([1, 1, 1, C]) + eps)
            let out = normalized.asType(inputDtype)
            return out * weight.reshaped([1, 1, 1, C]) + bias.reshaped([1, 1, 1, C])
        } else {
            // Use running stats - normalize in Float32
            let runningMeanFloat = runningMean.asType(.float32)
            let runningVarFloat = runningVar.asType(.float32)
            let normalized = (xFloat - runningMeanFloat.reshaped([1, 1, 1, C])) / sqrt(runningVarFloat.reshaped([1, 1, 1, C]) + eps)
            let out = normalized.asType(inputDtype)
            return out * weight.reshaped([1, 1, 1, C]) + bias.reshaped([1, 1, 1, C])
        }
    }
}

/// ResNet block for VAE encoder/decoder
public class ResnetBlock2D: Module, @unchecked Sendable {
    let inChannels: Int
    let outChannels: Int

    let norm1: GroupNorm
    let conv1: Conv2d
    let norm2: GroupNorm
    let conv2: Conv2d
    let convShortcut: Conv2d?

    public init(
        inChannels: Int,
        outChannels: Int? = nil,
        numGroups: Int = 32
    ) {
        self.inChannels = inChannels
        self.outChannels = outChannels ?? inChannels

        self.norm1 = GroupNorm(numGroups: numGroups, numChannels: inChannels)
        self.conv1 = Conv2d(
            inputChannels: inChannels,
            outputChannels: self.outChannels,
            kernelSize: 3,
            padding: 1
        )

        self.norm2 = GroupNorm(numGroups: numGroups, numChannels: self.outChannels)
        self.conv2 = Conv2d(
            inputChannels: self.outChannels,
            outputChannels: self.outChannels,
            kernelSize: 3,
            padding: 1
        )

        // Shortcut projection if channels change
        if inChannels != self.outChannels {
            self.convShortcut = Conv2d(
                inputChannels: inChannels,
                outputChannels: self.outChannels,
                kernelSize: 1
            )
        } else {
            self.convShortcut = nil
        }
    }

    public func callAsFunction(_ x: MLXArray) -> MLXArray {
        var hidden = norm1(x)
        hidden = silu(hidden)
        hidden = conv1(hidden)

        hidden = norm2(hidden)
        hidden = silu(hidden)
        hidden = conv2(hidden)

        // Shortcut
        let shortcut: MLXArray
        if let convShortcut = convShortcut {
            shortcut = convShortcut(x)
        } else {
            shortcut = x
        }

        return hidden + shortcut
    }
}

/// Downsample block for VAE encoder
public class Downsample2D: Module, @unchecked Sendable {
    let conv: Conv2d

    public init(channels: Int, useConv: Bool = true, padding: Int = 1) {
        if useConv {
            self.conv = Conv2d(
                inputChannels: channels,
                outputChannels: channels,
                kernelSize: 3,
                stride: 2,
                padding: .init(padding)
            )
        } else {
            // Average pooling fallback (not typical for VAE)
            self.conv = Conv2d(
                inputChannels: channels,
                outputChannels: channels,
                kernelSize: 3,
                stride: 2,
                padding: .init(padding)
            )
        }
    }

    public func callAsFunction(_ x: MLXArray) -> MLXArray {
        conv(x)
    }
}

/// Upsample block for VAE decoder (NHWC format for MLX)
public class Upsample2D: Module, @unchecked Sendable {
    let conv: Conv2d

    public init(channels: Int, useConv: Bool = true) {
        self.conv = Conv2d(
            inputChannels: channels,
            outputChannels: channels,
            kernelSize: 3,
            padding: 1
        )
    }

    public func callAsFunction(_ x: MLXArray) -> MLXArray {
        // x shape: [B, H, W, C] (NHWC format for MLX)
        let shape = x.shape
        let B = shape[0]
        let H = shape[1]
        let W = shape[2]
        let C = shape[3]

        // Upsample by 2x using nearest neighbor interpolation
        // The correct approach is to expand dimensions and then reshape:
        // [B, H, W, C] -> [B, H, 1, W, 1, C] -> broadcast to [B, H, 2, W, 2, C] -> [B, H*2, W*2, C]

        // Reshape to add extra dimensions for broadcasting
        var upsampled = x.reshaped([B, H, 1, W, 1, C])

        // Tile/broadcast to duplicate each element along H and W
        // Use concatenation to duplicate along each new axis
        upsampled = concatenated([upsampled, upsampled], axis: 2)  // [B, H, 2, W, 1, C]
        upsampled = concatenated([upsampled, upsampled], axis: 4)  // [B, H, 2, W, 2, C]

        // Reshape to merge the duplicated dimensions
        upsampled = upsampled.reshaped([B, H * 2, W * 2, C])

        return conv(upsampled)
    }
}

/// Self-attention block for VAE (mid-block) - NHWC format for MLX
/// Uses Linear layers (as in HuggingFace checkpoint) instead of Conv2d
public class AttentionBlock: Module, @unchecked Sendable {
    let channels: Int
    let numHeads: Int
    let headDim: Int

    let groupNorm: GroupNorm
    let toQ: Linear
    let toK: Linear
    let toV: Linear
    let toOut: Linear

    public init(channels: Int, numHeads: Int = 1, numGroups: Int = 32) {
        self.channels = channels
        self.numHeads = numHeads
        self.headDim = channels / numHeads

        self.groupNorm = GroupNorm(numGroups: numGroups, numChannels: channels)
        self.toQ = Linear(channels, channels)
        self.toK = Linear(channels, channels)
        self.toV = Linear(channels, channels)
        self.toOut = Linear(channels, channels)
    }

    public func callAsFunction(_ x: MLXArray) -> MLXArray {
        // x shape: [B, H, W, C] (NHWC format for MLX)
        let residual = x
        let shape = x.shape
        let B = shape[0]
        let H = shape[1]
        let W = shape[2]
        let C = shape[3]

        // Normalize
        var hidden = groupNorm(x)

        // Reshape to [B, H*W, C] for Linear projection
        hidden = hidden.reshaped([B, H * W, C])

        // Project to Q, K, V using Linear layers
        let q = toQ(hidden)  // [B, HW, C]
        let k = toK(hidden).transposed(0, 2, 1)  // [B, C, HW]
        let v = toV(hidden)  // [B, HW, C]

        // Attention
        let scale = Float(1.0 / sqrt(Float(C)))
        let attnWeights = softmax(matmul(q, k) * scale, axis: -1)
        let attnOut = matmul(attnWeights, v)  // [B, HW, C]

        // Project output
        hidden = toOut(attnOut)

        // Reshape back to [B, H, W, C]
        hidden = hidden.reshaped([B, H, W, C])

        return hidden + residual
    }
}
