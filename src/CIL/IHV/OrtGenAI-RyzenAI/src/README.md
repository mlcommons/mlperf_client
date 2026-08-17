# MLPerf Client Benchmark

## Runtime Requirements
- Install the latest [AMD NPU driver](https://download.amd.com/opendownload/RyzenAI/1.8.0b0/NPU_RAI_376_WHQL.zip)
- Install the latest [AMD GPU driver](https://www.amd.com/en/support/download/drivers.html)

## Best Performance Setup
To run LLMs in best performance mode, follow these steps:
- For best performance, reboot your computer and exit as many background tasks and applications as possible.
- Go to Windows → Settings → System → Power, and set the power mode to `Best Performance`.
- Set `turbo` performance mode for NPU (requires AC power). Open a command prompt as `Administrator`:
```
cd C:\Windows\System32\AMD
xrt-smi configure --pmode turbo
```

## Run benchmark
Configure system for best performance and launch the benchmark
```
mlperf-windows.exe -c phi4/AMD_ORTGenAI-RyzenAI_NPU.json
```

# MLPerf Client Model Creation Procedure

## MLPerf Client v1.8 Model Recipes

The following source quantized models were used:
- [llama-3.1-8B](https://huggingface.co/onnx-community/Meta-Llama-3.1-8B-Instruct-ONNX-DirectML-GenAI-INT4)
- [qwen3-8B](https://huggingface.co/amd/Qwen3-8B-awq-quant-onnx)
- [phi-4-mini](https://huggingface.co/amd/phi-4-mini-instruct-oga-dml)

(Instructions on how these models are quantized are in our public [Ryzen AI docs](https://ryzenai.docs.amd.com/en/latest/oga_model_prepare.html)).

Then, the onnx_utils Python utility installed with Ryzen AI will be used to process these source ONNX models and generate ONNX models for AMD NPU or AMD Hybrid NPU-GPU execution.

### NPU Model Recipes

llama3.1-8B

- `onnx_utils -v optimize --input-model "llama3/model.onnx" --output-model "llama3.1-8B-full/full.onnx" --force llm --prefill npu_fusion --token npu_fusion --model-type llama3-8b --max-seq-len 16384 --attributes enable_flashmha=true enable_flatmlp=true enable_flat_kv=true`

qwen3-8B

- `onnx_utils -v optimize --input-model "qwen3-8B/model.onnx" --output-model "qwen3-8b-full/full.onnx" --force llm --prefill npu_fusion --token npu_fusion --model-type qwen3-8b --max-seq-len 16384 --attributes enable_flashmha=true enable_flatmlp=true enable_flat_kv=true`

phi4-mini-instruct

- `onnx_utils -v optimize --input-model "phi4/model.onnx" --output-model "phi4-mini-full/full.onnx" --force llm --prefill npu_fusion --token npu_fusion --model-type phi-4 --max-seq-len 16384 --attributes enable_flashmha=true enable_flatmlp=true enable_flat_kv=true`


### Hybrid Model Recipes

llama-3.1-8B 

- `onnx_utils -v optimize --input-model llama3/model.onnx  --output-model llama3.1-8B-hybrid/eager.onnx --force llm --prefill npu_eager --token gpu_eager --model-type llama3-8b --attributes enable_chunk_flash_mha=true`

qwen3-8B

- `onnx_utils -v optimize --input-model Qwen3-8B-awq-quant-onnx/model.onnx  --output-model qwen-3b-hybrid/eager.onnx --force llm --prefill npu_eager --token gpu_eager --model-type qwen3-8b --attributes enable_chunk_flash_mha=true`

phi-4-mini-instruct

- `onnx_utils -v optimize --input-model phi4/model.onnx  --output-model phi-4-mini-hybrid/eager.onnx --force llm --prefill npu_eager --token gpu_eager --model-type phi-4 --max-seq-len 8192 --attributes enable_chunk_flash_mha=true`

#### Notes

By default, the Hybrid models are optimized for lower memory. For max performance (with memory increase) set this option in `provider_options` in genai_config.json: `"hybrid_opt_npu_read_ahead": "-1"`

Hybrid models are sensitive to warmup prompt sizes. If a small warmup prompt is used before large active prompts, performance will be slightly lower. Set `"hybrid_opt_init_prompt_size": "<value>"` in `provider_options` in genai_config.json to control this. Use a `<value>` of `4096` for Llama-3.1-8B and Qwen3-8B and use `8192` for Phi-4-mini-instruct.

For reference, the Ryzen AI runtime used by the MLPerf client comes from [Ryzen AI 1.8-Beta](https://ryzenai.docs.amd.com/en/latest/app_development.html?foo=bar#application-packaging-requirements) via the [installer](https://download.amd.com/opendownload/RyzenAI/1.8.0b0/ryzen-ai-lt-1.8.0-beta.exe)

# Troubleshooting

Errors like `google.protobuf.message.EncodeError: Failed to serialize proto` are indicative of out of memory. Try freeing up memory before trying again.

By default, Microsoft Windows allocates 50% of system memory to AMD NPU/GPU: if the system has 32GB, only 16GB is made available to the NPU/GPU. To increase this allocation, follow the [regedit instructions](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/dxgkrnl-configuration) and edit `MemoryManager\SystemPartitionCommitLimitPercentage`.

For more information, visit our public [Ryzen AI docs](https://ryzenai.docs.amd.com/en/latest/hybrid_oga.html). 
