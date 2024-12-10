import onnxruntime
import numpy as np
from sentencepiece import SentencePieceProcessor
from typing import List
import os
import argparse


class Tokenizer:
    def __init__(self, model_path: str):
        # reload tokenizer
        assert os.path.isfile(model_path), model_path
        self.sp_model = SentencePieceProcessor(model_file=model_path)

        # BOS / EOS token IDs
        self.n_words: int = self.sp_model.vocab_size()
        self.bos_id: int = self.sp_model.bos_id()
        self.eos_id: int = self.sp_model.eos_id()
        self.pad_id: int = self.sp_model.pad_id()

        assert self.sp_model.vocab_size() == self.sp_model.get_piece_size()

    def encode(self, s: str, bos: bool, eos: bool) -> List[int]:
        assert type(s) is str
        t = self.sp_model.encode(s)
        if bos:
            t = [self.bos_id] + t
        if eos:
            t = t + [self.eos_id]
        return t

    def decode(self, t: List[int]) -> str:
        return self.sp_model.decode(t)

def run_onnx_llamav2(
    prompt: str,
    onnx_file: str,
    tokenizer_path: str,
    max_gen_len: int = 256,
) -> str:
    # Create the ONNX session
    options = onnxruntime.SessionOptions()
    llm_session = onnxruntime.InferenceSession(
        onnx_file,
        sess_options=options,
        providers=[
            "CPUExecutionProvider",
        ],
    )

    # get the data type used by the model
    data_type_str = llm_session.get_inputs()[0].type
    print(data_type_str)
    if data_type_str == "tensor(float16)":
        data_type = np.float16
    elif data_type_str == "tensor(float32)" or data_type_str == "tensor(float)":
        data_type = np.float32
    elif data_type_str == "tensor(int64)":
        data_type = np.int64
    else:
        raise Exception(f"Unknown data type {data_type_str}")

    # Get the relevant shapes so we can create the inputs
    for inputs_meta in llm_session._inputs_meta:
        if inputs_meta.name == "attention_mask":
            attn_mask_shape = inputs_meta.shape

    # Initialize the tokenizer and produce the initial tokens.
    tokenizer = Tokenizer(model_path=tokenizer_path)
    tokens = tokenizer.encode(prompt, bos=True, eos=False)

    # Input sizes:
    # input_ids: int64[batch_size,sequence_length]
    # attention_mask: int64[batch_size,total_sequence_length]
    # position_ids: int64[batch_size,sequence_length]
    # past_key_values.%d.key: float32[batch_size,32,past_sequence_length,128]
    # past_key_values.%d.value: float32[batch_size,32,past_sequence_length,128]

    # Create the input ids
    x = np.array(tokens)
    x = np.expand_dims(x, axis=0).astype(data_type)

    # Create the attention mask.
    attn_mask_shape = (1, x.shape[1])
    attn_mask = np.ones(attn_mask_shape, dtype=data_type)

    # Create the position ids.
    position_ids = np.arange(x.shape[1])
    position_ids = np.expand_dims(position_ids, axis=0).astype(np.int64)

    # Create the K and V caches.
    num_kv = 32
    k_values = {}
    v_values = {}
    for i in range(num_kv):
        k = np.zeros([1, 32, 0, 128], dtype=np.float32)
        v = np.zeros([1, 32, 0, 128], dtype=np.float32)
        k_key = f"past_key_values.{i}.key"
        v_key = f"past_key_values.{i}.value"
        k_values[k_key] = k
        v_values[v_key] = v

    # Iteratively generate tokens.
    output_tokens = []
    for idx in range(max_gen_len):
        inputs_map = {
            "input_ids": x,
            "attention_mask": attn_mask,
            "position_ids": position_ids,
        }
        inputs_map.update(k_values)
        inputs_map.update(v_values)
        results = llm_session.run(
            None,
            inputs_map,
        )

        # Get the output dictionary
        logits = results[0] #  float32[batch_size,sequence_length,32000]
        k_values = {}
        v_values = {}
        for i in range(num_kv):
            k_key = f"past_key_values.{i}.key"
            v_key = f"past_key_values.{i}.value"
            k_values[k_key] = results[i * 2 + 1]
            v_values[v_key] = results[i * 2 + 2]

        # Decide the next token using your preferred sampling strategy.
        next_token = np.argmax(logits[0, -1, :])
        print("next token:", next_token)
        output_tokens.append(int(next_token))

        # Stop if the model generated the EOS token.
        if next_token == tokenizer.eos_id:
            break

        # Update the input ids with the new token.
        x = np.array([next_token])
        x = np.expand_dims(x, axis=0).astype(data_type)

        # Update the position ids.
        position_ids = np.array([position_ids[0][-1] + 1])
        position_ids = np.expand_dims(position_ids, axis=0).astype(np.int64)

        # Update the attention mask.
        attn_mask = np.ones((1, attn_mask.shape[1] + 1), dtype=data_type)

    # Decode the output tokens.
    print(output_tokens)
    output_str = tokenizer.decode(output_tokens)

    return output_str


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--prompt",
        type=str,
        required=True,
    )
    parser.add_argument(
        "--onnx_file",
        type=str,
        required=True,
    )
    parser.add_argument(
        "--tokenizer_path",
        type=str,
        required=True,
    )
    parser.add_argument("--max_gen_len", type=int, default=256)
    args = parser.parse_args()
    response = run_onnx_llamav2(
        args.prompt,
        args.onnx_file,
        args.tokenizer_path,
        args.max_gen_len,
    )

    print(response)
