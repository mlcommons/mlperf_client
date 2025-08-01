// Copyright © 2024 Apple Inc.

import Foundation
// import Hub
import MLX
import MLXNN
// import Tokenizers

/// Representation of ``LanguageModel`` input.
///
/// This can contain text (tokens), prepared images (`MLXArray`), or other media as
/// needed.  ``LMInput`` is produced by ``UserInputProcessor`` in response
/// to ``UserInput``.
///
/// The ``ModelContext`` holds the ``UserInputProcessor`` associated with a
/// ``LanguageModel``.
public struct LMInput {
    public let text: Text

    /// Representation of tokenized input text.
    public struct Text {

        /// input token array
        public let tokens: MLXArray

        /// optional mask array
        public let mask: MLXArray?

        public init(tokens: MLXArray, mask: MLXArray? = nil) {
            self.tokens = tokens
            self.mask = mask
        }

        public subscript(
            indices: MLXArrayIndex..., stream stream: StreamOrDevice = .default
        ) -> Text {
            Text(tokens: tokens[indices, stream: stream], mask: mask?[indices, stream: stream])
        }

        public subscript(
            text indices: MLXArrayIndex..., stream stream: StreamOrDevice = .default
        ) -> Text {
            Text(tokens: tokens[indices, stream: stream], mask: mask)
        }
    }

    public init(tokens: MLXArray, mask: MLXArray? = nil) {
        self.init(text: .init(tokens: tokens, mask: mask))
    }

    public init(text: LMInput.Text) {
        self.text = text
    }
}

/// ``LanguageModel`` step output.  This is consumed internally
/// by the ``TokenIterator``.
public struct LMOutput {

    /// logits (one hot vector of probabilities for tokens)
    public let logits: MLXArray

    /// optional ``State`` to carry forward into the next step
    public let state: State?

    public struct State {
        public let crossAttentionStates: MLXArray?

        public init(crossAttentionStates: MLXArray? = nil) {
            self.crossAttentionStates = crossAttentionStates
        }
    }

    public init(logits: MLXArray, state: LMOutput.State? = nil) {
        self.logits = logits
        self.state = state
    }
}

// /// The result of the call to ``LanguageModel/prepare(_:cache:windowSize:)``
// public enum PrepareResult {
//     /// tokens to process by the ``TokenIterator``
//     case tokens(LMInput.Text)

//     /// logits representing the next token
//     case logits(LMOutput)
// }

/// Interface for all Language Models (e.g. LLM, VLM).
///
/// The language model is typically called by the ``TokenIterator`` and it:
///
/// - consumes the ``LMInput``
/// - calls ``prepare(_:cache:windowSize:)`` to initialize the KVCache and consume the prompt
/// - calls ``callAsFunction(_:cache:state:)-9kuvf`` for each token, producing an ``LMOutput``
/// - the ``TokenIterator`` accumulates this information into a ``GenerateResult``
public protocol LanguageModel: Module {

    /// Prepare the cache state and consume the ``LMInput``.
    ///
    /// This can return:
    /// - ``PrepareResult/tokens(_:)`` if the caller should evaluate the (remaining) tokens normally
    /// - ``PrepareResult/logits(_:)`` to produce the next token from the prompt
    // func prepare(_ input: LMInput, cache: [KVCache], windowSize: Int?) throws -> PrepareResult
    // func prepare(_ input: LMInput, windowSize: Int?) throws -> PrepareResult
    func prepare(_ input: MLXArray, cache: [KVCache], windowSize: Int?) throws -> MLXArray

    // /// Primary entry point to produce a step (single token) from the model
    // func callAsFunction(_ input: LMInput.Text, cache: [KVCache]?, state: LMOutput.State?)
    //     -> LMOutput

    /// Models may implement this simplified interface if they do not produce any ``LMOutput/State``
    func callAsFunction(_ inputs: MLXArray, cache: [KVCache]?) -> MLXArray

    /// create a new array of ``KVCache`` -- automatic implementation if self
    /// implements ``KVCacheDimensionProvider``
    func newCache() -> [KVCache]
}

extension LanguageModel {
    public func callAsFunction(_ inputs: MLXArray, cache: [KVCache]?) -> MLXArray {
    // public func callAsFunction(_ inputs: MLXArray) -> MLXArray {
        fatalError("callAsFunction(inputs:cache:) not implemented for \(Self.self)")
    }
}

extension LanguageModel {
    public func sanitize(weights: [String: MLXArray]) -> [String: MLXArray] {
        weights
    }
}

// Optional protocol that can be implemented by ``LanguageModel`` and will
// provide an automatic implementation of ``LanguageModel/newCache(parameters:)``
public protocol KVCacheDimensionProvider {
    var kvHeads: [Int] { get }
}

extension LanguageModel where Self: KVCacheDimensionProvider {
    public func newCache() -> [KVCache] {
        kvHeads.map { n in
            KVCacheSimple()
        }
    }
}

/// Base ``LanguageModel`` configuration -- provides `modelType`
/// and `quantization` (used in loading the model).
///
/// This is used by ``ModelFactory/load(hub:configuration:progressHandler:)``
/// to determine the type of model to load.
public struct BaseConfiguration: Codable, Sendable {
    public let modelType: String

    public struct Quantization: Codable, Sendable {
        public init(groupSize: Int, bits: Int) {
            self.groupSize = groupSize
            self.bits = bits
        }

        public let groupSize: Int
        public let bits: Int

        enum CodingKeys: String, CodingKey {
            case groupSize = "group_size"
            case bits = "bits"
        }
    }

    public var quantization: Quantization?

    enum CodingKeys: String, CodingKey {
        case modelType = "model_type"
        case quantization
    }
}