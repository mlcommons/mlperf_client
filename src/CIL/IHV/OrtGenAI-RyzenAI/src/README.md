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
mlperf-windows.exe -c phi3.5/AMD_ORTGenAI-RyzenAI_NPU.json
```

# MLPerf Client v1.6 Runtime Assets
The following files are required from Ryzen AI 1.7. Filenames and `sha256` checksums are listed.
```
b6499663171916a0e298df529541d52bebd029023823bafed75c2531daf97b7b  dyn_bins.dll
05d33d6f60904cc41d697f079c1cb44a552fe044053ba8efd65531830df80197  dyn_dispatch_core.dll
ba9e5ecc050f3b7a1bdb18e18aa5dc225d1267745f6330716c9c2388c16ea789  onnxruntime-genai.dll
4d431431f571d585f9f30bfc43317654349296e971405aad91d8104a37cfdc9e  onnxruntime.dll
c91e871c516eb4e5193df92970cb9ed92782fcfaac66469d43f3006a31c0b88a  onnxruntime_providers_ryzenai.dll
0f60f69b93477a851dfcea652eb2e411d253e900791f74af023f75898fc6b96f  onnxruntime_providers_shared.dll
c7f1a1a61550326b89b7f8730711e6fef7715ed9ad2fd8b8b091e4dc72113b04  ryzen_mm.dll
264036aea88b5d5327f03b5d1c766a0a60572130baf53b081b150218df840402  zlib.dll
```
- Install Ryzen AI 1.7 [RAI-1.7 Installer](https://account.amd.com/en/forms/downloads/ryzenai-eula-public-xef.html?filename=ryzen-ai-lt-1.7.0.exe)
- Download and extract this patch [RAI-1.7.0.p1](https://download.amd.com/opendownload/RyzenAI/rai-deployment-dlls-1.7.0.p1.zip) in `C:/Program Files/RyzenAI/1.7.0/deployment`. 

# MLPerf Client Model Creation Procedure

## MLPerf Client v1.6 Model Recipes

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
