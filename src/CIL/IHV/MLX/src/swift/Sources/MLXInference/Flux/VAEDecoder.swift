// VAEDecoder.swift - VAE Decoder for Flux.2
// Copyright 2025 Vincent Gourbin

import Foundation
import MLX
import MLXNN

/// VAE Decoder for Flux.2
///
/// Decodes latent representations back to RGB images.
/// Supports both standard and small-decoder variants.
/// Architecture:
/// - Initial conv: latent_channels -> last_decoder_channel
/// - Mid block with attention
/// - Up blocks with ResNet blocks and upsampling
/// - Final conv: first_decoder_channel -> 3
public class VAEDecoder: Module, @unchecked Sendable {
    let config: VAEConfig

    let convIn: Conv2d
    let midBlock: (resnet1: ResnetBlock2D, attention: AttentionBlock, resnet2: ResnetBlock2D)
    let upBlocks: [(blocks: [ResnetBlock2D], upsample: Upsample2D?)]
    let convNormOut: GroupNorm
    let convOut: Conv2d

    public init(config: VAEConfig = .flux2Dev) {
        self.config = config

        // Use decoder-specific channels if available (small-decoder variant)
        let decoderChannels = config.effectiveDecoderChannels  // e.g. [96, 192, 384, 384]
        let reversedChannels = decoderChannels.reversed()

        // Initial convolution
        self.convIn = Conv2d(
            inputChannels: config.latentChannels,
            outputChannels: decoderChannels.last!,
            kernelSize: 3,
            padding: 1
        )

        // Mid block
        let midChannels = decoderChannels.last!
        self.midBlock = (
            resnet1: ResnetBlock2D(inChannels: midChannels, numGroups: config.normNumGroups),
            attention: AttentionBlock(channels: midChannels, numGroups: config.normNumGroups),
            resnet2: ResnetBlock2D(inChannels: midChannels, numGroups: config.normNumGroups)
        )

        // Up blocks (reversed order)
        var blocks: [(blocks: [ResnetBlock2D], upsample: Upsample2D?)] = []
        var prevChannels = midChannels

        for (i, outChannels) in Array(reversedChannels).enumerated() {
            var resBlocks: [ResnetBlock2D] = []

            // Diffusers adds +1 to layers_per_block for ALL up blocks in the decoder
            let numLayers = config.layersPerBlock + 1

            for j in 0..<numLayers {
                let inCh = (j == 0) ? prevChannels : outChannels
                resBlocks.append(ResnetBlock2D(
                    inChannels: inCh,
                    outChannels: outChannels,
                    numGroups: config.normNumGroups
                ))
            }
            prevChannels = outChannels

            // Upsample except for last block
            let upsample: Upsample2D?
            if i < Array(reversedChannels).count - 1 {
                upsample = Upsample2D(channels: outChannels)
            } else {
                upsample = nil
            }

            blocks.append((blocks: resBlocks, upsample: upsample))
        }
        self.upBlocks = blocks

        // Output
        self.convNormOut = GroupNorm(numGroups: config.normNumGroups, numChannels: decoderChannels[0])
        self.convOut = Conv2d(
            inputChannels: decoderChannels[0],
            outputChannels: config.outChannels,
            kernelSize: 3,
            padding: 1
        )
    }

    public func callAsFunction(_ z: MLXArray) -> MLXArray {
        // z shape: [B, latent_channels, H/8, W/8] (NCHW from transformer)
        // Convert to NHWC for MLX Conv2d
        var hidden = z.transposed(0, 2, 3, 1)  // [B, H/8, W/8, latent_channels]

        // Initial conv
        hidden = convIn(hidden)

        // Mid block
        hidden = midBlock.resnet1(hidden)
        hidden = midBlock.attention(hidden)
        hidden = midBlock.resnet2(hidden)

        // Up blocks
        for (resBlocks, upsample) in upBlocks {
            for resBlock in resBlocks {
                hidden = resBlock(hidden)
            }
            if let us = upsample {
                hidden = us(hidden)
            }
        }

        // Output
        hidden = convNormOut(hidden)
        hidden = silu(hidden)
        hidden = convOut(hidden)

        // Convert back to NCHW for output: [B, H, W, 3] -> [B, 3, H, W]
        return hidden.transposed(0, 3, 1, 2)
    }
}
