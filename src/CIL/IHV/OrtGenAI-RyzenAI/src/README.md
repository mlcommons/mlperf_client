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

# MLPerf Client v1.5 Runtime Assets
The following files are required from RyzenAI 1.6. Filenames and `sha256` checksums are listed.
```
3d1cd18db9bbab6a896b5ba0aa69211358bb2f2e38c8ae356d67c906f1288283  abseil_dll.dll
8a23d826b25b4329522ff451cb52b7f2b34d7f2913cfeb878371ce8bd765fe2d  D3D12Core.dll
9c9e6d822561c6c41b90e6994b3e8857cf1d66dbfb1e0c4c799c7c89b4e92da1  DirectML.dll
ac97fe437f975ffbf0bb56c7f9548f7e8b46be2b96922795cb9943de6b488b1d  libutf8_validity.dll
1ff7685ac546c147242b1870bb47fea7eacb40186762091ba9a3a4cdeebdc69f  onnx_custom_ops.dll
aa60cc1975874ae611366c59946548d4fd3db61cbbc9ce34bc267292ced234e5  onnxruntime.dll
0bd17dba46cec160d95e4b5dd136357a61039b73aeef2eeafc514fe3f846af51  onnxruntime-genai.dll
22a7c390af6bebafefab603f67c00cf8c04eb4078c0fd44f1b1fa6f4611d8c23  onnxruntime_providers_shared.dll
3e514c97e3705cf38a14ed72d2158e49dbc2e6ae315555961df33fd1e9c49daf  ryzenai_onnx_utils.dll
e1103986ea2f4240fb40b6b0ed4906d03f1d1e1d0f52ea11a287eddeee910ade  ryzen_mm.dll
91c732f665e9cc53b76a5c1207ef8c9be1a2c812198adcdc7fb642e5072bc6ff  zlib.dll
```
- Install Ryzen AI 1.6 [RAI-1.6 Installer](https://account.amd.com/en/forms/downloads/ryzen-ai-software-platform-xef.html?filename=ryzen-ai-lt-1.6.0-GA.exe)
- DLLs will be in `C:/Program Files/RyzenAI/1.6.0/deployment`. 
- replace `onnx_custom_ops.dll` with the file in this patch [RAI-1.6-patch](https://account.amd.com/en/forms/downloads/ryzen-ai-software-platform-xef.html?filename=ryzenai-1.6.0-patch.zip)

# MLPerf Client Model Creation Procedure

## MLPerf Client v1.5 Model Recipes

### NPU Model Recipes and Llama 3.1 8B Hybrid Recipe
First install prerequisites
```
# Model generate Wheel
pip install model_generate-1.5.0-py3-none-any.whl
pip install sentencepiece
pip install torch
pip install numpy==1.26.4
pip install onnx==1.18.0
pip install onnx-ir
```

Generate model for NPU (Phi3.5 or Llama 3.1)
```
model_generate --npu <output_fusion_model> .\<quark_model_path> --optimize decode
```

Generate model for Hybrid (Llama 3.1)
```
model_generate --hybrid <output_hybrid_v2_model> .\<quark_or_dml_model path> --mode bfp16
```

### Phi 3.5 Hybrid Recipe -- Prefill NPU Fusion + Token GPU Eager Model Perpartion

Download these two `.whl` files
- Download https://client.mlcommons-storage.org/deps/1.0/scenario_files/llm/recipes/OrtGenAI-RyzenAI/ryzenai_dynamic_dispatch-1.1.0.dev1-cp310-cp310-win_amd64.whl
- Download https://client.mlcommons-storage.org/deps/1.0/scenario_files/llm/recipes/OrtGenAI-RyzenAI/ryzenai_onnx_utils-0.9.0.dev1-py3-none-any.whl

**Use Powershell for the following procedure.**

Start with Quark-quantized DML FP16 ONNX target model and clone locally:
- [Phi 3.5 Mini Instruct](https://huggingface.co/amd/Phi-3.5-mini-instruct-awq-g128-int4-asym-fp16-onnx-dml)

(Instructions on how these models are quantized are in our public Ryzen AI docs: https://ryzenai.docs.amd.com/en/latest/oga_model_prepare.html)

Clone model from HuggingFace
```shell
git clone https://huggingface.co/amd/Phi-3.5-mini-instruct-awq-g128-int4-asym-fp16-onnx-dml
cd Phi-3.5-mini-instruct-awq-g128-int4-asym-fp16-onnx-dml
```

Create conda environment
```shell
conda create -n "model-creation" python=3.10
conda activate model-creation
```

Install dependencies
```shell
pip install ryzenai_dynamic_dispatch-1.1.0.dev1-cp310-cp310-win_amd64.whl
pip install ryzenai_onnx_utils-0.9.0.dev1-py3-none-any.whl
pip install onnx==1.17.0
pip install onnxruntime
conda install libprotobuf=6.31.1=h8944e3b_0 spdlog==1.15.0
```

Set model shape
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

Partition reference model
```shell
onnx_utils partition ./model.onnx ./tmp hybrid_llm_experimental.yaml -v --force  --model-name reference --save-as-external --attributes npu_jit=true
```

Partition prefill model
```shell
onnx_utils partition "./model-4096-prompt-dynamic.onnx" ./tmp hybrid_llm_prefill_fusion.yaml -v --force  --model-name prefill_fusion --save-as-external --attributes convert_kv_cache=false
```

Prepare final model
```shell
cd tmp
onnx_utils postprocess combine_llm --input-path "./reference.onnx" "./prefill_fusion.onnx" "./reference.onnx" --output-path ./model-amd.onnx
```

# Troubleshooting

Errors like `google.protobuf.message.EncodeError: Failed to serialize proto` are indicative of out of memory. Try freeing up memory before trying again.

For more information, visit our public Ryzen AI docs: https://ryzenai.docs.amd.com/en/latest/hybrid_oga.html
