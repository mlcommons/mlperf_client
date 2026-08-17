// TransformerKVCache.swift - KV Cache for FLUX.2-klein-9b-kv
// Copyright 2025 Vincent Gourbin

import Foundation
@preconcurrency import MLX

/// Per-layer cached K/V for reference tokens (post-RoPE)
///
/// Stores the Key and Value tensors extracted during the KV-extraction forward pass (step 0).
/// These are reused in subsequent denoising steps to avoid reprocessing reference tokens.
///
/// IMPORTANT: Keys are stored post-RoPE. Do NOT re-apply RoPE to cached keys.
public struct LayerKVCacheEntry: @unchecked Sendable {
    /// Cached keys [B, H, S_ref, D] (post-RoPE, post-QKNorm)
    public let keys: MLXArray

    /// Cached values [B, H, S_ref, D]
    public let values: MLXArray

    public init(keys: MLXArray, values: MLXArray) {
        self.keys = keys
        self.values = values
    }
}

/// Complete KV cache for all transformer layers
///
/// Holds `LayerKVCacheEntry` for every double-stream and single-stream block.
/// Created during the first denoising step (KV extraction) and reused for steps 1+.
///
/// Memory estimate: ~1 GB for 4 ref images at 512x512
/// (32 layers × 2 × 32 heads × 4096 tokens × 128 dim × 2 bytes)
public struct TransformerKVCache: @unchecked Sendable {
    /// KV cache entries for double-stream blocks, keyed by block index
    public var doubleStreamEntries: [Int: LayerKVCacheEntry]

    /// KV cache entries for single-stream blocks, keyed by block index
    public var singleStreamEntries: [Int: LayerKVCacheEntry]

    /// Number of reference tokens that were cached
    public let referenceTokenCount: Int

    public init(referenceTokenCount: Int) {
        self.doubleStreamEntries = [:]
        self.singleStreamEntries = [:]
        self.referenceTokenCount = referenceTokenCount
    }

    /// Store a double-stream block's KV cache entry
    public mutating func setDoubleStream(blockIndex: Int, entry: LayerKVCacheEntry) {
        doubleStreamEntries[blockIndex] = entry
    }

    /// Store a single-stream block's KV cache entry
    public mutating func setSingleStream(blockIndex: Int, entry: LayerKVCacheEntry) {
        singleStreamEntries[blockIndex] = entry
    }

    /// Retrieve the double-stream KV cache for a specific block
    public func doubleStreamEntry(at blockIndex: Int) -> LayerKVCacheEntry? {
        doubleStreamEntries[blockIndex]
    }

    /// Retrieve the single-stream KV cache for a specific block
    public func singleStreamEntry(at blockIndex: Int) -> LayerKVCacheEntry? {
        singleStreamEntries[blockIndex]
    }

    /// Clear all cached entries to free memory
    public mutating func clear() {
        doubleStreamEntries.removeAll()
        singleStreamEntries.removeAll()
    }

    /// Total number of cached layers
    public var layerCount: Int {
        doubleStreamEntries.count + singleStreamEntries.count
    }
}
