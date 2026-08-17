// VAEEncoder.swift - VAE Encoder for Flux.2
// Copyright 2025 Vincent Gourbin

import Foundation
import MLX
import MLXNN

/// VAE Encoder for Flux.2
///
/// Encodes RGB images to latent space.
/// Architecture:
/// - Initial conv: 3 -> 128
/// - Down blocks with ResNet blocks and downsampling
/// - Mid block with attention
/// - Final conv: 512 -> latent_channels * 2 (for mean and variance)
public class VAEEncoder: Module, @unchecked Sendable {
    let config: VAEConfig

    let convIn: Conv2d
    let downBlocks: [(blocks: [ResnetBlock2D], downsample: Downsample2D?)]
    let midBlock: (resnet1: ResnetBlock2D, attention: AttentionBlock, resnet2: ResnetBlock2D)
    let convNormOut: GroupNorm
    let convOut: Conv2d

    public init(config: VAEConfig = .flux2Dev) {
        self.config = config

        let blockOutChannels = config.blockOutChannels  // [128, 256, 512, 512]

        // Initial convolution
        self.convIn = Conv2d(
            inputChannels: config.inChannels,
            outputChannels: blockOutChannels[0],
            kernelSize: 3,
            padding: 1
        )

        // Down blocks
        var blocks: [(blocks: [ResnetBlock2D], downsample: Downsample2D?)] = []
        var prevChannels = blockOutChannels[0]

        for (i, outChannels) in blockOutChannels.enumerated() {
            var resBlocks: [ResnetBlock2D] = []

            for _ in 0..<config.layersPerBlock {
                resBlocks.append(ResnetBlock2D(
                    inChannels: prevChannels,
                    outChannels: outChannels,
                    numGroups: config.normNumGroups
                ))
                prevChannels = outChannels
            }

            // Downsample except for last block
            let downsample: Downsample2D?
            if i < blockOutChannels.count - 1 {
                downsample = Downsample2D(channels: outChannels)
            } else {
                downsample = nil
            }

            blocks.append((blocks: resBlocks, downsample: downsample))
        }
        self.downBlocks = blocks

        // Mid block
        let midChannels = blockOutChannels.last!
        self.midBlock = (
            resnet1: ResnetBlock2D(inChannels: midChannels, numGroups: config.normNumGroups),
            attention: AttentionBlock(channels: midChannels, numGroups: config.normNumGroups),
            resnet2: ResnetBlock2D(inChannels: midChannels, numGroups: config.normNumGroups)
        )

        // Output
        self.convNormOut = GroupNorm(numGroups: config.normNumGroups, numChannels: midChannels)
        // Output channels: 2 * latent_channels for mean and log_variance
        self.convOut = Conv2d(
            inputChannels: midChannels,
            outputChannels: config.latentChannels * 2,
            kernelSize: 3,
            padding: 1
        )
    }

    public func callAsFunction(_ x: MLXArray) -> MLXArray {
        // x shape: [B, 3, H, W] (NCHW from input)
        // Convert to NHWC for MLX Conv2d
        var hidden = x.transposed(0, 2, 3, 1)  // [B, H, W, 3]

        // Initial conv
        hidden = convIn(hidden)

        // Down blocks
        for (resBlocks, downsample) in downBlocks {
            for resBlock in resBlocks {
                hidden = resBlock(hidden)
            }
            if let ds = downsample {
                hidden = ds(hidden)
            }
        }

        // Mid block
        hidden = midBlock.resnet1(hidden)
        hidden = midBlock.attention(hidden)
        hidden = midBlock.resnet2(hidden)

        // Output
        hidden = convNormOut(hidden)
        hidden = silu(hidden)
        hidden = convOut(hidden)

        // Convert back to NCHW: [B, H/8, W/8, latent_channels*2] -> [B, latent_channels*2, H/8, W/8]
        return hidden.transposed(0, 3, 1, 2)
    }

    /// Encode and sample from the latent distribution
    /// - Parameter x: Input image [B, 3, H, W] (NCHW format)
    /// - Returns: Sampled latent [B, latent_channels, H/8, W/8] (NCHW format)
    public func encode(_ x: MLXArray, samplePosterior: Bool = true) -> MLXArray {
        let h = self.callAsFunction(x)  // Output is NCHW

        // Split into mean and log_variance (NCHW format)
        let mean = h[0..., 0..<config.latentChannels, 0..., 0...]
        let logVar = h[0..., config.latentChannels..., 0..., 0...]

        if samplePosterior {
            // Reparameterization trick
            let std = exp(0.5 * logVar)
            let noise = MLXRandom.normal(mean.shape)
            return mean + std * noise
        } else {
            return mean
        }
    }
}
