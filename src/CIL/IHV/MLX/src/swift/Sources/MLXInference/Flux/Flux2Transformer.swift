// Flux2Transformer.swift - Complete Flux.2 Diffusion Transformer
// Copyright 2025 Vincent Gourbin

import Foundation
import MLX
import MLXNN

/// Flux.2 Diffusion Transformer (DiT) Model
///
/// Architecture:
/// - 8 double-stream transformer blocks (joint text-image attention)
/// - 48 single-stream transformer blocks (concatenated self-attention)
/// - ~32B parameters total
///
/// Flow:
/// 1. Project latents to inner dim: [B, H*W, 128] -> [B, H*W, 6144]
/// 2. Project text embeddings: [B, S, 15360] -> [B, S, 6144]
/// 3. Generate timestep/guidance embeddings
/// 4. Double-stream blocks: process text and image separately with joint attention
/// 5. Single-stream blocks: concatenate and process together
/// 6. Final norm and projection: [B, H*W, 6144] -> [B, H*W, 128]
public class Flux2Transformer2DModel: Module, @unchecked Sendable {
    let config: Flux2TransformerConfig

    /// Memory optimization settings for periodic graph evaluation
    public var memoryOptimization: MemoryOptimizationConfig

    /// Enable gradient checkpointing for training (reduces memory, increases compute ~2x)
    /// When enabled, each transformer block's forward pass is wrapped with checkpoint()
    /// so intermediate activations are recomputed during backward instead of stored.
    public var gradientCheckpointing: Bool = false

    // Input embeddings (var for LoRA injection)
    @ModuleInfo var xEmbedder: Linear           // Latent projection: 128 -> 6144
    @ModuleInfo var contextEmbedder: Linear     // Text projection: 15360 -> 6144

    // Timestep/guidance embeddings (var for LoRA injection)
    @ModuleInfo var timeGuidanceEmbed: Flux2TimestepGuidanceEmbeddings

    // Positional embeddings (RoPE)
    let posEmbed: Flux2RoPE

    // Modulation layers (var for LoRA injection)
    @ModuleInfo var doubleStreamModulationImg: Flux2Modulation
    @ModuleInfo var doubleStreamModulationTxt: Flux2Modulation
    @ModuleInfo var singleStreamModulation: Flux2Modulation

    // Transformer blocks
    let transformerBlocks: [Flux2TransformerBlock]
    let singleTransformerBlocks: [Flux2SingleTransformerBlock]

    // Output layers
    let normOut: AdaLayerNormContinuous
    @ModuleInfo var projOut: Linear             // Output projection: 6144 -> 128

    /// Initialize Flux.2 Transformer
    /// - Parameters:
    ///   - config: Model configuration
    ///   - memoryOptimization: Memory optimization settings (default: moderate)
    public init(
        config: Flux2TransformerConfig = .flux2Dev,
        memoryOptimization: MemoryOptimizationConfig = .moderate
    ) {
        self.config = config
        self.memoryOptimization = memoryOptimization

        let dim = config.innerDim  // 6144

        // Input projections (no bias to match checkpoint)
        self.xEmbedder = Linear(config.inChannels, dim, bias: false)
        self.contextEmbedder = Linear(config.jointAttentionDim, dim, bias: false)

        // Timestep embeddings
        self.timeGuidanceEmbed = Flux2TimestepGuidanceEmbeddings(
            embeddingDim: 256,
            timeEmbedDim: dim,
            useGuidanceEmbeds: config.guidanceEmbeds
        )

        // RoPE
        self.posEmbed = Flux2RoPE(
            axesDims: config.axesDimsRope,
            theta: config.ropeTheta
        )

        // Modulation layers (6 params each for double-stream, 3 for single)
        self.doubleStreamModulationImg = Flux2Modulation(dim: dim, numSets: 2)
        self.doubleStreamModulationTxt = Flux2Modulation(dim: dim, numSets: 2)
        self.singleStreamModulation = Flux2Modulation(dim: dim, numSets: 1)

        // Double-stream blocks (8)
        self.transformerBlocks = (0..<config.numLayers).map { _ in
            Flux2TransformerBlock(
                dim: dim,
                numHeads: config.numAttentionHeads,
                headDim: config.attentionHeadDim
            )
        }

        // Single-stream blocks (48)
        self.singleTransformerBlocks = (0..<config.numSingleLayers).map { _ in
            Flux2SingleTransformerBlock(
                dim: dim,
                numHeads: config.numAttentionHeads,
                headDim: config.attentionHeadDim
            )
        }

        // Output (no bias to match checkpoint)
        self.normOut = AdaLayerNormContinuous(dim: dim)
        self.projOut = Linear(dim, config.outChannels, bias: false)
    }

    /// Forward pass
    /// - Parameters:
    ///   - hiddenStates: Packed latent image [B, S_img, 128]
    ///   - encoderHiddenStates: Text embeddings from Mistral [B, S_txt, 15360]
    ///   - timestep: Diffusion timestep [B]
    ///   - guidance: Guidance scale [B] (optional)
    ///   - imgIds: Image position IDs [S_img, 4]
    ///   - txtIds: Text position IDs [S_txt, 4]
    /// - Returns: Predicted noise [B, S_img, 128]
    public func callAsFunction(
        hiddenStates: MLXArray,
        encoderHiddenStates: MLXArray,
        timestep: MLXArray,
        guidance: MLXArray? = nil,
        imgIds: MLXArray,
        txtIds: MLXArray
    ) -> MLXArray {
        Flux2Debug.verbose("=== Transformer Forward ===")
        Flux2Debug.verbose("hiddenStates: \(hiddenStates.shape)")
        Flux2Debug.verbose("encoderHiddenStates: \(encoderHiddenStates.shape)")
        Flux2Debug.verbose("timestep: \(timestep.shape)")

        // Project inputs
        var imgHS = xEmbedder(hiddenStates)
        var txtHS = contextEmbedder(encoderHiddenStates)
        Flux2Debug.verbose("After projection - imgHS: \(imgHS.shape), txtHS: \(txtHS.shape)")

        // Scale timestep and guidance by 1000 (diffusers pattern)
        // Pipeline passes timestep in [0, 1] range (sigma), but the sinusoidal
        // embedding in Flux2TimestepGuidanceEmbeddings expects values in [0, 1000] range.
        // Similarly, guidance (e.g., 4.0) is scaled to 4000.
        let scaledTimestep = timestep * 1000
        let scaledGuidance = guidance.map { $0 * 1000 }

        // Generate timestep + guidance embedding
        let temb = timeGuidanceEmbed(timestep: scaledTimestep, guidance: scaledGuidance)
        Flux2Debug.verbose("temb shape: \(temb.shape)")

        // Generate RoPE embeddings
        let combinedIds = concatenated([txtIds, imgIds], axis: 0)
        let ropeEmb = posEmbed(combinedIds)
        Flux2Debug.verbose("RoPE shapes - cos: \(ropeEmb.cos.shape), sin: \(ropeEmb.sin.shape)")

        // --- Double-Stream Blocks ---
        // OPTIMIZATION: Compute modulation parameters ONCE before the loop
        // (they only depend on temb which doesn't change between blocks)
        let imgMod = doubleStreamModulationImg(temb)
        let txtMod = doubleStreamModulationTxt(temb)
        Flux2Debug.verbose("imgMod count: \(imgMod.count), first shift: \(imgMod.first?.shift.shape ?? [])")

        // Pre-compute flattened modulation arrays once (avoids re-creating at each iteration)
        let imgModFlat = imgMod.flatMap { [$0.shift, $0.scale, $0.gate] }
        let txtModFlat = txtMod.flatMap { [$0.shift, $0.scale, $0.gate] }

        for (blockIdx, block) in transformerBlocks.enumerated() {
            Flux2Debug.verbose("Double-stream block \(blockIdx)")

            if gradientCheckpointing {
                // Gradient checkpointing: wrap block forward pass with checkpoint()
                // Pack all inputs (activations + trainable params) as explicit arguments
                // so checkpoint() can track gradients through them during backward pass.
                let paramTemplate = block.trainableParameters()
                let paramValues = paramTemplate.flattenedValues()
                let numActivations = 17  // imgHS, txtHS, temb, ropeCos, ropeSin, 6×imgMod, 6×txtMod

                var inputs: [MLXArray] = [imgHS, txtHS, temb, ropeEmb.cos, ropeEmb.sin]
                inputs.append(contentsOf: imgModFlat)
                inputs.append(contentsOf: txtModFlat)
                inputs.append(contentsOf: paramValues)

                let checkpointedForward = checkpoint { (arrays: [MLXArray]) -> [MLXArray] in
                    // Unpack activations
                    let hs = arrays[0], ehs = arrays[1], t = arrays[2]
                    let rope = (cos: arrays[3], sin: arrays[4])
                    let iMod = [ModulationParams(shift: arrays[5], scale: arrays[6], gate: arrays[7]),
                                ModulationParams(shift: arrays[8], scale: arrays[9], gate: arrays[10])]
                    let tMod = [ModulationParams(shift: arrays[11], scale: arrays[12], gate: arrays[13]),
                                ModulationParams(shift: arrays[14], scale: arrays[15], gate: arrays[16])]

                    // Unpack and restore trainable parameters in block
                    let blockParams = Array(arrays[numActivations...])
                    block.update(parameters: paramTemplate.replacingValues(with: blockParams))

                    let (newTxt, newImg) = block(
                        hiddenStates: hs,
                        encoderHiddenStates: ehs,
                        temb: t,
                        rotaryEmb: rope,
                        imgModParams: iMod,
                        txtModParams: tMod
                    )
                    return [newTxt, newImg]
                }

                let out = checkpointedForward(inputs)
                txtHS = out[0]
                imgHS = out[1]
            } else {
                let (newTxt, newImg) = block(
                    hiddenStates: imgHS,
                    encoderHiddenStates: txtHS,
                    temb: temb,
                    rotaryEmb: ropeEmb,
                    imgModParams: imgMod,
                    txtModParams: txtMod
                )

                imgHS = newImg
                txtHS = newTxt
            }
            Flux2Debug.verbose("After block \(blockIdx) - imgHS: \(imgHS.shape), txtHS: \(txtHS.shape)")

            // Memory optimization: periodic evaluation to prevent graph accumulation
            // Skip when gradient checkpointing is active (checkpoint boundaries handle segmentation)
            if !gradientCheckpointing &&
                memoryOptimization.evalFrequency > 0 &&
                (blockIdx + 1) % memoryOptimization.evalFrequency == 0 {
                eval(imgHS, txtHS)
                if memoryOptimization.clearCacheOnEval {
                    MLX.Memory.clearCache()
                }
                Flux2Debug.verbose("Eval at double-stream block \(blockIdx)")
            }
        }

        // Memory optimization: evaluate between phases
        if memoryOptimization.evalBetweenPhases {
            eval(imgHS, txtHS)
            if memoryOptimization.clearCacheOnEval {
                MLX.Memory.clearCache()
            }
            Flux2Debug.verbose("Eval between double/single stream phases")
        }

        // --- Single-Stream Blocks ---
        // Concatenate text and image streams BEFORE entering single-stream blocks
        // (diffusers pattern: single blocks work on concatenated hidden_states)
        let textSeqLen = txtHS.shape[1]
        var combinedHS = concatenated([txtHS, imgHS], axis: 1)  // [B, S_txt + S_img, dim]
        Flux2Debug.verbose("Single-stream input (combined): \(combinedHS.shape)")

        // OPTIMIZATION: Compute single-stream modulation ONCE before the loop
        let singleMod = singleStreamModulation(temb)
        let singleModFlat = singleMod.flatMap { [$0.shift, $0.scale, $0.gate] }

        for (blockIdx, block) in singleTransformerBlocks.enumerated() {
            if gradientCheckpointing {
                // Gradient checkpointing: same pattern as double-stream blocks
                let paramTemplate = block.trainableParameters()
                let paramValues = paramTemplate.flattenedValues()
                let numActivations = 7  // combinedHS, temb, ropeCos, ropeSin, mod.shift, mod.scale, mod.gate

                var inputs: [MLXArray] = [combinedHS, temb, ropeEmb.cos, ropeEmb.sin]
                inputs.append(contentsOf: singleModFlat)
                inputs.append(contentsOf: paramValues)

                let checkpointedForward = checkpoint { (arrays: [MLXArray]) -> [MLXArray] in
                    let hs = arrays[0], t = arrays[1]
                    let rope = (cos: arrays[2], sin: arrays[3])
                    let mod = [ModulationParams(shift: arrays[4], scale: arrays[5], gate: arrays[6])]

                    // Restore trainable parameters in block
                    let blockParams = Array(arrays[numActivations...])
                    block.update(parameters: paramTemplate.replacingValues(with: blockParams))

                    let result = block(
                        hiddenStates: hs,
                        encoderHiddenStates: nil,
                        temb: t,
                        rotaryEmb: rope,
                        modParams: mod
                    )
                    return [result]
                }

                let out = checkpointedForward(inputs)
                combinedHS = out[0]
            } else {
                // Pass encoder_hidden_states=nil since everything is in combinedHS
                combinedHS = block(
                    hiddenStates: combinedHS,
                    encoderHiddenStates: nil,
                    temb: temb,
                    rotaryEmb: ropeEmb,
                    modParams: singleMod
                )
            }

            // Memory optimization: periodic evaluation to prevent graph accumulation
            // Skip when gradient checkpointing is active (checkpoint boundaries handle segmentation)
            if !gradientCheckpointing &&
                memoryOptimization.evalFrequency > 0 &&
                (blockIdx + 1) % memoryOptimization.evalFrequency == 0 {
                eval(combinedHS)
                if memoryOptimization.clearCacheOnEval {
                    MLX.Memory.clearCache()
                }
                Flux2Debug.verbose("Eval at single-stream block \(blockIdx)")
            }
        }

        // Remove text tokens from the concatenated stream
        imgHS = combinedHS[0..., textSeqLen..., 0...]
        Flux2Debug.verbose("After single blocks (image only): \(imgHS.shape)")
        
        // --- Output ---
        // Final adaptive layer norm
        imgHS = normOut(imgHS, conditioning: temb)

        // Project to output channels
        let output = projOut(imgHS)

        return output
    }

    // MARK: - KV-Cached Forward Passes (for klein-9b-kv)

    /// KV extraction forward pass (step 0 of KV-cached denoising)
    ///
    /// Processes [ref + output] image tokens and text tokens, extracting KV cache
    /// for reference tokens at each layer. Reference tokens only self-attend.
    ///
    /// - Parameters:
    ///   - hiddenStates: Output image latents [B, S_img, 128]
    ///   - referenceHiddenStates: Reference image latents [B, S_ref, 128]
    ///   - encoderHiddenStates: Text embeddings [B, S_txt, jointAttentionDim]
    ///   - timestep: Diffusion timestep [B]
    ///   - guidance: Guidance scale [B] (optional)
    ///   - imgIds: Output image position IDs [S_img, 4]
    ///   - txtIds: Text position IDs [S_txt, 4]
    ///   - refIds: Reference image position IDs [S_ref, 4]
    /// - Returns: (noisePred [B, S_img, 128], kvCache with all layers)
    public func forwardKVExtract(
        hiddenStates: MLXArray,
        referenceHiddenStates: MLXArray,
        encoderHiddenStates: MLXArray,
        timestep: MLXArray,
        guidance: MLXArray? = nil,
        imgIds: MLXArray,
        txtIds: MLXArray,
        refIds: MLXArray
    ) -> (MLXArray, TransformerKVCache) {
        let referenceTokenCount = referenceHiddenStates.shape[1]

        // Project inputs
        let imgHS = xEmbedder(hiddenStates)
        let refHS = xEmbedder(referenceHiddenStates)
        var txtHS = contextEmbedder(encoderHiddenStates)

        // Combine ref + output for the image stream
        var combinedImgHS = concatenated([refHS, imgHS], axis: 1)

        // Timestep/guidance embeddings
        let scaledTimestep = timestep * 1000
        let scaledGuidance = guidance.map { $0 * 1000 }
        let temb = timeGuidanceEmbed(timestep: scaledTimestep, guidance: scaledGuidance)

        // RoPE for [txt, ref, img] combined IDs
        let combinedIds = concatenated([txtIds, refIds, imgIds], axis: 0)
        let ropeEmb = posEmbed(combinedIds)

        // Modulation
        let imgMod = doubleStreamModulationImg(temb)
        let txtMod = doubleStreamModulationTxt(temb)

        // Initialize KV cache
        var kvCache = TransformerKVCache(referenceTokenCount: referenceTokenCount)

        // --- Double-Stream Blocks with KV extraction ---
        for (blockIdx, block) in transformerBlocks.enumerated() {
            let (newTxt, newImg, cacheEntry) = block.callWithKVExtraction(
                hiddenStates: combinedImgHS,
                encoderHiddenStates: txtHS,
                temb: temb,
                rotaryEmb: ropeEmb,
                imgModParams: imgMod,
                txtModParams: txtMod,
                referenceTokenCount: referenceTokenCount
            )

            combinedImgHS = newImg
            txtHS = newTxt
            kvCache.setDoubleStream(blockIndex: blockIdx, entry: cacheEntry)

            // Memory optimization
            if memoryOptimization.evalFrequency > 0 &&
                (blockIdx + 1) % memoryOptimization.evalFrequency == 0 {
                eval(combinedImgHS, txtHS)
            }
        }

        if memoryOptimization.evalBetweenPhases {
            eval(combinedImgHS, txtHS)
        }

        // --- Single-Stream Blocks with KV extraction ---
        let textSeqLen = txtHS.shape[1]
        var singleHS = concatenated([txtHS, combinedImgHS], axis: 1)

        let singleMod = singleStreamModulation(temb)

        for (blockIdx, block) in singleTransformerBlocks.enumerated() {
            let (result, cacheEntry) = block.callWithKVExtraction(
                hiddenStates: singleHS,
                temb: temb,
                rotaryEmb: ropeEmb,
                modParams: singleMod,
                textLen: textSeqLen,
                referenceTokenCount: referenceTokenCount
            )

            singleHS = result
            kvCache.setSingleStream(blockIndex: blockIdx, entry: cacheEntry)

            if memoryOptimization.evalFrequency > 0 &&
                (blockIdx + 1) % memoryOptimization.evalFrequency == 0 {
                eval(singleHS)
            }
        }

        // Extract output portion (skip txt and ref tokens)
        let outputStart = textSeqLen + referenceTokenCount
        let outputHS = singleHS[0..., outputStart..., 0...]

        // Final norm and projection on output only
        let finalHS = normOut(outputHS, conditioning: temb)
        let noisePred = projOut(finalHS)

        return (noisePred, kvCache)
    }

    /// KV-cached forward pass (steps 1+ of KV-cached denoising)
    ///
    /// Processes only [output] image tokens and text tokens, using cached reference K/V.
    /// No reference tokens in the input sequence.
    ///
    /// - Parameters:
    ///   - hiddenStates: Output image latents [B, S_img, 128]
    ///   - encoderHiddenStates: Text embeddings [B, S_txt, jointAttentionDim]
    ///   - timestep: Diffusion timestep [B]
    ///   - guidance: Guidance scale [B] (optional)
    ///   - imgIds: Output image position IDs [S_img, 4]
    ///   - txtIds: Text position IDs [S_txt, 4]
    ///   - kvCache: Cached reference K/V from step 0
    /// - Returns: Predicted noise [B, S_img, 128]
    public func forwardKVCached(
        hiddenStates: MLXArray,
        encoderHiddenStates: MLXArray,
        timestep: MLXArray,
        guidance: MLXArray? = nil,
        imgIds: MLXArray,
        txtIds: MLXArray,
        kvCache: TransformerKVCache
    ) -> MLXArray {
        // Project inputs (no reference tokens)
        var imgHS = xEmbedder(hiddenStates)
        var txtHS = contextEmbedder(encoderHiddenStates)

        // Timestep/guidance embeddings
        let scaledTimestep = timestep * 1000
        let scaledGuidance = guidance.map { $0 * 1000 }
        let temb = timeGuidanceEmbed(timestep: scaledTimestep, guidance: scaledGuidance)

        // RoPE for [txt, img] only (no ref)
        let combinedIds = concatenated([txtIds, imgIds], axis: 0)
        let ropeEmb = posEmbed(combinedIds)

        // Modulation
        let imgMod = doubleStreamModulationImg(temb)
        let txtMod = doubleStreamModulationTxt(temb)

        // --- Double-Stream Blocks with cached KV ---
        for (blockIdx, block) in transformerBlocks.enumerated() {
            guard let cache = kvCache.doubleStreamEntry(at: blockIdx) else {
                fatalError("Missing KV cache for double-stream block \(blockIdx)")
            }

            let (newTxt, newImg) = block.callWithKVCached(
                hiddenStates: imgHS,
                encoderHiddenStates: txtHS,
                temb: temb,
                rotaryEmb: ropeEmb,
                imgModParams: imgMod,
                txtModParams: txtMod,
                cachedKV: cache
            )

            imgHS = newImg
            txtHS = newTxt

            if memoryOptimization.evalFrequency > 0 &&
                (blockIdx + 1) % memoryOptimization.evalFrequency == 0 {
                eval(imgHS, txtHS)
            }
        }

        if memoryOptimization.evalBetweenPhases {
            eval(imgHS, txtHS)
        }

        // --- Single-Stream Blocks with cached KV ---
        let textSeqLen = txtHS.shape[1]
        var combinedHS = concatenated([txtHS, imgHS], axis: 1)

        let singleMod = singleStreamModulation(temb)

        for (blockIdx, block) in singleTransformerBlocks.enumerated() {
            guard let cache = kvCache.singleStreamEntry(at: blockIdx) else {
                fatalError("Missing KV cache for single-stream block \(blockIdx)")
            }

            combinedHS = block.callWithKVCached(
                hiddenStates: combinedHS,
                temb: temb,
                rotaryEmb: ropeEmb,
                modParams: singleMod,
                cachedKV: cache,
                textLen: textSeqLen
            )

            if memoryOptimization.evalFrequency > 0 &&
                (blockIdx + 1) % memoryOptimization.evalFrequency == 0 {
                eval(combinedHS)
            }
        }

        // Extract output portion (skip text tokens, no ref tokens)
        imgHS = combinedHS[0..., textSeqLen..., 0...]

        // Final norm and projection
        imgHS = normOut(imgHS, conditioning: temb)
        return projOut(imgHS)
    }

    /// Convenience method with automatic position ID generation
    public func forward(
        latents: MLXArray,
        encoderHiddenStates: MLXArray,
        timestep: MLXArray,
        guidance: MLXArray? = nil,
        height: Int,
        width: Int
    ) -> MLXArray {
        let textLen = encoderHiddenStates.shape[1]

        // Generate position IDs
        let imgIds = generateImagePositionIDs(height: height, width: width)
        let txtIds = generateTextPositionIDs(length: textLen)

        return self.callAsFunction(
            hiddenStates: latents,
            encoderHiddenStates: encoderHiddenStates,
            timestep: timestep,
            guidance: guidance,
            imgIds: imgIds,
            txtIds: txtIds
        )
    }
}

// MARK: - Weight Loading Extension

extension Flux2Transformer2DModel {
    /// Load weights from safetensors files
    /// - Parameters:
    ///   - url: Directory containing weight files
    ///   - quantization: Quantization configuration
    public func loadWeights(from url: URL, quantization: TransformerQuantization) throws {
        // Weight loading implementation will be added in WeightLoader.swift
        // This is a placeholder for the interface
        fatalError("Weight loading not yet implemented - see WeightLoader.swift")
    }
}

// MARK: - Memory Management

extension Flux2Transformer2DModel {
    /// Estimated memory requirement for this model configuration
    public var estimatedMemoryGB: Int {
        // Rough estimate based on parameter count and dtype
        // 32B params * 2 bytes (bf16) / 1e9 = ~64GB for bf16
        // Quantized versions are proportionally smaller
        64
    }

    /// Clear GPU cache
    public func clearCache() {
        // MLX manages memory automatically, but we can suggest cleanup
        eval([])  // Ensure all operations are complete
    }
}
