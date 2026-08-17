// Flux2RoPE.swift - Rotary Position Embeddings for Flux.2
// Copyright 2025 Vincent Gourbin

import Foundation
import MLX
import MLXNN

/// Cached RoPE embeddings with LRU eviction
/// Note: Not Sendable because MLXArray isn't, but thread-safety is ensured by NSLock
private struct RoPECacheEntry {
    let cos: MLXArray
    let sin: MLXArray
    var lastAccess: UInt64
}

/// 4D Rotary Position Embedding for Flux.2
///
/// Flux.2 uses a 4D RoPE with axes_dims = [32, 32, 32, 32] for a total of 128 dims.
/// This encodes position information for (T, H, W, L) axes.
///
/// Includes LRU caching to avoid recomputing embeddings for repeated position IDs.
public class Flux2RoPE: Module, @unchecked Sendable {
    let axesDims: [Int]
    let theta: Float
    let totalDims: Int

    /// Maximum number of cached entries (LRU eviction when exceeded)
    private let cacheMaxSize: Int

    /// Cache for computed embeddings, keyed by shape hash
    private var cache: [String: RoPECacheEntry] = [:]
    private var accessCounter: UInt64 = 0
    private let lock = NSLock()

    /// Initialize RoPE with given axes dimensions
    /// - Parameters:
    ///   - axesDims: Dimensions per axis [T, H, W, L], default [32, 32, 32, 32]
    ///   - theta: Base frequency for positional encoding, default 2000.0
    ///   - cacheMaxSize: Maximum cache entries (default 8)
    public init(
        axesDims: [Int] = [32, 32, 32, 32],
        theta: Float = 2000.0,
        cacheMaxSize: Int = 8
    ) {
        self.axesDims = axesDims
        self.theta = theta
        self.totalDims = axesDims.reduce(0, +)
        self.cacheMaxSize = cacheMaxSize
    }

    /// Compute frequency bands for given dimension
    private func computeFreqs(dim: Int, maxLen: Int) -> MLXArray {
        let halfDim = dim / 2
        let freqSeq = MLXArray(0..<halfDim).asType(.float32)
        let invFreq = 1.0 / pow(MLXArray(theta), freqSeq / Float(halfDim))

        let positions = MLXArray(0..<maxLen).asType(.float32).expandedDimensions(axis: 1)
        let freqs = positions * invFreq.expandedDimensions(axis: 0)

        return freqs
    }

    /// Generate cache key from position IDs shape
    /// For deterministic position IDs (text/image), shape uniquely identifies the embeddings
    private func cacheKey(for ids: MLXArray) -> String {
        // Shape-based key: seqLen determines the embeddings
        // We also include a hash of first/last positions for extra safety
        let seqLen = ids.shape[0]
        return "rope_\(seqLen)"
    }

    /// Generate rotary embeddings for position IDs
    /// - Parameter ids: Position IDs [S, 4] where 4 corresponds to (T, H, W, L)
    /// - Returns: Tuple of (cos, sin) embeddings each of shape [S, totalDims]
    public func callAsFunction(_ ids: MLXArray) -> (cos: MLXArray, sin: MLXArray) {
        let key = cacheKey(for: ids)

        // Check cache with lock
        lock.lock()
        if var entry = cache[key] {
            accessCounter += 1
            entry.lastAccess = accessCounter
            cache[key] = entry
            lock.unlock()
            Flux2Debug.verbose("RoPE cache hit for \(key)")
            return (cos: entry.cos, sin: entry.sin)
        }
        lock.unlock()

        Flux2Debug.verbose("RoPE cache miss for \(key), computing...")

        // Compute embeddings
        let (cosEmb, sinEmb) = computeEmbeddings(for: ids)

        // Store in cache with LRU eviction
        lock.lock()
        accessCounter += 1

        // Evict LRU entry if cache is full
        if cache.count >= cacheMaxSize {
            if let lruKey = cache.min(by: { $0.value.lastAccess < $1.value.lastAccess })?.key {
                cache.removeValue(forKey: lruKey)
                Flux2Debug.verbose("RoPE cache evicted \(lruKey)")
            }
        }

        cache[key] = RoPECacheEntry(cos: cosEmb, sin: sinEmb, lastAccess: accessCounter)
        lock.unlock()

        return (cos: cosEmb, sin: sinEmb)
    }

    /// Clear the RoPE embedding cache
    public func clearCache() {
        lock.lock()
        cache.removeAll()
        accessCounter = 0
        lock.unlock()
        Flux2Debug.verbose("RoPE cache cleared")
    }

    /// Compute embeddings (internal method)
    private func computeEmbeddings(for ids: MLXArray) -> (cos: MLXArray, sin: MLXArray) {
        var cosComponents: [MLXArray] = []
        var sinComponents: [MLXArray] = []

        let seqLen = ids.shape[0]

        // Process each axis - INTERLEAVE EACH AXIS FIRST, THEN CONCATENATE
        // This matches diffusers: repeat_interleave(2) per axis, then cat all axes
        for (axisIdx, axisDim) in axesDims.enumerated() {
            // Get position indices for this axis
            let axisPositions = ids[0..., axisIdx]  // [S]

            // Compute frequencies for this axis dimension
            // diffusers: freqs = 1.0 / (theta ** (arange(0, dim, 2) / dim))
            // For dim=32: arange(0, 32, 2) = [0, 2, 4, ..., 30] and /32 gives [0, 1/16, 2/16, ...]
            let freqSeq = MLXArray(stride(from: 0, to: axisDim, by: 2)).asType(.float32)
            let invFreq = 1.0 / pow(MLXArray(theta), freqSeq / Float(axisDim))

            // Compute position * frequency
            let posExpanded = axisPositions.asType(.float32).expandedDimensions(axis: 1)  // [S, 1]
            let freqExpanded = invFreq.expandedDimensions(axis: 0)  // [1, halfDim]
            let freqs = posExpanded * freqExpanded  // [S, halfDim]

            // Compute cos and sin
            let axisCosHalf = cos(freqs)  // [S, halfDim]
            let axisSinHalf = sin(freqs)  // [S, halfDim]

            // Interleave THIS axis: [c0, c0, c1, c1, ...] like repeat_interleave(2, dim=1)
            // [S, halfDim] -> [S, halfDim, 1] -> [S, halfDim, 2] -> [S, axisDim]
            let cosExpanded = axisCosHalf.expandedDimensions(axis: -1)  // [S, halfDim, 1]
            let cosInterleaved = concatenated([cosExpanded, cosExpanded], axis: -1)  // [S, halfDim, 2]
            let axisCos = cosInterleaved.reshaped([seqLen, axisDim])  // [S, axisDim]

            let sinExpanded = axisSinHalf.expandedDimensions(axis: -1)  // [S, halfDim, 1]
            let sinInterleaved = concatenated([sinExpanded, sinExpanded], axis: -1)  // [S, halfDim, 2]
            let axisSin = sinInterleaved.reshaped([seqLen, axisDim])  // [S, axisDim]

            cosComponents.append(axisCos)
            sinComponents.append(axisSin)
        }

        // Now concatenate all ALREADY INTERLEAVED axis embeddings
        let cosEmb = concatenated(cosComponents, axis: -1)  // [S, totalDims]
        let sinEmb = concatenated(sinComponents, axis: -1)  // [S, totalDims]

        return (cos: cosEmb, sin: sinEmb)
    }

    /// Apply rotary embeddings to query and key tensors
    /// - Parameters:
    ///   - q: Query tensor [B, H, S, D]
    ///   - k: Key tensor [B, H, S, D]
    ///   - cos: Cosine embeddings [S, D]
    ///   - sin: Sine embeddings [S, D]
    /// - Returns: Tuple of rotated (query, key)
    public func apply(
        query q: MLXArray,
        key k: MLXArray,
        cos cosEmb: MLXArray,
        sin sinEmb: MLXArray
    ) -> (query: MLXArray, key: MLXArray) {
        let rotatedQ = applyRotary(q, cos: cosEmb, sin: sinEmb)
        let rotatedK = applyRotary(k, cos: cosEmb, sin: sinEmb)
        return (query: rotatedQ, key: rotatedK)
    }

    /// Apply rotary embedding to a single tensor
    /// Uses the same approach as diffusers: treat consecutive pairs as (real, imag)
    private func applyRotary(_ x: MLXArray, cos cosEmb: MLXArray, sin sinEmb: MLXArray) -> MLXArray {
        // x shape: [B, H, S, D]
        // cos/sin shape: [S, D]

        // Try fused Metal kernel first (requires float32 cos/sin)
        let cosFloat32 = cosEmb.dtype == .float32 ? cosEmb : cosEmb.asType(.float32)
        let sinFloat32 = sinEmb.dtype == .float32 ? sinEmb : sinEmb.asType(.float32)

        if let fused = Flux2FusedKernels.applyRotaryEmb(x, cos: cosFloat32, sin: sinFloat32) {
            return fused
        }

        // Fall back to reference implementation
        return applyRotaryReference(x, cos: cosEmb, sin: sinEmb)
    }

    /// Reference implementation of rotary embeddings (fallback when kernel unavailable)
    private func applyRotaryReference(_ x: MLXArray, cos cosEmb: MLXArray, sin sinEmb: MLXArray) -> MLXArray {
        // x shape: [B, H, S, D]
        // cos/sin shape: [S, D]

        // Reshape for broadcasting
        let cosExpanded = cosEmb.expandedDimensions(axes: [0, 1])  // [1, 1, S, D]
        let sinExpanded = sinEmb.expandedDimensions(axes: [0, 1])  // [1, 1, S, D]

        // Diffusers approach: reshape x to [B, H, S, D//2, 2] and unbind to get x_real, x_imag
        // x_real = x[..., 0::2], x_imag = x[..., 1::2]  (every other element)
        // x_rotated = stack([-x_imag, x_real], dim=-1).flatten(3)

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

        // Create rotated: [-x_imag, x_real] interleaved
        let negXImag = -xImag
        // Stack and flatten: [[-x_imag[0], x_real[0]], [-x_imag[1], x_real[1]], ...]
        let xRotatedStacked = stacked([negXImag, xReal], axis: -1)  // [B, H, S, D/2, 2]
        let xRotated = xRotatedStacked.reshaped([batchSize, numHeads, seqLen, dim])  // [B, H, S, D]

        // Apply rotation: x * cos + x_rotated * sin
        return x * cosExpanded + xRotated * sinExpanded
    }
}

/// Generate position IDs for image latents using GPU-native operations
/// - Parameters:
///   - height: Image height in latent space
///   - width: Image width in latent space
/// - Returns: Position IDs [H*W, 4]
public func generateImagePositionIDs(height: Int, width: Int) -> MLXArray {
    // GPU-native implementation using meshgrid-like operations
    // Create H and W coordinate grids
    let hIndices = MLXArray.arange(height, dtype: .int32)  // [H]
    let wIndices = MLXArray.arange(width, dtype: .int32)   // [W]

    // Create meshgrid: expand H to [H, W] and W to [H, W] using broadcastTo
    let hExpanded = hIndices.expandedDimensions(axis: 1)  // [H, 1]
    let wExpanded = wIndices.expandedDimensions(axis: 0)  // [1, W]
    let hGrid = MLX.broadcast(hExpanded, to: [height, width])  // [H, W]
    let wGrid = MLX.broadcast(wExpanded, to: [height, width])  // [H, W]

    // Flatten to [H*W]
    let hFlat = hGrid.reshaped([height * width])
    let wFlat = wGrid.reshaped([height * width])

    // Create T=0 and L=0 columns
    let zeros = MLXArray.zeros([height * width], dtype: .int32)

    // Stack to create [H*W, 4] with format [T, H, W, L]
    return MLX.stacked([zeros, hFlat, wFlat, zeros], axis: 1)
}

/// Generate position IDs for text sequence using GPU-native operations
/// - Parameter length: Text sequence length
/// - Returns: Position IDs [length, 4]
public func generateTextPositionIDs(length: Int) -> MLXArray {
    // GPU-native implementation
    // Create L column as [0, 1, 2, ..., length-1]
    let lIndices = MLXArray.arange(length, dtype: .int32)  // [length]

    // Create T=0, H=0, W=0 columns
    let zeros = MLXArray.zeros([length], dtype: .int32)

    // Stack to create [length, 4] with format [T, H, W, L]
    return MLX.stacked([zeros, zeros, zeros, lIndices], axis: 1)
}

/// Combine text and image position IDs for joint attention
/// - Parameters:
///   - textLength: Text sequence length
///   - height: Image height in latent space
///   - width: Image width in latent space
/// - Returns: Combined position IDs [textLength + H*W, 4]
public func generateCombinedPositionIDs(
    textLength: Int,
    height: Int,
    width: Int
) -> MLXArray {
    let textIds = generateTextPositionIDs(length: textLength)
    let imageIds = generateImagePositionIDs(height: height, width: width)
    return concatenated([textIds, imageIds], axis: 0)
}
