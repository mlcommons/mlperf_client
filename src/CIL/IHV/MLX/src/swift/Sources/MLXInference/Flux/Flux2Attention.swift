// Flux2Attention.swift - Joint Attention for Flux.2 Double-Stream Blocks
// Copyright 2025 Vincent Gourbin

import Foundation
import MLX
import MLXNN
import MLXFast

/// RMS Normalization (Root Mean Square Layer Normalization)
/// Optimized using MLXFast.rmsNorm for better performance
public class Flux2RMSNorm: Module, @unchecked Sendable {
    let dim: Int
    let eps: Float
    let weight: MLXArray

    public init(dim: Int, eps: Float = 1e-6) {
        self.dim = dim
        self.eps = eps
        self.weight = MLXArray.ones([dim])
    }

    public func callAsFunction(_ x: MLXArray) -> MLXArray {
        // Use optimized MLXFast implementation
        return MLXFast.rmsNorm(x, weight: weight, eps: eps)
    }
}

/// Joint Attention for Double-Stream Blocks
///
/// Processes both image hidden states and text encoder hidden states together.
/// Uses separate Q/K/V projections for each modality, then performs joint attention.
public class Flux2Attention: Module, @unchecked Sendable {
    let dim: Int
    let numHeads: Int
    let headDim: Int

    // Image hidden states projections (@ModuleInfo for LoRA injection via update)
    @ModuleInfo var toQ: Linear
    @ModuleInfo var toK: Linear
    @ModuleInfo var toV: Linear

    // Text encoder hidden states projections
    @ModuleInfo var addQProj: Linear
    @ModuleInfo var addKProj: Linear
    @ModuleInfo var addVProj: Linear

    // QK normalization (Flux2RMSNorm)
    let normQ: Flux2RMSNorm
    let normK: Flux2RMSNorm
    let normAddedQ: Flux2RMSNorm
    let normAddedK: Flux2RMSNorm

    // Output projections
    @ModuleInfo var toOut: Linear
    @ModuleInfo var toAddOut: Linear

    /// Initialize Joint Attention
    /// - Parameters:
    ///   - dim: Model dimension (6144 for Flux.2)
    ///   - numHeads: Number of attention heads (48)
    ///   - headDim: Dimension per head (128)
    ///   - contextDim: Dimension of text encoder hidden states (same as dim after projection)
    public init(
        dim: Int,
        numHeads: Int,
        headDim: Int,
        contextDim: Int? = nil
    ) {
        self.dim = dim
        self.numHeads = numHeads
        self.headDim = headDim

        let innerDim = numHeads * headDim
        let ctxDim = contextDim ?? dim

        // Image projections (no bias to match checkpoint)
        self._toQ.wrappedValue = Linear(dim, innerDim, bias: false)
        self._toK.wrappedValue = Linear(dim, innerDim, bias: false)
        self._toV.wrappedValue = Linear(dim, innerDim, bias: false)

        // Text projections (no bias to match checkpoint)
        self._addQProj.wrappedValue = Linear(ctxDim, innerDim, bias: false)
        self._addKProj.wrappedValue = Linear(ctxDim, innerDim, bias: false)
        self._addVProj.wrappedValue = Linear(ctxDim, innerDim, bias: false)

        // QK normalization
        self.normQ = Flux2RMSNorm(dim: headDim)
        self.normK = Flux2RMSNorm(dim: headDim)
        self.normAddedQ = Flux2RMSNorm(dim: headDim)
        self.normAddedK = Flux2RMSNorm(dim: headDim)

        // Output projections (no bias to match checkpoint)
        self._toOut.wrappedValue = Linear(innerDim, dim, bias: false)
        self._toAddOut.wrappedValue = Linear(innerDim, ctxDim, bias: false)
    }

    /// Forward pass for joint attention
    /// - Parameters:
    ///   - hiddenStates: Image hidden states [B, S_img, dim]
    ///   - encoderHiddenStates: Text encoder hidden states [B, S_txt, dim]
    ///   - rotaryEmb: Optional RoPE embeddings (cos, sin)
    /// - Returns: Tuple of updated (image hidden states, text hidden states)
    public func callAsFunction(
        hiddenStates: MLXArray,
        encoderHiddenStates: MLXArray,
        rotaryEmb: (cos: MLXArray, sin: MLXArray)? = nil
    ) -> (hiddenStates: MLXArray, encoderHiddenStates: MLXArray) {
        Flux2Debug.verbose("Flux2Attention input - img: \(hiddenStates.shape), txt: \(encoderHiddenStates.shape)")

        let batchSize = hiddenStates.shape[0]
        let seqLenImg = hiddenStates.shape[1]
        let seqLenTxt = encoderHiddenStates.shape[1]

        // Project image to Q, K, V
        var q = toQ(hiddenStates)
        var k = toK(hiddenStates)
        let v = toV(hiddenStates)
        Flux2Debug.verbose("Image Q/K/V projected: q=\(q.shape), k=\(k.shape), v=\(v.shape)")

        // Project text to Q, K, V
        var addedQ = addQProj(encoderHiddenStates)
        var addedK = addKProj(encoderHiddenStates)
        let addedV = addVProj(encoderHiddenStates)
        Flux2Debug.verbose("Text Q/K/V projected: addedQ=\(addedQ.shape), addedK=\(addedK.shape), addedV=\(addedV.shape)")

        // Reshape for multi-head attention: [B, S, H*D] -> [B, H, S, D]
        q = reshapeForAttention(q, batchSize: batchSize, seqLen: seqLenImg)
        k = reshapeForAttention(k, batchSize: batchSize, seqLen: seqLenImg)
        let vReshaped = reshapeForAttention(v, batchSize: batchSize, seqLen: seqLenImg)
        Flux2Debug.verbose("Image after reshape: q=\(q.shape), k=\(k.shape), v=\(vReshaped.shape)")

        addedQ = reshapeForAttention(addedQ, batchSize: batchSize, seqLen: seqLenTxt)
        addedK = reshapeForAttention(addedK, batchSize: batchSize, seqLen: seqLenTxt)
        let addedVReshaped = reshapeForAttention(addedV, batchSize: batchSize, seqLen: seqLenTxt)
        Flux2Debug.verbose("Text after reshape: addedQ=\(addedQ.shape), addedK=\(addedK.shape), addedV=\(addedVReshaped.shape)")

        // Apply QK normalization
        q = normQ(q)
        k = normK(k)
        addedQ = normAddedQ(addedQ)
        addedK = normAddedK(addedK)
        Flux2Debug.verbose("After QK norm: q=\(q.shape), addedQ=\(addedQ.shape)")

        // Apply RoPE if provided
        if let rope = rotaryEmb {
            // Split RoPE for text and image portions
            // Note: combined IDs are [txtIds, imgIds], so text comes first
            let txtCos = rope.cos[0..<seqLenTxt]
            let txtSin = rope.sin[0..<seqLenTxt]
            let imgCos = rope.cos[seqLenTxt..<(seqLenTxt + seqLenImg)]
            let imgSin = rope.sin[seqLenTxt..<(seqLenTxt + seqLenImg)]

            Flux2Debug.verbose("RoPE split - txtCos: \(txtCos.shape), imgCos: \(imgCos.shape)")

            (q, k) = applyRoPE(q: q, k: k, cos: imgCos, sin: imgSin)
            (addedQ, addedK) = applyRoPE(q: addedQ, k: addedK, cos: txtCos, sin: txtSin)
            Flux2Debug.verbose("After RoPE: q=\(q.shape), addedQ=\(addedQ.shape)")
        }

        // Concatenate image and text for joint attention
        let concatQ = concatenated([addedQ, q], axis: 2)  // [B, H, S_txt+S_img, D]
        let concatK = concatenated([addedK, k], axis: 2)
        let concatV = concatenated([addedVReshaped, vReshaped], axis: 2)
        Flux2Debug.verbose("Concatenated for attention: Q=\(concatQ.shape), K=\(concatK.shape), V=\(concatV.shape)")

        // Compute attention using MLXFast scaled dot product attention
        Flux2Debug.verbose("Calling scaledDotProductAttention...")
        let attnOutput = MLXFast.scaledDotProductAttention(
            queries: concatQ,
            keys: concatK,
            values: concatV,
            scale: Float(1.0 / sqrt(Float(headDim))),
            mask: nil
        )
        Flux2Debug.verbose("Attention output: \(attnOutput.shape)")

        // Split back into text and image portions
        let txtAttnOutput = attnOutput[0..., 0..., 0..<seqLenTxt, 0...]
        let imgAttnOutput = attnOutput[0..., 0..., seqLenTxt..., 0...]
        Flux2Debug.verbose("Split attention: txt=\(txtAttnOutput.shape), img=\(imgAttnOutput.shape)")

        // Reshape back: [B, H, S, D] -> [B, S, H*D]
        let imgOut = reshapeFromAttention(imgAttnOutput, batchSize: batchSize, seqLen: seqLenImg)
        let txtOut = reshapeFromAttention(txtAttnOutput, batchSize: batchSize, seqLen: seqLenTxt)
        Flux2Debug.verbose("After reshape from attn: img=\(imgOut.shape), txt=\(txtOut.shape)")

        // Project outputs
        let hiddenStatesOut = toOut(imgOut)
        let encoderHiddenStatesOut = toAddOut(txtOut)
        Flux2Debug.verbose("Final outputs: img=\(hiddenStatesOut.shape), txt=\(encoderHiddenStatesOut.shape)")

        return (hiddenStates: hiddenStatesOut, encoderHiddenStates: encoderHiddenStatesOut)
    }

    // MARK: - Helper Functions

    /// Reshape tensor for multi-head attention
    /// [B, S, H*D] -> [B, H, S, D]
    private func reshapeForAttention(_ x: MLXArray, batchSize: Int, seqLen: Int) -> MLXArray {
        x.reshaped([batchSize, seqLen, numHeads, headDim])
            .transposed(0, 2, 1, 3)
    }

    /// Reshape tensor from multi-head attention
    /// [B, H, S, D] -> [B, S, H*D]
    private func reshapeFromAttention(_ x: MLXArray, batchSize: Int, seqLen: Int) -> MLXArray {
        x.transposed(0, 2, 1, 3)
            .reshaped([batchSize, seqLen, numHeads * headDim])
    }

    /// Apply rotary position embeddings
    private func applyRoPE(
        q: MLXArray,
        k: MLXArray,
        cos: MLXArray,
        sin: MLXArray
    ) -> (MLXArray, MLXArray) {
        // cos/sin: [S, D] -> [1, 1, S, D]
        let cosExpanded = cos.expandedDimensions(axes: [0, 1])
        let sinExpanded = sin.expandedDimensions(axes: [0, 1])

        let qRotated = rotateHalf(q)
        let kRotated = rotateHalf(k)

        let qOut = q * cosExpanded + qRotated * sinExpanded
        let kOut = k * cosExpanded + kRotated * sinExpanded

        return (qOut, kOut)
    }

    // MARK: - KV Cache Methods (for klein-9b-kv)

    /// Forward pass with KV extraction for reference tokens (step 0 of KV-cached denoising)
    ///
    /// In this mode, the image stream contains [ref + output] tokens.
    /// An attention mask ensures reference tokens only self-attend (no cross-attend with output).
    /// After attention, the reference K/V are extracted and returned for caching.
    ///
    /// - Parameters:
    ///   - hiddenStates: Image hidden states [B, S_ref+S_img, dim] (ref tokens + output tokens)
    ///   - encoderHiddenStates: Text encoder hidden states [B, S_txt, dim]
    ///   - rotaryEmb: RoPE embeddings for combined sequence [txtIds, refIds, imgIds]
    ///   - referenceTokenCount: Number of reference tokens at the start of hiddenStates
    /// - Returns: (hiddenStates, encoderHiddenStates, LayerKVCacheEntry with ref K/V)
    public func callWithKVExtraction(
        hiddenStates: MLXArray,
        encoderHiddenStates: MLXArray,
        rotaryEmb: (cos: MLXArray, sin: MLXArray),
        referenceTokenCount: Int
    ) -> (hiddenStates: MLXArray, encoderHiddenStates: MLXArray, cache: LayerKVCacheEntry) {
        let batchSize = hiddenStates.shape[0]
        let seqLenImg = hiddenStates.shape[1]  // ref + output tokens
        let seqLenTxt = encoderHiddenStates.shape[1]

        // Project image (ref + output) to Q, K, V
        var q = toQ(hiddenStates)
        var k = toK(hiddenStates)
        let v = toV(hiddenStates)

        // Project text to Q, K, V
        var addedQ = addQProj(encoderHiddenStates)
        var addedK = addKProj(encoderHiddenStates)
        let addedV = addVProj(encoderHiddenStates)

        // Reshape for multi-head attention
        q = reshapeForAttention(q, batchSize: batchSize, seqLen: seqLenImg)
        k = reshapeForAttention(k, batchSize: batchSize, seqLen: seqLenImg)
        let vReshaped = reshapeForAttention(v, batchSize: batchSize, seqLen: seqLenImg)

        addedQ = reshapeForAttention(addedQ, batchSize: batchSize, seqLen: seqLenTxt)
        addedK = reshapeForAttention(addedK, batchSize: batchSize, seqLen: seqLenTxt)
        let addedVReshaped = reshapeForAttention(addedV, batchSize: batchSize, seqLen: seqLenTxt)

        // Apply QK normalization
        q = normQ(q)
        k = normK(k)
        addedQ = normAddedQ(addedQ)
        addedK = normAddedK(addedK)

        // Apply RoPE
        // RoPE order: [txt, ref, img] matching position IDs
        let txtCos = rotaryEmb.cos[0..<seqLenTxt]
        let txtSin = rotaryEmb.sin[0..<seqLenTxt]
        let imgCos = rotaryEmb.cos[seqLenTxt..<(seqLenTxt + seqLenImg)]
        let imgSin = rotaryEmb.sin[seqLenTxt..<(seqLenTxt + seqLenImg)]

        (q, k) = applyRoPE(q: q, k: k, cos: imgCos, sin: imgSin)
        (addedQ, addedK) = applyRoPE(q: addedQ, k: addedK, cos: txtCos, sin: txtSin)

        // Extract reference K/V AFTER RoPE (so cached keys are post-RoPE)
        // Image K/V layout: [B, H, S_ref+S_img, D] -> ref portion is first referenceTokenCount
        let refK = k[0..., 0..., 0..<referenceTokenCount, 0...]
        let refV = vReshaped[0..., 0..., 0..<referenceTokenCount, 0...]
        let cacheEntry = LayerKVCacheEntry(keys: refK, values: refV)

        // Build attention mask for KV extraction mode:
        // - Reference queries only attend to text + reference (not output)
        // - Text and output queries attend to everything
        // Sequence order in joint attention: [txt, ref+img]
        let totalSeq = seqLenTxt + seqLenImg
        let mask = buildKVExtractionMask(
            textLen: seqLenTxt,
            refLen: referenceTokenCount,
            outputLen: seqLenImg - referenceTokenCount,
            totalSeq: totalSeq
        )

        // Concatenate for joint attention: [txt, ref+img]
        let concatQ = concatenated([addedQ, q], axis: 2)
        let concatK = concatenated([addedK, k], axis: 2)
        let concatV = concatenated([addedVReshaped, vReshaped], axis: 2)

        // Compute masked attention
        let attnOutput = MLXFast.scaledDotProductAttention(
            queries: concatQ,
            keys: concatK,
            values: concatV,
            scale: Float(1.0 / sqrt(Float(headDim))),
            mask: mask
        )

        // Split back into text and image portions
        let txtAttnOutput = attnOutput[0..., 0..., 0..<seqLenTxt, 0...]
        let imgAttnOutput = attnOutput[0..., 0..., seqLenTxt..., 0...]

        // Reshape and project
        let imgOut = reshapeFromAttention(imgAttnOutput, batchSize: batchSize, seqLen: seqLenImg)
        let txtOut = reshapeFromAttention(txtAttnOutput, batchSize: batchSize, seqLen: seqLenTxt)

        let hiddenStatesOut = toOut(imgOut)
        let encoderHiddenStatesOut = toAddOut(txtOut)

        return (hiddenStates: hiddenStatesOut, encoderHiddenStates: encoderHiddenStatesOut, cache: cacheEntry)
    }

    /// Forward pass with cached KV for reference tokens (steps 1+ of KV-cached denoising)
    ///
    /// In this mode, only [output] tokens are in the image stream (no reference tokens).
    /// Cached reference K/V from step 0 are concatenated with current K/V.
    ///
    /// - Parameters:
    ///   - hiddenStates: Image hidden states [B, S_img, dim] (output tokens only)
    ///   - encoderHiddenStates: Text encoder hidden states [B, S_txt, dim]
    ///   - rotaryEmb: RoPE embeddings for [txtIds, imgIds] (no refIds)
    ///   - cachedKV: Cached reference K/V from step 0 (post-RoPE)
    /// - Returns: (hiddenStates, encoderHiddenStates)
    public func callWithKVCached(
        hiddenStates: MLXArray,
        encoderHiddenStates: MLXArray,
        rotaryEmb: (cos: MLXArray, sin: MLXArray),
        cachedKV: LayerKVCacheEntry
    ) -> (hiddenStates: MLXArray, encoderHiddenStates: MLXArray) {
        let batchSize = hiddenStates.shape[0]
        let seqLenImg = hiddenStates.shape[1]
        let seqLenTxt = encoderHiddenStates.shape[1]

        // Project current tokens
        var q = toQ(hiddenStates)
        var k = toK(hiddenStates)
        let v = toV(hiddenStates)

        var addedQ = addQProj(encoderHiddenStates)
        var addedK = addKProj(encoderHiddenStates)
        let addedV = addVProj(encoderHiddenStates)

        // Reshape
        q = reshapeForAttention(q, batchSize: batchSize, seqLen: seqLenImg)
        k = reshapeForAttention(k, batchSize: batchSize, seqLen: seqLenImg)
        let vReshaped = reshapeForAttention(v, batchSize: batchSize, seqLen: seqLenImg)

        addedQ = reshapeForAttention(addedQ, batchSize: batchSize, seqLen: seqLenTxt)
        addedK = reshapeForAttention(addedK, batchSize: batchSize, seqLen: seqLenTxt)
        let addedVReshaped = reshapeForAttention(addedV, batchSize: batchSize, seqLen: seqLenTxt)

        // QK norm
        q = normQ(q)
        k = normK(k)
        addedQ = normAddedQ(addedQ)
        addedK = normAddedK(addedK)

        // Apply RoPE to current tokens only
        let txtCos = rotaryEmb.cos[0..<seqLenTxt]
        let txtSin = rotaryEmb.sin[0..<seqLenTxt]
        let imgCos = rotaryEmb.cos[seqLenTxt..<(seqLenTxt + seqLenImg)]
        let imgSin = rotaryEmb.sin[seqLenTxt..<(seqLenTxt + seqLenImg)]

        (q, k) = applyRoPE(q: q, k: k, cos: imgCos, sin: imgSin)
        (addedQ, addedK) = applyRoPE(q: addedQ, k: addedK, cos: txtCos, sin: txtSin)

        // Concatenate K/V with cached reference K/V
        // Order: [txt_K, cached_ref_K, img_K] for keys
        // Cached ref K is already post-RoPE — do NOT re-apply
        let concatQ = concatenated([addedQ, q], axis: 2)
        let concatK = concatenated([addedK, cachedKV.keys, k], axis: 2)
        let concatV = concatenated([addedVReshaped, cachedKV.values, vReshaped], axis: 2)

        // No mask needed: all positions can attend to all (including cached ref)
        let attnOutput = MLXFast.scaledDotProductAttention(
            queries: concatQ,
            keys: concatK,
            values: concatV,
            scale: Float(1.0 / sqrt(Float(headDim))),
            mask: nil
        )

        // Split back
        let txtAttnOutput = attnOutput[0..., 0..., 0..<seqLenTxt, 0...]
        let imgAttnOutput = attnOutput[0..., 0..., seqLenTxt..., 0...]

        let imgOut = reshapeFromAttention(imgAttnOutput, batchSize: batchSize, seqLen: seqLenImg)
        let txtOut = reshapeFromAttention(txtAttnOutput, batchSize: batchSize, seqLen: seqLenTxt)

        return (hiddenStates: toOut(imgOut), encoderHiddenStates: toAddOut(txtOut))
    }

    /// Build attention mask for KV extraction step
    /// Reference queries should only attend to text + reference tokens (not output)
    /// Text and output queries attend to everything
    ///
    /// Joint attention sequence order: [txt, ref, output]
    /// Returns additive mask: 0.0 for allowed, -inf for blocked
    func buildKVExtractionMask(textLen: Int, refLen: Int, outputLen: Int, totalSeq: Int) -> MLXArray {
        // Create mask filled with 0 (all allowed)
        var maskData = [Float](repeating: 0.0, count: totalSeq * totalSeq)

        // Block reference queries from attending to output keys
        // Reference query indices: textLen ..< (textLen + refLen)
        // Output key indices: (textLen + refLen) ..< totalSeq
        for qIdx in textLen..<(textLen + refLen) {
            for kIdx in (textLen + refLen)..<totalSeq {
                maskData[qIdx * totalSeq + kIdx] = -Float.infinity
            }
        }

        // Shape: [1, 1, totalSeq, totalSeq] for broadcasting over batch and heads
        return MLXArray(maskData).reshaped([1, 1, totalSeq, totalSeq])
    }

    // MARK: - Helper Functions

    /// Rotate features for RoPE (diffusers-style: treat consecutive pairs as real/imag)
    private func rotateHalf(_ x: MLXArray) -> MLXArray {
        // x shape: [B, H, S, D]
        // Diffusers approach: reshape to [B, H, S, D/2, 2], then [-imag, real]
        let batchSize = x.shape[0]
        let numHeads = x.shape[1]
        let seqLen = x.shape[2]
        let dim = x.shape[3]
        let halfDim = dim / 2

        // Reshape to [B, H, S, D/2, 2]
        let xReshaped = x.reshaped([batchSize, numHeads, seqLen, halfDim, 2])

        // Get real and imag parts (consecutive pairs)
        let xReal = xReshaped[0..., 0..., 0..., 0..., 0]  // [B, H, S, D/2]
        let xImag = xReshaped[0..., 0..., 0..., 0..., 1]  // [B, H, S, D/2]

        // Create rotated: stack [-imag, real] and flatten
        let xRotatedStacked = stacked([-xImag, xReal], axis: -1)  // [B, H, S, D/2, 2]
        return xRotatedStacked.reshaped([batchSize, numHeads, seqLen, dim])  // [B, H, S, D]
    }
}
