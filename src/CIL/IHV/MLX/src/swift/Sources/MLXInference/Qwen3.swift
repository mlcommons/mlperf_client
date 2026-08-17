// Copyright © 2024 Apple Inc.

import Foundation
import MLX
import MLXFast
import MLXNN

// Port of https://github.com/ml-explore/mlx-swift-lm/blob/main/Libraries/MLXLLM/Models/Qwen3.swift
//
// Qwen3 differs from Llama/Qwen2 in three places:
//  - per-head RMSNorm applied to the queries and keys (`q_norm` / `k_norm`)
//  - `head_dim` is taken from the config rather than derived from hidden_size / heads
//  - no bias on the attention projections

private class Attention: Module {

    let heads: Int
    let kvHeads: Int
    let headDim: Int
    let scale: Float

    @ModuleInfo(key: "q_proj") var wq: Linear
    @ModuleInfo(key: "k_proj") var wk: Linear
    @ModuleInfo(key: "v_proj") var wv: Linear
    @ModuleInfo(key: "o_proj") var wo: Linear

    @ModuleInfo(key: "q_norm") var qNorm: RMSNorm
    @ModuleInfo(key: "k_norm") var kNorm: RMSNorm

    let rope: RoPE

    init(_ args: Qwen3Configuration) {
        let dim = args.hiddenSize
        self.heads = args.attentionHeads
        self.kvHeads = args.kvHeads
        self.headDim = args.resolvedHeadDimensions
        self.scale = pow(Float(headDim), -0.5)

        self._wq.wrappedValue = Linear(dim, heads * headDim, bias: args.attentionBias)
        self._wk.wrappedValue = Linear(dim, kvHeads * headDim, bias: args.attentionBias)
        self._wv.wrappedValue = Linear(dim, kvHeads * headDim, bias: args.attentionBias)
        self._wo.wrappedValue = Linear(heads * headDim, dim, bias: args.attentionBias)

        self._qNorm.wrappedValue = RMSNorm(dimensions: headDim, eps: args.rmsNormEps)
        self._kNorm.wrappedValue = RMSNorm(dimensions: headDim, eps: args.rmsNormEps)

        var ropeScale: Float = 1.0
        if let ropeScaling = args.ropeScaling, ropeScaling.type == "linear",
            let factor = ropeScaling.factor
        {
            ropeScale = 1 / factor
        }

        self.rope = RoPE(
            dimensions: headDim, traditional: args.ropeTraditional, base: args.ropeTheta,
            scale: ropeScale)
    }

    func callAsFunction(_ x: MLXArray, mask: MLXArray? = nil, cache: KVCache?) -> MLXArray {
        let (B, L) = (x.dim(0), x.dim(1))

        // Per-head RMSNorm is applied on the head_dim axis, before the heads are
        // transposed out, hence the reshape -> norm -> transpose ordering.
        var queries = qNorm(wq(x).reshaped(B, L, heads, -1)).transposed(0, 2, 1, 3)
        var keys = kNorm(wk(x).reshaped(B, L, kvHeads, -1)).transposed(0, 2, 1, 3)
        var values = wv(x).reshaped(B, L, kvHeads, -1).transposed(0, 2, 1, 3)

        if let cache {
            queries = rope(queries, offset: cache.offset)
            keys = rope(keys, offset: cache.offset)
            (keys, values) = cache.update(keys: keys, values: values)
        } else {
            queries = rope(queries)
            keys = rope(keys)
        }

        let output = MLXFast.scaledDotProductAttention(
            queries: queries, keys: keys, values: values, scale: scale, mask: mask
        )
        .transposed(0, 2, 1, 3)
        .reshaped(B, L, -1)

        return wo(output)
    }
}

private class MLP: Module, UnaryLayer {

    @ModuleInfo(key: "gate_proj") var gate: Linear
    @ModuleInfo(key: "down_proj") var down: Linear
    @ModuleInfo(key: "up_proj") var up: Linear

    init(_ args: Qwen3Configuration) {
        self._gate.wrappedValue = Linear(args.hiddenSize, args.intermediateSize, bias: false)
        self._down.wrappedValue = Linear(args.intermediateSize, args.hiddenSize, bias: false)
        self._up.wrappedValue = Linear(args.hiddenSize, args.intermediateSize, bias: false)
    }

    func callAsFunction(_ x: MLXArray) -> MLXArray {
        down(silu(gate(x)) * up(x))
    }
}

private class TransformerBlock: Module {

    @ModuleInfo(key: "self_attn") var attention: Attention
    @ModuleInfo(key: "mlp") var mlp: MLP

    @ModuleInfo(key: "input_layernorm") var inputLayerNorm: RMSNorm
    @ModuleInfo(key: "post_attention_layernorm") var postAttentionLayerNorm: RMSNorm

    init(_ args: Qwen3Configuration) {
        self._attention.wrappedValue = Attention(args)
        self._mlp.wrappedValue = MLP(args)
        self._inputLayerNorm.wrappedValue = RMSNorm(
            dimensions: args.hiddenSize, eps: args.rmsNormEps)
        self._postAttentionLayerNorm.wrappedValue = RMSNorm(
            dimensions: args.hiddenSize, eps: args.rmsNormEps)
    }

    func callAsFunction(_ x: MLXArray, mask: MLXArray? = nil, cache: KVCache?) -> MLXArray {
        var r = attention(inputLayerNorm(x), mask: mask, cache: cache)
        let h = x + r
        r = mlp(postAttentionLayerNorm(h))
        return h + r
    }
}

private class Qwen3ModelInner: Module {

    @ModuleInfo(key: "embed_tokens") var embedTokens: Embedding

    fileprivate let layers: [TransformerBlock]
    let norm: RMSNorm

    init(_ args: Qwen3Configuration) {
        precondition(args.vocabularySize > 0)

        self._embedTokens.wrappedValue = Embedding(
            embeddingCount: args.vocabularySize, dimensions: args.hiddenSize)

        self.layers = (0 ..< args.hiddenLayers).map { _ in TransformerBlock(args) }
        self.norm = RMSNorm(dimensions: args.hiddenSize, eps: args.rmsNormEps)
    }

    func callAsFunction(_ inputs: MLXArray, cache: [KVCache]? = nil) -> MLXArray {
        var h = embedTokens(inputs)

        let mask: MLXArray? = createAttentionMask(h: h, cache: cache)

        for (i, layer) in layers.enumerated() {
            h = layer(h, mask: mask, cache: cache?[i])
        }

        return norm(h)
    }

    // Used by the FLUX.2 Klein text encoder: run a single forward pass over the
    // (right-padded) prompt and return the raw hidden states after the requested
    // 1-based layer indices (no final norm). Replicates flux-2-swift-mlx's
    // Qwen3.forwardWithHiddenStates + createCausalMask (causal + key-padding).
    func collectHiddenStates(_ inputIds: MLXArray, layerIndices: [Int],
                             attentionMask: MLXArray?) -> [Int: MLXArray] {
        var h = embedTokens(inputIds)
        let mask = Self.encoderMask(seqLen: inputIds.dim(1),
                                    attentionMask: attentionMask, like: h)
        let want = Set(layerIndices)
        var out: [Int: MLXArray] = [:]
        if want.contains(0) { out[0] = h }
        for (i, layer) in layers.enumerated() {
            h = layer(h, mask: mask, cache: nil)
            let idx = i + 1
            if want.contains(idx) { eval(h); out[idx] = h }
        }
        return out
    }

    // Additive [1,1,L,L] causal mask combined with a [B,L] 1/0 key-padding mask.
    static func encoderMask(seqLen: Int, attentionMask: MLXArray?, like h: MLXArray)
        -> MLXArray {
        let row = MLXArray(Array(0..<seqLen).map { Float($0) }).expandedDimensions(axis: 1)
        let col = MLXArray(Array(0..<seqLen).map { Float($0) }).expandedDimensions(axis: 0)
        var mask = MLX.where(col .<= row, MLXArray(Float(0)), MLXArray(-Float.infinity))
        if let attn = attentionMask {
            let pad = MLX.where(attn .== Int32(1), MLXArray(Float(0)), MLXArray(Float(-1e9)))
                .reshaped([attn.dim(0), 1, 1, attn.dim(1)])
            mask = mask.reshaped([1, 1, seqLen, seqLen]) + pad
        } else {
            mask = mask.reshaped([1, 1, seqLen, seqLen])
        }
        return mask.asType(h.dtype)
    }
}

public class Qwen3Model: Module, LanguageModel, KVCacheDimensionProvider {

    public let vocabularySize: Int
    public let kvHeads: [Int]

    private let model: Qwen3ModelInner
    private let args: Qwen3Configuration

    @ModuleInfo(key: "lm_head") var lmHead: Linear?

    public init(_ args: Qwen3Configuration) {
        self.vocabularySize = args.vocabularySize
        self.kvHeads = (0 ..< args.hiddenLayers).map { _ in args.kvHeads }
        self.model = Qwen3ModelInner(args)
        self.args = args

        if !args.tieWordEmbeddings {
            self._lmHead.wrappedValue = Linear(args.hiddenSize, args.vocabularySize, bias: false)
        }
    }

    public func callAsFunction(_ inputs: MLXArray, cache: [KVCache]?) -> MLXArray {
        let out = model(inputs, cache: cache)
        if args.tieWordEmbeddings {
            return model.embedTokens.asLinear(out)
        } else if let lmHead {
            return lmHead(out)
        } else {
            fatalError(
                "Model configuration error: Neither tied embeddings nor lm_head is available")
        }
    }

    public func sanitize(weights: [String: MLXArray]) -> [String: MLXArray] {
        // Remove unused precomputed rotary frequencies; drop lm_head when tied.
        weights.filter {
            if $0.key.contains("self_attn.rotary_emb.inv_freq") { return false }
            if args.tieWordEmbeddings && $0.key.contains("lm_head.weight") { return false }
            return true
        }
    }

    public func prepare(_ input: MLXArray, cache: [KVCache], windowSize: Int?) throws -> MLXArray {
        let prefillStepSize = windowSize ?? 512
        var y = input
        while y.shape[1] > prefillStepSize {
            let inputChunk = y[0..<1, 0..<prefillStepSize]
            let result = self(inputChunk, cache: cache.isEmpty ? nil : cache)
            eval(cache)
            let oldLen = y.shape[1]
            y = y[0..<1, prefillStepSize..<oldLen]
        }

        return y
    }

    /// FLUX.2 Klein text-encoder entry point: hidden states after the given
    /// 1-based layer indices, for a right-padded prompt + key-padding mask.
    public func encoderHiddenStates(_ inputIds: MLXArray, layerIndices: [Int],
                                    attentionMask: MLXArray?) -> [Int: MLXArray] {
        model.collectHiddenStates(inputIds, layerIndices: layerIndices,
                                  attentionMask: attentionMask)
    }
}

public struct Qwen3Configuration: Codable, Sendable {

    var hiddenSize: Int
    var hiddenLayers: Int
    var intermediateSize: Int
    var attentionHeads: Int
    var headDimensions: Int?
    var rmsNormEps: Float
    var vocabularySize: Int
    var kvHeads: Int
    var ropeTheta: Float = 1_000_000
    var ropeTraditional: Bool = false
    var ropeScaling: RopeScalingWithFactorArrays?
    var tieWordEmbeddings: Bool = false
    var attentionBias: Bool = false
    var maxPositionEmbeddings: Int = 32_768

    var resolvedHeadDimensions: Int {
        headDimensions ?? (hiddenSize / attentionHeads)
    }

    enum CodingKeys: String, CodingKey {
        case hiddenSize = "hidden_size"
        case hiddenLayers = "num_hidden_layers"
        case intermediateSize = "intermediate_size"
        case attentionHeads = "num_attention_heads"
        case headDimensions = "head_dim"
        case rmsNormEps = "rms_norm_eps"
        case vocabularySize = "vocab_size"
        case kvHeads = "num_key_value_heads"
        case ropeTheta = "rope_theta"
        case ropeTraditional = "rope_traditional"
        case ropeScaling = "rope_scaling"
        case tieWordEmbeddings = "tie_word_embeddings"
        case attentionBias = "attention_bias"
        case maxPositionEmbeddings = "max_position_embeddings"
    }

    public init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)

        hiddenSize = try container.decode(Int.self, forKey: .hiddenSize)
        hiddenLayers = try container.decode(Int.self, forKey: .hiddenLayers)
        intermediateSize = try container.decode(Int.self, forKey: .intermediateSize)
        attentionHeads = try container.decode(Int.self, forKey: .attentionHeads)
        headDimensions = try container.decodeIfPresent(Int.self, forKey: .headDimensions)
        rmsNormEps = try container.decode(Float.self, forKey: .rmsNormEps)
        vocabularySize = try container.decode(Int.self, forKey: .vocabularySize)
        kvHeads = try container.decodeIfPresent(Int.self, forKey: .kvHeads) ?? attentionHeads
        ropeTheta =
            try container.decodeIfPresent(Float.self, forKey: .ropeTheta) ?? 1_000_000
        ropeTraditional =
            try container.decodeIfPresent(Bool.self, forKey: .ropeTraditional) ?? false
        ropeScaling = try container.decodeIfPresent(
            RopeScalingWithFactorArrays.self, forKey: .ropeScaling)
        tieWordEmbeddings =
            try container.decodeIfPresent(Bool.self, forKey: .tieWordEmbeddings) ?? false
        attentionBias =
            try container.decodeIfPresent(Bool.self, forKey: .attentionBias) ?? false
        maxPositionEmbeddings =
            try container.decodeIfPresent(Int.self, forKey: .maxPositionEmbeddings) ?? 32_768
    }
}
