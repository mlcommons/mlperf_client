// Flux2Config.swift - Flux.2 Transformer Configuration
// Copyright 2025 Vincent Gourbin

import Foundation

// MARK: - Model Selection

/// Flux.2 model variants
public enum Flux2Model: String, CaseIterable, Sendable {
    /// Flux.2 Dev - 32B parameters, Mistral text encoder
    case dev = "dev"

    /// Flux.2 Klein 4B - 4B parameters, Qwen3-4B text encoder (Apache 2.0)
    case klein4B = "klein-4b"

    /// Flux.2 Klein 4B Base - Non-distilled version for LoRA training (Apache 2.0)
    case klein4BBase = "klein-4b-base"

    /// Flux.2 Klein 9B - 9B parameters, Qwen3-8B text encoder (Non-commercial)
    case klein9B = "klein-9b"

    /// Flux.2 Klein 9B Base - Non-distilled version for LoRA training (Non-commercial)
    case klein9BBase = "klein-9b-base"

    /// Flux.2 Klein 9B KV - KV-cached variant for faster multi-reference I2I (Non-commercial)
    case klein9BKV = "klein-9b-kv"

    public var displayName: String {
        switch self {
        case .dev: return "Flux.2 Dev (32B)"
        case .klein4B: return "Flux.2 Klein 4B"
        case .klein4BBase: return "Flux.2 Klein 4B Base"
        case .klein9B: return "Flux.2 Klein 9B"
        case .klein9BBase: return "Flux.2 Klein 9B Base"
        case .klein9BKV: return "Flux.2 Klein 9B KV"
        }
    }

    /// Whether this is a base (non-distilled) model for training
    public var isBaseModel: Bool {
        switch self {
        case .klein4BBase, .klein9BBase: return true
        case .dev, .klein4B, .klein9B, .klein9BKV: return false
        }
    }

    /// Whether this model can be used for inference
    public var isForInference: Bool {
        switch self {
        case .dev, .klein4B, .klein9B, .klein9BKV: return true
        case .klein4BBase, .klein9BBase: return false  // Base models not for inference
        }
    }

    /// Whether this model can be used for LoRA training
    public var isForTraining: Bool {
        switch self {
        case .dev: return true  // Dev bf16 can train
        case .klein4BBase, .klein9BBase: return true  // Base models for training
        case .klein4B, .klein9B, .klein9BKV: return false  // Distilled cannot train
        }
    }

    /// Get the base (non-distilled) variant for training, if available
    public var trainingVariant: Flux2Model {
        switch self {
        case .klein4B, .klein4BBase: return .klein4BBase
        case .klein9B, .klein9BBase, .klein9BKV: return .klein9BBase
        case .dev: return .dev  // Dev doesn't have a separate base model
        }
    }

    /// Get the distilled variant for inference (validation images during training)
    public var inferenceVariant: Flux2Model {
        switch self {
        case .klein4B, .klein4BBase: return .klein4B
        case .klein9B, .klein9BBase, .klein9BKV: return .klein9B
        case .dev: return .dev
        }
    }

    /// Whether this model uses guidance embeddings
    public var usesGuidanceEmbeds: Bool {
        switch self {
        case .dev: return true
        case .klein4B, .klein4BBase, .klein9B, .klein9BBase, .klein9BKV: return false
        }
    }

    /// Joint attention dimension (text encoder output)
    public var jointAttentionDim: Int {
        switch self {
        case .dev: return 15360      // Mistral: 3 × 5120
        case .klein4B, .klein4BBase: return 7680   // Qwen3-4B: 3 × 2560
        case .klein9B, .klein9BBase, .klein9BKV: return 12288  // Qwen3-8B: 3 × 4096
        }
    }

    /// Get the transformer configuration for this model
    public var transformerConfig: Flux2TransformerConfig {
        switch self {
        case .dev: return .flux2Dev
        case .klein4B, .klein4BBase: return .klein4B
        case .klein9B, .klein9BBase, .klein9BKV: return .klein9B
        }
    }

    /// Estimated VRAM usage in GB
    public var estimatedVRAM: Int {
        switch self {
        case .dev: return 60     // ~32GB transformer + ~25GB Mistral
        case .klein4B, .klein4BBase: return 13  // ~8GB transformer + ~5GB Qwen3-4B
        case .klein9B, .klein9BBase, .klein9BKV: return 29  // ~18GB transformer + ~10GB Qwen3-8B
        }
    }

    /// License information
    public var license: String {
        switch self {
        case .dev: return "FLUX.2 Non-Commercial"
        case .klein4B, .klein4BBase: return "Apache 2.0"
        case .klein9B, .klein9BBase, .klein9BKV: return "Non-Commercial"
        }
    }

    /// Whether this model can be used commercially
    public var isCommercialUseAllowed: Bool {
        switch self {
        case .klein4B, .klein4BBase: return true
        case .dev, .klein9B, .klein9BBase, .klein9BKV: return false
        }
    }

    /// Recommended number of inference steps for optimal quality
    public var defaultSteps: Int {
        switch self {
        case .dev: return 28
        case .klein4B, .klein9B, .klein9BKV: return 4
        case .klein4BBase, .klein9BBase: return 28  // Base models need more steps
        }
    }

    /// Recommended guidance scale for optimal quality
    public var defaultGuidance: Float {
        switch self {
        case .dev: return 4.0
        case .klein4B, .klein9B, .klein9BKV: return 1.0
        case .klein4BBase, .klein9BBase: return 3.5
        }
    }

    /// Whether this model expects **classical CFG** at inference time —
    /// i.e. two transformer passes per step (cond + uncond with empty
    /// negative prompt) combined as `uncond + guidance * (cond - uncond)`.
    ///
    /// - `klein4BBase` / `klein9BBase`: ✅ true. These are *non-distilled*
    ///   models. Their attention behaviour and any LoRA trained against
    ///   them (e.g. `flux2-klein9b-lora-mlsharp-3d-repair`) depend on the
    ///   uncond/cond delta — single-pass inference produces visually
    ///   plausible output but cannot follow conditioning that wasn't
    ///   trivially copyable from the references (e.g. camera deltas).
    ///   Matches diffusers' `Flux2KleinPipeline` which uses
    ///   `negative_prompt = ""` and `guidance_scale = 4.0` by default.
    ///
    /// - All others: ❌ false. `dev` uses embedded-guidance (a single
    ///   transformer pass with a guidance tensor); the distilled / KV
    ///   klein variants were CFG-distilled so they expect one pass at
    ///   guidance = 1.0.
    public var usesClassicalCFG: Bool {
        switch self {
        case .klein4BBase, .klein9BBase: return true
        case .dev, .klein4B, .klein9B, .klein9BKV: return false
        }
    }

    /// Estimated generation time in seconds (1024x1024 on M2 Max)
    public var estimatedTimeSeconds: Int {
        switch self {
        case .dev: return 2100      // ~35 minutes
        case .klein4B: return 26    // ~26 seconds
        case .klein4BBase: return 180  // ~3 minutes (28 steps)
        case .klein9B: return 62    // ~62 seconds
        case .klein9BKV: return 23  // ~23 seconds (2.66x speedup with KV cache, I2I only)
        case .klein9BBase: return 420  // ~7 minutes (28 steps)
        }
    }

    /// Maximum number of reference images for I2I generation
    /// Source: https://docs.bfl.ai/flux_2/flux2_image_editing
    public var maxReferenceImages: Int {
        switch self {
        case .dev: return 6         // Limited by memory
        case .klein4B, .klein4BBase, .klein9B, .klein9BBase, .klein9BKV: return 4
        }
    }

    /// Whether this model supports KV-cached denoising for faster I2I
    /// When true, step 0 extracts KV cache from reference tokens, and steps 1+ reuse cached KV
    public var supportsKVCache: Bool {
        switch self {
        case .klein9BKV: return true
        default: return false
        }
    }
}

// MARK: - Transformer Configuration

/// Configuration for the Flux.2 diffusion transformer
public struct Flux2TransformerConfig: Codable, Sendable {
    /// Patch size for input embedding (1 for Flux.2)
    public var patchSize: Int

    /// Number of input channels (128 for Flux.2 latents)
    public var inChannels: Int

    /// Number of output channels (same as input)
    public var outChannels: Int

    /// Number of double-stream transformer blocks
    public var numLayers: Int

    /// Number of single-stream transformer blocks
    public var numSingleLayers: Int

    /// Dimension of each attention head
    public var attentionHeadDim: Int

    /// Number of attention heads
    public var numAttentionHeads: Int

    /// Inner dimension for transformer (numAttentionHeads * attentionHeadDim)
    public var innerDim: Int {
        numAttentionHeads * attentionHeadDim
    }

    /// Dimension of joint attention (from Mistral embeddings: 15360)
    public var jointAttentionDim: Int

    /// Dimension of pooled projection (time + guidance embeddings)
    public var pooledProjectionDim: Int

    /// Whether to use guidance embedding
    public var guidanceEmbeds: Bool

    /// Axes dimensions for RoPE [T, H, W, L]
    public var axesDimsRope: [Int]

    /// Base theta for RoPE
    public var ropeTheta: Float

    /// MLP expansion ratio (3.0 for Flux.2, determines FFN hidden dimension)
    public var mlpRatio: Float

    /// Activation function for feedforward
    public var activationFunction: String

    public init(
        patchSize: Int = 1,
        inChannels: Int = 128,
        outChannels: Int = 128,
        numLayers: Int = 8,
        numSingleLayers: Int = 48,
        attentionHeadDim: Int = 128,
        numAttentionHeads: Int = 48,
        jointAttentionDim: Int = 15360,
        pooledProjectionDim: Int = 768,
        guidanceEmbeds: Bool = true,
        axesDimsRope: [Int] = [32, 32, 32, 32],
        ropeTheta: Float = 2000.0,
        mlpRatio: Float = 3.0,
        activationFunction: String = "silu"
    ) {
        self.patchSize = patchSize
        self.inChannels = inChannels
        self.outChannels = outChannels
        self.numLayers = numLayers
        self.numSingleLayers = numSingleLayers
        self.attentionHeadDim = attentionHeadDim
        self.numAttentionHeads = numAttentionHeads
        self.jointAttentionDim = jointAttentionDim
        self.pooledProjectionDim = pooledProjectionDim
        self.guidanceEmbeds = guidanceEmbeds
        self.axesDimsRope = axesDimsRope
        self.ropeTheta = ropeTheta
        self.mlpRatio = mlpRatio
        self.activationFunction = activationFunction
    }

    /// Default Flux.2 Dev configuration (32B)
    public static let flux2Dev = Flux2TransformerConfig()

    /// Flux.2 Klein 4B configuration
    /// 4B parameters, 5 double + 20 single blocks
    public static let klein4B = Flux2TransformerConfig(
        patchSize: 1,
        inChannels: 128,
        outChannels: 128,
        numLayers: 5,
        numSingleLayers: 20,
        attentionHeadDim: 128,
        numAttentionHeads: 24,      // 24 × 128 = 3072
        jointAttentionDim: 7680,    // Qwen3-4B: 3 × 2560
        pooledProjectionDim: 768,
        guidanceEmbeds: false,
        axesDimsRope: [32, 32, 32, 32],
        ropeTheta: 2000.0,
        mlpRatio: 3.0,
        activationFunction: "silu"
    )

    /// Flux.2 Klein 9B configuration
    /// 9B parameters, 8 double + 24 single blocks
    public static let klein9B = Flux2TransformerConfig(
        patchSize: 1,
        inChannels: 128,
        outChannels: 128,
        numLayers: 8,
        numSingleLayers: 24,
        attentionHeadDim: 128,
        numAttentionHeads: 32,      // 32 × 128 = 4096
        jointAttentionDim: 12288,   // Qwen3-8B: 3 × 4096
        pooledProjectionDim: 768,
        guidanceEmbeds: false,
        axesDimsRope: [32, 32, 32, 32],
        ropeTheta: 2000.0,
        mlpRatio: 3.0,
        activationFunction: "silu"
    )

    // MARK: - Codable

    enum CodingKeys: String, CodingKey {
        case patchSize = "patch_size"
        case inChannels = "in_channels"
        case outChannels = "out_channels"
        case numLayers = "num_layers"
        case numSingleLayers = "num_single_layers"
        case attentionHeadDim = "attention_head_dim"
        case numAttentionHeads = "num_attention_heads"
        case jointAttentionDim = "joint_attention_dim"
        case pooledProjectionDim = "pooled_projection_dim"
        case guidanceEmbeds = "guidance_embeds"
        case axesDimsRope = "axes_dims_rope"
        case ropeTheta = "rope_theta"
        case mlpRatio = "mlp_ratio"
        case activationFunction = "activation_function"
    }

    public init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)

        patchSize = try container.decodeIfPresent(Int.self, forKey: .patchSize) ?? 1
        inChannels = try container.decodeIfPresent(Int.self, forKey: .inChannels) ?? 128
        outChannels = try container.decodeIfPresent(Int.self, forKey: .outChannels) ?? 128
        numLayers = try container.decodeIfPresent(Int.self, forKey: .numLayers) ?? 8
        numSingleLayers = try container.decodeIfPresent(Int.self, forKey: .numSingleLayers) ?? 48
        attentionHeadDim = try container.decodeIfPresent(Int.self, forKey: .attentionHeadDim) ?? 128
        numAttentionHeads = try container.decodeIfPresent(Int.self, forKey: .numAttentionHeads) ?? 48
        jointAttentionDim = try container.decodeIfPresent(Int.self, forKey: .jointAttentionDim) ?? 15360
        pooledProjectionDim = try container.decodeIfPresent(Int.self, forKey: .pooledProjectionDim) ?? 768
        guidanceEmbeds = try container.decodeIfPresent(Bool.self, forKey: .guidanceEmbeds) ?? true
        axesDimsRope = try container.decodeIfPresent([Int].self, forKey: .axesDimsRope) ?? [32, 32, 32, 32]
        ropeTheta = try container.decodeIfPresent(Float.self, forKey: .ropeTheta) ?? 2000.0
        mlpRatio = try container.decodeIfPresent(Float.self, forKey: .mlpRatio) ?? 3.0
        activationFunction = try container.decodeIfPresent(String.self, forKey: .activationFunction) ?? "silu"
    }

    /// Load configuration from a JSON file
    public static func load(from url: URL) throws -> Flux2TransformerConfig {
        let data = try Data(contentsOf: url)
        return try JSONDecoder().decode(Flux2TransformerConfig.self, from: data)
    }
}

extension Flux2TransformerConfig: CustomStringConvertible {
    public var description: String {
        """
        Flux2TransformerConfig(
            layers: \(numLayers) double + \(numSingleLayers) single,
            heads: \(numAttentionHeads) × \(attentionHeadDim) = \(innerDim),
            jointDim: \(jointAttentionDim),
            rope: \(axesDimsRope) θ=\(ropeTheta)
        )
        """
    }
}
