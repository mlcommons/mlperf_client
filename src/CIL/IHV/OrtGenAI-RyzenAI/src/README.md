# MLPerf Client Benchmark

## Runtime Requirements
- Install the latest AMD NPU driver: https://ryzenai.docs.amd.com/en/latest/inst.html#install-npu-drivers
- Install the latest AMD GPU driver: https://www.amd.com/en/support/download/drivers.html

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
mlperf-windows.exe -c phi3.5/AMD_ORTGenAI-RyzenAI_NPU-GPU.json
```

## Troubleshooting: AMD Strix Systems with 32 GB Memory

AMD Strix systems with 32 GB memory are capable of running the Hybrid NPU + GPU benchmark. This may require a step to allocate additional system memory to the GPU.

**Step: Increase GPU memory commit limit via the Windows Registry**

1. Open **Registry Editor** (`regedit`).
2. Navigate to:  
   `HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\GraphicsDrivers`
3. Right-click `GraphicsDrivers` → **New** > **Key**  
   Name it: `MemoryManager`
4. Select the newly created `MemoryManager` key.
5. In the right pane, right-click → **New** > **DWORD (32-bit) Value**  
   Name it: `SystemPartitionCommitLimitPercentage`
6. Double-click the new value and set it to:  
   **Base:** Decimal  
   **Value:** `60`
7. Close Registry Editor.
8. **Reboot** your system.
9. Run MLPerf Client.

> 💡 This change allocates an additional 10% of system commit limit to GPU resources, ensuring successful benchmark execution.

# MLPerf Client Model Creation Procedure
## Prefill NPU Fusion + Token GPU Eager Model Perpartion

Download these two `.whl` files
- Download https://client.mlcommons-storage.org/deps/1.0/scenario_files/llm/recipes/OrtGenAI-RyzenAI/ryzenai_dynamic_dispatch-1.1.0.dev1-cp310-cp310-win_amd64.whl
- Download https://client.mlcommons-storage.org/deps/1.0/scenario_files/llm/recipes/OrtGenAI-RyzenAI/ryzenai_onnx_utils-0.9.0.dev1-py3-none-any.whl

**Use Powershell for the following procedure.**

Start with Quark-quantized DML FP16 ONNX target model and clone locally:

- [Llama 2 7B Chat](https://huggingface.co/amd/Llama-2-7b-chat-hf-awq-g128-int4-asym-fp16-onnx-dml/tree/main)
- [Phi 3.5 Mini Instruct](https://huggingface.co/amd/Phi-3.5-mini-instruct-awq-g128-int4-asym-fp16-onnx-dml)
- [Llama 3.1 8B](https://huggingface.co/amd/llama-3.1-8B-Instruct-awq-dml)

(Instructions on how these models are quantized are in our public Ryzen AI docs: https://ryzenai.docs.amd.com/en/latest/oga_model_prepare.html)

### Clone model from HuggingFace
```shell
git clone https://huggingface.co/amd/Phi-3.5-mini-instruct-awq-g128-int4-asym-fp16-onnx-dml
cd Phi-3.5-mini-instruct-awq-g128-int4-asym-fp16-onnx-dml
```

### Create conda environment
```shell
conda create -n "model-creation" python=3.10
conda activate model-creation
```

### Install dependencies
```shell
pip install ryzenai_dynamic_dispatch-1.1.0.dev1-cp310-cp310-win_amd64.whl
pip install ryzenai_onnx_utils-0.9.0.dev1-py3-none-any.whl
pip install onnx==1.17.0
pip install onnxruntime
conda install libprotobuf=6.31.1=h8944e3b_0 spdlog==1.15.0
```

### Set model shape
```shell
onnx_utils preprocess ./model.onnx ./model-4096-prompt-dynamic.onnx # ignore "No shape inference script found for GreaterOrEqual: /model/pos_ids_reformat/GreaterOrEqual"
```

- When prompted `Enter fixed value(s) for graphs:` type `1`
- When prompted `Enter fixed value(s) for batch_size:` type `1`
- When prompted `Enter fixed value(s) for sequence_length:` press `enter`
- When prompted `Enter fixed value(s) for total_sequence_length:` type `4096`
- When prompted  Enter fixed value(s) for past_sequence_length:` type `4096`

Output should look like this
```shell
 Enter fixed value(s) for graphs: 1
 Enter fixed value(s) for batch_size: 1
 Enter fixed value(s) for sequence_length: [just press enter]
 Enter fixed value(s) for total_sequence_length: 4096
 Enter fixed value(s) for past_sequence_length: 4096
```

### Partition reference model
```shell
onnx_utils partition ./model.onnx ./tmp hybrid_llm_experimental.yaml -v --force  --model-name reference --save-as-external --attributes npu_jit=true
```

### Partition prefill model
For llama2 and phi:

```shell
onnx_utils partition "./model-4096-prompt-dynamic.onnx" ./tmp hybrid_llm_prefill_fusion.yaml -v --force  --model-name prefill_fusion --save-as-external --attributes convert_kv_cache=false
```

For llama3:

```shell
onnx_utils partition "./model-4096-prompt-dynamic.onnx" ./tmp hybrid_llm_prefill_fusion_llama3.yaml -v --force  --model-name prefill_fusion --save-as-external --attributes convert_kv_cache=false
```

### Prepare final model
For llama2 and phi:

```shell
cd tmp
onnx_utils postprocess combine_llm --input-path "./reference.onnx" "./prefill_fusion.onnx" "./reference.onnx" --output-path ./model-amd.onnx
```

For llama3:

```shell
cd ./tmp
onnx_utils postprocess combine_llm_llama3 --input-path "./reference.onnx" "./prefill_fusion.onnx" "./reference.onnx" --output-path ./model-amd.onnx
```

## Troubleshooting

Errors like `google.protobuf.message.EncodeError: Failed to serialize proto` are indicative of out of memory. Try freeing up memory before trying again.

For more information, visit our public Ryzen AI docs: https://ryzenai.docs.amd.com/en/latest/hybrid_oga.html
