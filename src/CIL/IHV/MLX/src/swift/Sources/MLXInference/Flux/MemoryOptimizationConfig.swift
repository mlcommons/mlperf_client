// MemoryOptimizationConfig.swift - Memory optimization settings
// Copyright 2025 Vincent Gourbin

import Foundation

/// Configuration for memory optimization during transformer inference
///
/// These settings control how aggressively the transformer manages memory
/// by forcing evaluation of the computation graph at intervals.
///
/// # Background
/// MLX uses lazy evaluation and builds a computation graph. For large models,
/// this graph can grow very large and consume significant memory. Periodically
/// calling `eval()` materializes intermediate results and allows the graph to
/// be garbage collected.
///
/// # Trade-offs
/// - Lower `evalFrequency` = more memory savings, but slower inference
/// - Higher `evalFrequency` = faster inference, but more memory usage
/// - `clearCacheOnEval` = additional memory savings at cost of recomputation
///
/// # Reference
/// Based on optimizations from LTX-2-MLX: https://github.com/Acelogic/LTX-2-MLX
public struct MemoryOptimizationConfig: Sendable, Equatable {

    /// How often to evaluate the computation graph during transformer forward pass.
    ///
    /// - `0` = disabled (fastest, most memory)
    /// - `4` = evaluate every 4 blocks (aggressive memory savings)
    /// - `8` = evaluate every 8 blocks (moderate, recommended)
    /// - `16` = evaluate every 16 blocks (light memory savings)
    ///
    /// For Flux.2 Dev with 8 double + 48 single blocks:
    /// - `evalFrequency: 8` will eval ~7 times during forward pass
    /// - `evalFrequency: 4` will eval ~14 times during forward pass
    public var evalFrequency: Int

    /// Whether to clear GPU cache after each evaluation.
    ///
    /// When enabled, calls `MLX.Memory.clearCache()` after each periodic eval.
    /// This provides additional memory savings but may cause recomputation
    /// of cached values.
    ///
    /// Recommended: `false` for most cases, `true` only if running out of memory
    public var clearCacheOnEval: Bool

    /// Whether to evaluate after all double-stream blocks complete.
    ///
    /// This ensures the transition between double-stream and single-stream
    /// phases has a clean memory state.
    public var evalBetweenPhases: Bool

    public init(
        evalFrequency: Int = 0,
        clearCacheOnEval: Bool = false,
        evalBetweenPhases: Bool = true
    ) {
        self.evalFrequency = max(0, evalFrequency)
        self.clearCacheOnEval = clearCacheOnEval
        self.evalBetweenPhases = evalBetweenPhases
    }

    // MARK: - Presets

    /// Disabled - no periodic evaluation (fastest, most memory)
    ///
    /// Use when you have plenty of RAM and want maximum speed.
    public static let disabled = MemoryOptimizationConfig(
        evalFrequency: 0,
        clearCacheOnEval: false,
        evalBetweenPhases: false
    )

    /// Light - evaluate every 16 blocks
    ///
    /// Minimal impact on speed with some memory savings.
    public static let light = MemoryOptimizationConfig(
        evalFrequency: 16,
        clearCacheOnEval: false,
        evalBetweenPhases: true
    )

    /// Moderate - evaluate every 8 blocks (recommended default)
    ///
    /// Good balance between speed and memory usage.
    public static let moderate = MemoryOptimizationConfig(
        evalFrequency: 8,
        clearCacheOnEval: false,
        evalBetweenPhases: true
    )

    /// Aggressive - evaluate every 4 blocks with cache clearing
    ///
    /// Use when running low on memory. Slower but uses significantly less RAM.
    public static let aggressive = MemoryOptimizationConfig(
        evalFrequency: 4,
        clearCacheOnEval: true,
        evalBetweenPhases: true
    )

    /// Ultra low memory - evaluate every 2 blocks with cache clearing
    ///
    /// Maximum memory savings at significant speed cost.
    /// Use only if other presets cause out-of-memory errors.
    public static let ultraLowMemory = MemoryOptimizationConfig(
        evalFrequency: 2,
        clearCacheOnEval: true,
        evalBetweenPhases: true
    )
}

// MARK: - Description

extension MemoryOptimizationConfig: CustomStringConvertible {
    public var description: String {
        if evalFrequency == 0 && !evalBetweenPhases {
            return "MemoryOptimization(disabled)"
        }

        var parts: [String] = []
        if evalFrequency > 0 {
            parts.append("evalEvery: \(evalFrequency)")
        }
        if clearCacheOnEval {
            parts.append("clearCache: true")
        }
        if evalBetweenPhases {
            parts.append("evalBetweenPhases: true")
        }

        return "MemoryOptimization(\(parts.joined(separator: ", ")))"
    }
}

// MARK: - Recommendation

extension MemoryOptimizationConfig {

    /// Get recommended configuration based on available RAM
    ///
    /// - Parameter ramGB: System RAM in gigabytes
    /// - Returns: Recommended memory optimization preset
    public static func recommended(forRAMGB ramGB: Int) -> MemoryOptimizationConfig {
        switch ramGB {
        case 0..<32:
            return .ultraLowMemory
        case 32..<64:
            return .aggressive
        case 64..<96:
            return .moderate
        case 96..<128:
            return .light
        default:
            return .disabled
        }
    }
}
