// QuantizationConfig.swift - Fine-grained quantization configuration
// Copyright 2025 Vincent Gourbin

import Foundation

/// Quantization level for the Mistral text encoder
public enum MistralQuantization: String, CaseIterable, Codable, Sendable {
    case bf16 = "bf16"      // Full precision ~48GB
    case mlx8bit = "8bit"   // 8-bit ~25GB
    case mlx6bit = "6bit"   // 6-bit ~19GB
    case mlx4bit = "4bit"   // 4-bit ~14GB

    public var estimatedMemoryGB: Int {
        switch self {
        case .bf16: return 48
        case .mlx8bit: return 25
        case .mlx6bit: return 19
        case .mlx4bit: return 14
        }
    }

    public var displayName: String {
        switch self {
        case .bf16: return "Full Precision (bf16)"
        case .mlx8bit: return "8-bit"
        case .mlx6bit: return "6-bit"
        case .mlx4bit: return "4-bit"
        }
    }
}

/// Quantization level for the Flux.2 diffusion transformer
public enum TransformerQuantization: String, CaseIterable, Codable, Sendable {
    case bf16 = "bf16"      // Full precision ~64GB
    case qint8 = "qint8"    // 8-bit ~32GB
    case int4 = "int4"      // 4-bit ~16GB (on-the-fly quantization)

    public var estimatedMemoryGB: Int {
        switch self {
        case .bf16: return 64
        case .qint8: return 32
        case .int4: return 16
        }
    }

    public var displayName: String {
        switch self {
        case .bf16: return "Full Precision (bf16)"
        case .qint8: return "8-bit (qint8)"
        case .int4: return "4-bit (int4)"
        }
    }

    public var bits: Int {
        switch self {
        case .bf16: return 16
        case .qint8: return 8
        case .int4: return 4
        }
    }

    public var groupSize: Int { 64 }
}

/// Independent quantization configuration for text encoder and transformer
public struct Flux2QuantizationConfig: Codable, Sendable {
    /// Quantization for the Mistral text encoder
    public var textEncoder: MistralQuantization

    /// Quantization for the Flux.2 diffusion transformer
    public var transformer: TransformerQuantization

    public init(
        textEncoder: MistralQuantization,
        transformer: TransformerQuantization
    ) {
        self.textEncoder = textEncoder
        self.transformer = transformer
    }

    /// Total estimated memory requirement in GB
    public var estimatedTotalMemoryGB: Int {
        // Text encoder and transformer are never loaded simultaneously
        // So we take the max of the two, plus VAE (~3GB) and working memory (~5GB)
        max(textEncoder.estimatedMemoryGB, transformer.estimatedMemoryGB) + 8
    }

    /// Peak memory during text encoding phase
    public var textEncodingPhaseMemoryGB: Int {
        textEncoder.estimatedMemoryGB + 1  // +1GB for embeddings
    }

    /// Peak memory during image generation phase
    public var imageGenerationPhaseMemoryGB: Int {
        transformer.estimatedMemoryGB + 3 + 5  // +3GB VAE, +5GB working memory
    }

    // MARK: - Presets

    /// High quality preset - requires ~90GB+ RAM
    public static let highQuality = Flux2QuantizationConfig(
        textEncoder: .bf16,
        transformer: .bf16
    )

    /// Balanced preset - requires ~57GB RAM (recommended for 64GB Macs)
    public static let balanced = Flux2QuantizationConfig(
        textEncoder: .mlx8bit,
        transformer: .qint8
    )

    /// Memory efficient preset - requires ~47GB RAM
    public static let memoryEfficient = Flux2QuantizationConfig(
        textEncoder: .mlx4bit,
        transformer: .qint8
    )

    /// Minimal preset - requires ~47GB RAM
    public static let minimal = Flux2QuantizationConfig(
        textEncoder: .mlx4bit,
        transformer: .qint8
    )

    /// Ultra-minimal preset - requires ~30GB RAM (4-bit transformer)
    public static let ultraMinimal = Flux2QuantizationConfig(
        textEncoder: .mlx4bit,
        transformer: .int4
    )

    /// Default preset (balanced)
    public static let `default` = balanced
}

extension Flux2QuantizationConfig: CustomStringConvertible {
    public var description: String {
        "Flux2QuantizationConfig(text: \(textEncoder.rawValue), transformer: \(transformer.rawValue), ~\(estimatedTotalMemoryGB)GB)"
    }
}
