// Flux2TransformerBlock.swift - Double-Stream Transformer Block
// Copyright 2025 Vincent Gourbin

import Foundation
import MLX
import MLXNN

/// Double-Stream Transformer Block for Flux.2
///
/// Processes image and text hidden states in parallel streams with:
/// 1. Separate LayerNorm for each modality
/// 2. Joint attention across both modalities
/// 3. Separate FeedForward for each modality
/// 4. AdaLN modulation from timestep embeddings
///
/// There are 8 such blocks in Flux.2.
public class Flux2TransformerBlock: Module, @unchecked Sendable {
    let dim: Int
    let numHeads: Int
    let headDim: Int

    // Layer norms for image stream
    let norm1: LayerNorm
    let norm2: LayerNorm

    // Layer norms for text stream
    let norm1Context: LayerNorm
    let norm2Context: LayerNorm

    // Joint attention
    let attn: Flux2Attention

    // FeedForward networks
    let ff: Flux2FeedForward
    let ffContext: Flux2FeedForward

    /// Initialize Double-Stream Block
    /// - Parameters:
    ///   - dim: Model dimension (6144)
    ///   - numHeads: Number of attention heads (48)
    ///   - headDim: Dimension per head (128)
    ///   - mlpRatio: MLP expansion ratio
    public init(
        dim: Int,
        numHeads: Int,
        headDim: Int,
        mlpRatio: Float = 3.0
    ) {
        self.dim = dim
        self.numHeads = numHeads
        self.headDim = headDim

        let mlpHidden = Int(Float(dim) * mlpRatio)

        // Image stream norms
        self.norm1 = LayerNorm(dimensions: dim, eps: 1e-6, affine: false)
        self.norm2 = LayerNorm(dimensions: dim, eps: 1e-6, affine: false)

        // Text stream norms
        self.norm1Context = LayerNorm(dimensions: dim, eps: 1e-6, affine: false)
        self.norm2Context = LayerNorm(dimensions: dim, eps: 1e-6, affine: false)

        // Joint attention
        self.attn = Flux2Attention(dim: dim, numHeads: numHeads, headDim: headDim)

        // FeedForward
        self.ff = Flux2FeedForward(dim: dim, innerDim: mlpHidden)
        self.ffContext = Flux2FeedForward(dim: dim, innerDim: mlpHidden)
    }

    /// Forward pass
    /// - Parameters:
    ///   - hiddenStates: Image hidden states [B, S_img, dim]
    ///   - encoderHiddenStates: Text hidden states [B, S_txt, dim]
    ///   - temb: Timestep embedding [B, dim]
    ///   - rotaryEmb: Optional RoPE embeddings
    ///   - imgModParams: Modulation params for image stream (2 sets: attn, ffn)
    ///   - txtModParams: Modulation params for text stream (2 sets: attn, ffn)
    /// - Returns: Updated (text hidden states, image hidden states)
    public func callAsFunction(
        hiddenStates: MLXArray,
        encoderHiddenStates: MLXArray,
        temb: MLXArray,
        rotaryEmb: (cos: MLXArray, sin: MLXArray)? = nil,
        imgModParams: [ModulationParams]? = nil,
        txtModParams: [ModulationParams]? = nil
    ) -> (encoderHiddenStates: MLXArray, hiddenStates: MLXArray) {
        // Store residuals
        let residualImg = hiddenStates
        let residualTxt = encoderHiddenStates

        // --- Attention Block ---

        // Normalize image hidden states
        var imgNorm = norm1(hiddenStates)

        // Normalize text hidden states
        var txtNorm = norm1Context(encoderHiddenStates)

        // Apply modulation if provided
        if let imgMod = imgModParams, imgMod.count >= 1 {
            imgNorm = applyModulation(imgNorm, shift: imgMod[0].shift, scale: imgMod[0].scale)
        }
        if let txtMod = txtModParams, txtMod.count >= 1 {
            txtNorm = applyModulation(txtNorm, shift: txtMod[0].shift, scale: txtMod[0].scale)
        }

        // Joint attention
        let (imgAttnOut, txtAttnOut) = attn(
            hiddenStates: imgNorm,
            encoderHiddenStates: txtNorm,
            rotaryEmb: rotaryEmb
        )

        // Apply gate and add residual
        var imgOut: MLXArray
        var txtOut: MLXArray

        if let imgMod = imgModParams, imgMod.count >= 1 {
            imgOut = residualImg + applyGate(imgAttnOut, gate: imgMod[0].gate)
        } else {
            imgOut = residualImg + imgAttnOut
        }

        if let txtMod = txtModParams, txtMod.count >= 1 {
            txtOut = residualTxt + applyGate(txtAttnOut, gate: txtMod[0].gate)
        } else {
            txtOut = residualTxt + txtAttnOut
        }

        // --- FeedForward Block ---

        // Store new residuals
        let residualImg2 = imgOut
        let residualTxt2 = txtOut

        // Normalize
        var imgNorm2 = norm2(imgOut)
        var txtNorm2 = norm2Context(txtOut)

        // Apply modulation if provided
        if let imgMod = imgModParams, imgMod.count >= 2 {
            imgNorm2 = applyModulation(imgNorm2, shift: imgMod[1].shift, scale: imgMod[1].scale)
        }
        if let txtMod = txtModParams, txtMod.count >= 2 {
            txtNorm2 = applyModulation(txtNorm2, shift: txtMod[1].shift, scale: txtMod[1].scale)
        }

        // FeedForward
        let imgFFOut = ff(imgNorm2)
        let txtFFOut = ffContext(txtNorm2)

        // Apply gate and add residual
        if let imgMod = imgModParams, imgMod.count >= 2 {
            imgOut = residualImg2 + applyGate(imgFFOut, gate: imgMod[1].gate)
        } else {
            imgOut = residualImg2 + imgFFOut
        }

        if let txtMod = txtModParams, txtMod.count >= 2 {
            txtOut = residualTxt2 + applyGate(txtFFOut, gate: txtMod[1].gate)
        } else {
            txtOut = residualTxt2 + txtFFOut
        }

        // Return order matches diffusers: (encoder_hidden_states, hidden_states)
        return (encoderHiddenStates: txtOut, hiddenStates: imgOut)
    }

    // MARK: - KV Cache Methods (for klein-9b-kv)

    /// Forward pass with KV extraction (step 0 of KV-cached denoising)
    public func callWithKVExtraction(
        hiddenStates: MLXArray,
        encoderHiddenStates: MLXArray,
        temb: MLXArray,
        rotaryEmb: (cos: MLXArray, sin: MLXArray),
        imgModParams: [ModulationParams]?,
        txtModParams: [ModulationParams]?,
        referenceTokenCount: Int
    ) -> (encoderHiddenStates: MLXArray, hiddenStates: MLXArray, cache: LayerKVCacheEntry) {
        let residualImg = hiddenStates
        let residualTxt = encoderHiddenStates

        // --- Attention Block ---
        var imgNorm = norm1(hiddenStates)
        var txtNorm = norm1Context(encoderHiddenStates)

        if let imgMod = imgModParams, imgMod.count >= 1 {
            imgNorm = applyModulation(imgNorm, shift: imgMod[0].shift, scale: imgMod[0].scale)
        }
        if let txtMod = txtModParams, txtMod.count >= 1 {
            txtNorm = applyModulation(txtNorm, shift: txtMod[0].shift, scale: txtMod[0].scale)
        }

        // Joint attention with KV extraction
        let (imgAttnOut, txtAttnOut, cacheEntry) = attn.callWithKVExtraction(
            hiddenStates: imgNorm,
            encoderHiddenStates: txtNorm,
            rotaryEmb: rotaryEmb,
            referenceTokenCount: referenceTokenCount
        )

        // Apply gate and add residual
        var imgOut: MLXArray
        var txtOut: MLXArray

        if let imgMod = imgModParams, imgMod.count >= 1 {
            imgOut = residualImg + applyGate(imgAttnOut, gate: imgMod[0].gate)
        } else {
            imgOut = residualImg + imgAttnOut
        }

        if let txtMod = txtModParams, txtMod.count >= 1 {
            txtOut = residualTxt + applyGate(txtAttnOut, gate: txtMod[0].gate)
        } else {
            txtOut = residualTxt + txtAttnOut
        }

        // --- FeedForward Block ---
        let residualImg2 = imgOut
        let residualTxt2 = txtOut

        var imgNorm2 = norm2(imgOut)
        var txtNorm2 = norm2Context(txtOut)

        if let imgMod = imgModParams, imgMod.count >= 2 {
            imgNorm2 = applyModulation(imgNorm2, shift: imgMod[1].shift, scale: imgMod[1].scale)
        }
        if let txtMod = txtModParams, txtMod.count >= 2 {
            txtNorm2 = applyModulation(txtNorm2, shift: txtMod[1].shift, scale: txtMod[1].scale)
        }

        let imgFFOut = ff(imgNorm2)
        let txtFFOut = ffContext(txtNorm2)

        if let imgMod = imgModParams, imgMod.count >= 2 {
            imgOut = residualImg2 + applyGate(imgFFOut, gate: imgMod[1].gate)
        } else {
            imgOut = residualImg2 + imgFFOut
        }

        if let txtMod = txtModParams, txtMod.count >= 2 {
            txtOut = residualTxt2 + applyGate(txtFFOut, gate: txtMod[1].gate)
        } else {
            txtOut = residualTxt2 + txtFFOut
        }

        return (encoderHiddenStates: txtOut, hiddenStates: imgOut, cache: cacheEntry)
    }

    /// Forward pass with cached KV (steps 1+ of KV-cached denoising)
    public func callWithKVCached(
        hiddenStates: MLXArray,
        encoderHiddenStates: MLXArray,
        temb: MLXArray,
        rotaryEmb: (cos: MLXArray, sin: MLXArray),
        imgModParams: [ModulationParams]?,
        txtModParams: [ModulationParams]?,
        cachedKV: LayerKVCacheEntry
    ) -> (encoderHiddenStates: MLXArray, hiddenStates: MLXArray) {
        let residualImg = hiddenStates
        let residualTxt = encoderHiddenStates

        var imgNorm = norm1(hiddenStates)
        var txtNorm = norm1Context(encoderHiddenStates)

        if let imgMod = imgModParams, imgMod.count >= 1 {
            imgNorm = applyModulation(imgNorm, shift: imgMod[0].shift, scale: imgMod[0].scale)
        }
        if let txtMod = txtModParams, txtMod.count >= 1 {
            txtNorm = applyModulation(txtNorm, shift: txtMod[0].shift, scale: txtMod[0].scale)
        }

        let (imgAttnOut, txtAttnOut) = attn.callWithKVCached(
            hiddenStates: imgNorm,
            encoderHiddenStates: txtNorm,
            rotaryEmb: rotaryEmb,
            cachedKV: cachedKV
        )

        var imgOut: MLXArray
        var txtOut: MLXArray

        if let imgMod = imgModParams, imgMod.count >= 1 {
            imgOut = residualImg + applyGate(imgAttnOut, gate: imgMod[0].gate)
        } else {
            imgOut = residualImg + imgAttnOut
        }

        if let txtMod = txtModParams, txtMod.count >= 1 {
            txtOut = residualTxt + applyGate(txtAttnOut, gate: txtMod[0].gate)
        } else {
            txtOut = residualTxt + txtAttnOut
        }

        // FeedForward
        let residualImg2 = imgOut
        let residualTxt2 = txtOut

        var imgNorm2 = norm2(imgOut)
        var txtNorm2 = norm2Context(txtOut)

        if let imgMod = imgModParams, imgMod.count >= 2 {
            imgNorm2 = applyModulation(imgNorm2, shift: imgMod[1].shift, scale: imgMod[1].scale)
        }
        if let txtMod = txtModParams, txtMod.count >= 2 {
            txtNorm2 = applyModulation(txtNorm2, shift: txtMod[1].shift, scale: txtMod[1].scale)
        }

        let imgFFOut = ff(imgNorm2)
        let txtFFOut = ffContext(txtNorm2)

        if let imgMod = imgModParams, imgMod.count >= 2 {
            imgOut = residualImg2 + applyGate(imgFFOut, gate: imgMod[1].gate)
        } else {
            imgOut = residualImg2 + imgFFOut
        }

        if let txtMod = txtModParams, txtMod.count >= 2 {
            txtOut = residualTxt2 + applyGate(txtFFOut, gate: txtMod[1].gate)
        } else {
            txtOut = residualTxt2 + txtFFOut
        }

        return (encoderHiddenStates: txtOut, hiddenStates: imgOut)
    }
}

/// Stack of Double-Stream Transformer Blocks
public class Flux2TransformerBlocks: Module, @unchecked Sendable {
    let blocks: [Flux2TransformerBlock]

    public init(
        numBlocks: Int,
        dim: Int,
        numHeads: Int,
        headDim: Int,
        mlpRatio: Float = 3.0
    ) {
        self.blocks = (0..<numBlocks).map { _ in
            Flux2TransformerBlock(
                dim: dim,
                numHeads: numHeads,
                headDim: headDim,
                mlpRatio: mlpRatio
            )
        }
    }

    public func callAsFunction(
        hiddenStates: MLXArray,
        encoderHiddenStates: MLXArray,
        temb: MLXArray,
        rotaryEmb: (cos: MLXArray, sin: MLXArray)? = nil,
        imgModulation: Flux2Modulation? = nil,
        txtModulation: Flux2Modulation? = nil
    ) -> (encoderHiddenStates: MLXArray, hiddenStates: MLXArray) {
        var imgHS = hiddenStates
        var txtHS = encoderHiddenStates

        for block in blocks {
            // Get modulation params if modulators provided
            let imgParams = imgModulation?(temb)
            let txtParams = txtModulation?(temb)

            let (newTxt, newImg) = block(
                hiddenStates: imgHS,
                encoderHiddenStates: txtHS,
                temb: temb,
                rotaryEmb: rotaryEmb,
                imgModParams: imgParams,
                txtModParams: txtParams
            )

            imgHS = newImg
            txtHS = newTxt
        }

        return (encoderHiddenStates: txtHS, hiddenStates: imgHS)
    }
}
