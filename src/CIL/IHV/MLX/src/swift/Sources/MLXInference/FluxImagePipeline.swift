import Foundation
import MLX
import MLXNN
import MLXRandom
import Tokenizers

// FLUX.2 Klein 4B text-to-image pipeline.
//
// Ported from flux-2-swift-mlx (MIT) — the implementation the dev validated.
// Uses the diffusers-layout weights from black-forest-labs/FLUX.2-klein-4B:
//   <modelDir>/{transformer,vae,text_encoder,tokenizer}/...
// Text encoder = Qwen3 (reuses our Qwen3.swift backbone + the Klein 3-layer
// hidden-state extraction). Loads pre-quantized (int8/int4/mixed) or bf16.

public struct GeneratedImage {
    public let rgb: [UInt8]   // row-major RGB8, H*W*3
    public let width: Int
    public let height: Int
}

public final class FluxImagePipeline {

    public enum FluxError: Error, CustomStringConvertible {
        case notImplemented(String)
        case missing(String)
        public var description: String {
            switch self {
            case .notImplemented(let m): return "FluxImagePipeline not implemented: \(m)"
            case .missing(let m): return "FluxImagePipeline missing: \(m)"
            }
        }
    }

    private let transformer: Flux2Transformer2DModel
    private let vae: AutoencoderKLFlux2
    private let encoder: Qwen3Model
    private let tokenizer: Tokenizer

    private var cachedPrompt: String? = nil
    private var cachedEmb: MLXArray? = nil

    // Klein 4B constants (flux-2-swift-mlx KleinConfig).
    private static let maxLen = 512
    private static let layers = [9, 18, 27]
    private static let padTokenId: Int32 = 151643

    private init(transformer: Flux2Transformer2DModel, vae: AutoencoderKLFlux2,
                 encoder: Qwen3Model, tokenizer: Tokenizer) {
        self.transformer = transformer
        self.vae = vae
        self.encoder = encoder
        self.tokenizer = tokenizer
    }

    // MARK: - Loading

    public static func load(modelDirectory: String, modelName: String) throws -> FluxImagePipeline {
        let root = URL(fileURLWithPath: modelDirectory)
        func sub(_ name: String) throws -> String {
            let p = root.appendingPathComponent(name).path
            guard FileManager.default.fileExists(atPath: p) else {
                throw FluxError.missing("\(name)/ under \(modelDirectory)")
            }
            return p
        }

        // model_index.json decides pre-quantized (MLPerf: load only, no on-load
        // quantization) vs bf16(+optional on-load quant for local dev).
        var prequant = false
        var tBits = 4
        var eBits = 4
        if let d = try? Data(contentsOf: root.appendingPathComponent("model_index.json")),
           let idx = (try? JSONSerialization.jsonObject(with: d)) as? [String: Any] {
            prequant = (idx["prequantized"] as? Bool) ?? false
            tBits = (idx["transformer_bits"] as? Int) ?? 4
            eBits = (idx["encoder_bits"] as? Int) ?? 4
        }
        Flux2Debug.log("Loading Klein pipeline: prequantized=\(prequant) tBits=\(tBits) eBits=\(eBits)")

        // --- Transformer ---
        let transformer = Flux2Transformer2DModel(config: .klein4B, memoryOptimization: .moderate)
        if prequant {
            // Pre-quantized: map diffusers keys (incl .scales/.biases), quantize the
            // module structure for exactly the layers that carry scales, then load.
            var w: [String: MLXArray] = [:]
            for (k, v) in try Flux2WeightLoader.loadWeights(from: try sub("transformer")) {
                w[Flux2WeightLoader.mapTransformerKeySimple(k)] = v
            }
            quantize(model: transformer, groupSize: 64, bits: tBits) { path, _ in
                w["\(path).scales"] != nil
            }
            try transformer.update(parameters: ModuleParameters.unflattened(w), verify: [])
        } else {
            // Full-precision bf16: load as-is, no quantization.
            var tWeights = try Flux2WeightLoader.loadWeights(from: try sub("transformer"))
            try Flux2WeightLoader.applyTransformerWeights(&tWeights, to: transformer)
            tWeights.removeAll()
        }
        eval(transformer.parameters())
        MLX.GPU.clearCache()

        // --- VAE (always bf16) ---
        let vae = AutoencoderKLFlux2(config: .flux2Dev)
        let vWeights = try Flux2WeightLoader.loadWeights(from: try sub("vae"))
        try Flux2WeightLoader.applyVAEWeights(vWeights, to: vae)
        eval(vae.parameters())

        // --- Text encoder: our Qwen3 backbone ---
        let encDir = try sub("text_encoder")
        let cfgData = try Data(contentsOf: URL(fileURLWithPath: encDir).appendingPathComponent("config.json"))
        let encConfig = try JSONDecoder().decode(Qwen3Configuration.self, from: cfgData)
        let encoder = Qwen3Model(encConfig)
        let encW = remapEncoderKeys(try Flux2WeightLoader.loadWeights(from: encDir))
        if prequant {
            quantize(model: encoder, groupSize: 64, bits: eBits) { path, _ in
                encW["\(path).scales"] != nil
            }
            try encoder.update(parameters: ModuleParameters.unflattened(encW), verify: [])
        } else {
            // Full-precision bf16. verify: [] — these weights have no lm_head (unused).
            try encoder.update(parameters: ModuleParameters.unflattened(encW), verify: [])
        }
        eval(encoder.parameters())
        MLX.GPU.clearCache()

        let tokenizer = try loadTokenizer(folder: try sub("tokenizer"))

        return FluxImagePipeline(transformer: transformer, vae: vae,
                                 encoder: encoder, tokenizer: tokenizer)
    }

    /// HF text_encoder weights are stored without our `model.` wrapper prefix
    /// (`embed_tokens.*`, `layers.N.*`, `norm.*`). Map them onto our module tree.
    private static func remapEncoderKeys(_ w: [String: MLXArray]) -> [String: MLXArray] {
        var out: [String: MLXArray] = [:]
        for (k, v) in w {
            if k.hasPrefix("model.") || k.hasPrefix("lm_head") {
                out[k] = v
            } else {
                out["model." + k] = v
            }
        }
        return out
    }

    private static func loadTokenizer(folder: String) throws -> Tokenizer {
        let sem = DispatchSemaphore(value: 0)
        var result: Tokenizer?
        var failure: Error?
        Task {
            do { result = try await AutoTokenizer.from(modelFolder: URL(fileURLWithPath: folder)) }
            catch { failure = error }
            sem.signal()
        }
        sem.wait()
        if let failure { throw failure }
        guard let result else { throw FluxError.missing("tokenizer") }
        return result
    }

    // MARK: - Generation

    public func generate(prompt: String, negativePrompt: String, width: Int, height: Int,
                         steps: Int, guidance: Float, seed: Int64,
                         onStep: (Int, Int) -> Void) throws -> GeneratedImage {
        // 1. Text embeddings [1, 512, 7680] — encoded once per prompt and
        // reused across seeds in a multi-seed run.
        let textEmb: MLXArray
        if let cachedEmb, cachedPrompt == prompt {
            textEmb = cachedEmb
        } else {
            textEmb = encodeKlein(prompt: prompt)
            cachedEmb = textEmb
            cachedPrompt = prompt
        }

        // 2. Latents + packing
        if seed >= 0 { MLXRandom.seed(UInt64(truncatingIfNeeded: seed)) }
        let patchified = LatentUtils.generatePatchifiedLatents(
            batchSize: 1, height: height, width: width, latentChannels: 32,
            patchSize: 2, seed: seed >= 0 ? UInt64(truncatingIfNeeded: seed) : nil)
        var packed = LatentUtils.packPatchifiedToSequence(patchified)
        let imageSeqLen = packed.dim(1)

        // 3. Position IDs
        let ids = LatentUtils.combinePositionIDs(
            textLength: Self.maxLen, height: height, width: width, patchSize: 2)

        // 4. Scheduler (flow-match, distilled steps)
        let scheduler = FlowMatchEulerScheduler(numTrainTimesteps: 1000, shift: 1.0)
        scheduler.setTimesteps(numInferenceSteps: steps, imageSeqLen: imageSeqLen, strength: 1.0)

        // 5. Denoise loop (Klein is distilled: no classical CFG, guidance embeds off)
        let nSteps = scheduler.sigmas.count - 1
        for stepIdx in 0..<nSteps {
            let sigma = scheduler.sigmas[stepIdx]
            let t = MLXArray([sigma])
            let noisePred = transformer(
                hiddenStates: packed, encoderHiddenStates: textEmb, timestep: t,
                guidance: nil, imgIds: ids.imageIds, txtIds: ids.textIds)
            packed = scheduler.step(modelOutput: noisePred, timestep: sigma, sample: packed)
            eval(packed)
            onStep(stepIdx + 1, nSteps)
        }

        // 6. Decode: unpack -> batchnorm denorm -> unpatchify -> VAE -> RGB
        let unpacked = LatentUtils.unpackSequenceToPatchified(packed, height: height, width: width)
        let denorm = LatentUtils.denormalizeLatentsWithBatchNorm(
            unpacked, runningMean: vae.batchNormRunningMean, runningVar: vae.batchNormRunningVar)
        let latents = LatentUtils.unpatchifyLatents(denorm)
        let decoded = vae.decode(latents)   // [1,3,H,W] in [-1,1]
        let img = Self.toRGB8(decoded)
        MLX.GPU.clearCache()
        return img
    }

    // Klein embedding extraction (chat template -> tokenize -> right-pad 512 ->
    // hidden states after layers [9,18,27] -> concat on hidden dim).
    private func encodeKlein(prompt: String) -> MLXArray {
        let cleaned = prompt.replacingOccurrences(of: "[IMG]", with: "")
        let formatted = "<|im_start|>user\n\(cleaned)<|im_end|>\n<|im_start|>assistant\n<think>\n\n</think>\n\n"
        var ids = tokenizer.encode(text: formatted).map { Int32($0) }
        if ids.count > Self.maxLen { ids = Array(ids.prefix(Self.maxLen)) }
        let realLen = ids.count
        while ids.count < Self.maxLen { ids.append(Self.padTokenId) }

        let inputIds = MLXArray(ids).reshaped([1, Self.maxLen])
        let pos = MLXArray(Array(0..<Self.maxLen).map { Int32($0) })
        let attnMask = (pos .< Int32(realLen)).asType(.int32).reshaped([1, Self.maxLen])

        let states = encoder.encoderHiddenStates(inputIds, layerIndices: Self.layers,
                                                 attentionMask: attnMask)
        let ordered = Self.layers.map { states[$0]! }
        let emb = concatenated(ordered, axis: -1)
        eval(emb)
        return emb
    }

    // [1,3,H,W] in [-1,1] -> row-major RGB8 bytes.
    private static func toRGB8(_ tensor: MLXArray) -> GeneratedImage {
        let h = tensor.dim(2), w = tensor.dim(3)
        let denorm = (tensor + 1.0) * 127.5
        let clamped = clip(denorm, min: 0, max: 255)
        let hwc = clamped.squeezed(axis: 0).transposed(axes: [1, 2, 0]).asType(.uint8)
        eval(hwc)
        return GeneratedImage(rgb: hwc.asArray(UInt8.self), width: w, height: h)
    }
}
