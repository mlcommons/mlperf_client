# Llama2 Models

## I. Llama2 Model Configuration Parameters

This section provides an overview of the Llama2 model configuration parameters (check the `model_config` in the `generation-test-greedy.json` as a sample). The parameters control both the architecture of the model and the decoding strategy for generating sequences of tokens.

### Model Parameters

The following parameters define the architecture, tokens, and input/output mappings for the model.

- **bos_token_id**:  
  The ID of the "Beginning Of Sequence" token, marking the start of a sequence.

- **context_length**:  
  The maximum number of input tokens (prompt) that the model can process at once.

- **decoder**:  
  Configuration for the model's decoder, which includes attention heads, hidden layers, and input/output mappings.
  
  - **head_size**:  
    The size of each attention head in the decoder.
  
  - **hidden_size**:  
    The number of neurons in each hidden layer of the model. This defines the **width** of the model layers. For example, if `hidden_size = 4096`, each hidden layer will have 4096 neurons.
  
  - **inputs**:  
    The input features required by the model, such as token IDs, attention masks, and positional encodings that are passed to the model for processing.
  
  - **outputs**:  
    The output features produced by the model, including the predicted token probabilities (logits) and attention states that are used for generating sequences.

  - **num_attention_heads**:  
    The number of attention heads in the decoder.

  - **num_hidden_layers**:  
    The number of hidden layers in the model, defining the **depth** of the model. For example, if `num_hidden_layers = 32`, the model has 32 hidden layers.

  - **num_key_value_heads**:  
    The number of key-value heads used in the attention mechanism.

- **eos_token_id**:  
  The ID of the "End Of Sequence" token, marking the end of a sequence.

- **pad_token_id**:  
  The ID of the padding token, used to pad sequences to the same length in a batch.

- **vocab_size**:  
  The total number of unique tokens in the model's vocabulary.
  
### Search Parameters

The following parameters control how the model generates a sequence of output tokens based on the input. They apply to different decoding strategies (Greedy, Top-K, Top-P, Beam Search) and affect the randomness, length, and stopping points of the generated sequence.

- **method**:  
  Determines the decoding strategy for generating tokens.  
  Possible values:  
  - `"greedy"`: Selects the token with the highest probability at each step.  
  - `"top_k"`: Applies Top-K Sampling, randomly selecting from the top `k` most probable tokens.  
  - `"top_p"`: Uses Nucleus (Top-P) Sampling, where a random probability threshold is chosen up to `p`. The model iterates over the tokens in descending order of probability and selects the first token where the probability exceeds the threshold.
  - `"beam_search"`: Uses Beam Search, exploring multiple candidate sequences and selecting the one with the highest overall probability.
  
- **num_beams**:  
  Used when `method` is set to `"beam_search"`.  
  Specifies how many candidate sequences (beams) to explore at each decoding step.
  
- **temperature**:  
  Controls randomness by scaling logits before applying softmax. Lower values make the output more deterministic, while higher values increase randomness.  
  Applicable to all methods, except `"greedy"`.

- **top_k**:  
  Used when `method` is set to `"top_k"`.  
  Specifies how many of the top probable tokens to consider when sampling.

- **top_p**:  
  Used when `method` is set to `"top_p"`.  
  Specifies the probability threshold for Nucleus Sampling.

- **stop_on_eos**:  
  Stops generation when the "End Of Sequence" token is predicted.  
  Applicable to all methods.
  
- **max_length**:  
  Defines the maximum number of tokens that the model is allowed to generate in a single output sequence. It restricts the length of the generated text, preventing the model from producing sequences that are too long.  
  Applicable to all methods.

- **max_total_length**:  
  Specifies the maximum combined length of both the input tokens (prompt) and the generated tokens in a sequence. The sum of the input and the generated output cannot exceed this limit.  
  Applicable to all methods.