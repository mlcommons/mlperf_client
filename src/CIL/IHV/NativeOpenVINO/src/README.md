## Usage

```bash
# Baseline
mlperf-windows.exe -c Llama3.1/Intel_NativeOpenVINO_GPU_Default.json
mlperf-windows.exe -c Llama3.1/Intel_NativeOpenVINO_NPU_Default.json

mlperf-windows.exe -c phi4mini/Intel_NativeOpenVINO_GPU_Default.json
mlperf-windows.exe -c phi4mini/Intel_NativeOpenVINO_NPU_Default.json

mlperf-windows.exe -c Llama3.1/Intel_NativeOpenVINO_GPU_Agentic-Default.json
mlperf-windows.exe -c Llama3.1/Intel_NativeOpenVINO_NPU_Agentic-Default.json

mlperf-windows.exe -c phi4mini/Intel_NativeOpenVINO_GPU_Agentic-Default.json
mlperf-windows.exe -c phi4mini/Intel_NativeOpenVINO_NPU_Agentic-Default.json


# Extended
mlperf-windows.exe -c extended/qwen3/Intel_NativeOpenVINO_GPU_Default.json
mlperf-windows.exe -c extended/qwen3/Intel_NativeOpenVINO_NPU_Default.json

mlperf-windows.exe -c extended/phi4reason/Intel_NativeOpenVINO_GPU_Default.json
mlperf-windows.exe -c extended/phi4reason/Intel_NativeOpenVINO_NPU_Default.json

mlperf-windows.exe -c extended/qwen3/Intel_NativeOpenVINO_GPU_Agentic_Default.json
mlperf-windows.exe -c extended/qwen3/Intel_NativeOpenVINO_NPU_Agentic_Default.json
```

An example config with files downloaded from web is available at `data/configs/Llama3.1/Intel_NativeOpenVINO_GPU_Default.json`. If `LibraryPath` is not set within the config file, it allows the app to automatically download dependencies and unpack the builtin `IHV_NativeOpenVINO.dll`.

## Model Building

There are three steps to create the models

### 1. Create the model using optimum-cli

Optimum Intel provides a simple interface to optimize Transformer and Diffuser models, convert them to the OpenVINO Intermediate Representation (IR) format to run inference using OpenVINO Runtime.

#### 1.1. Installation

To install latest Optimum Intel release with required dependencies one can use `pip` as follows:

Download OpenVINO 2026.2.1: https://storage.openvinotoolkit.org/repositories/openvino_genai/packages/2026.2.1/
 - Windows - windows/openvino_genai_windows_2026.2.1.0_x86_64.zip
 - Linux - linux/openvino_genai_ubuntu24_2026.2.1.0_x86_64.tar.gz

```bash
python --version
# Python 3.12.10
python -m venv .venv_phi4mini-ov26.2
.venv_phi4mini-ov26.2\Scripts\activate
pip install --upgrade pip setuptools wheel
pip install numpy==2.2.0 pandas==2.2.3 fsspec==2024.6.1 scipy tqdm
pip install torch==2.8.0+cpu torchvision==0.23.0+cpu --index-url https://download.pytorch.org/whl/cpu
pip install transformers==4.52.3 accelerate==1.13.0 transformers-stream-generator==0.0.5 
pip install datasets==2.21.0 --no-deps
pip install openvino==2026.2.1 openvino-genai==2026.2.1.0 openvino-tokenizers==2026.2.1.0 openvino-telemetry==2025.2.0
pip install optimum==2.1.0 optimum-intel==1.27.0 optimum-onnx==0.1.0 olive-ai==0.13.0 torchmetrics==1.9.0 --no-deps
pip uninstall -y diffusers peft

python -c "import transformers, datasets, fsspec, numpy, pandas; print(transformers.__version__, datasets.__version__, fsspec.__version__, numpy.__version__, pandas.__version__)"
pip freeze | grep -e transformers -e datasets -e fsspec -e numpy -e pandas -e optimum -e openvino -e olive -e torch
datasets==2.21.0
fsspec==2026.4.0
numpy==2.4.6
olive-ai==0.13.0
openvino==2026.2.1
openvino-genai==2026.2.1.0
openvino-tokenizers==2026.2.1.0
optimum==2.1.0
optimum-intel==1.27.0
optimum-onnx==0.1.0
pandas==2.2.3
torch==2.11.0
transformers==4.52.3
```

#### 1.2. Convert model to OpenVINO IR format

Create the model from Hugging Face using following commands:

##### NPU models:
```bash
optimum-cli export openvino -m meta-llama/Llama-3.1-8B-Instruct --weight-format int4 --sym --group-size -1 --ratio 1 --all-layers --awq --scale-estimation --dataset=wikitext2 models_NativeOpenVINO/Llama-3.1-8B-Instruct_ov-int4-CHw
optimum-cli export openvino -m microsoft/Phi-4-mini-instruct --weight-format int4 --sym --group-size -1 --ratio 1 --awq --scale-estimation --dataset=wikitext2 --trust-remote-code --backup-precision int8_sym models_NativeOpenVINO/Phi-4-mini-instruct_ov-int4-CHw

```

##### GPU models:
```bash
optimum-cli export openvino -m meta-llama/Llama-3.1-8B-Instruct --weight-format int4 --sym --group-size -1 --ratio 0.99 --backup-precision int8_sym --all-layers --awq --scale-estimation --dataset=wikitext2 --trust-remote-code  --sensitivity-metric=weight_quantization_error models_NativeOpenVINO/Llama-3.1-8B-Instruct_ov-int4-GPU
optimum-cli export openvino -m microsoft/Phi-4-mini-instruct --weight-format int4 --sym --group-size 128 --ratio 1 --awq --scale-estimation --dataset=wikitext2 --trust-remote-code --backup-precision int8_sym models_NativeOpenVINO/Phi-4-mini-instruct_ov-int4-GRw
```

##### Extended models:
```bash
# NPU
optimum-cli export openvino -m Qwen/Qwen3-8B --weight-format int4 --sym --group-size -1 --ratio 1 --awq --scale-estimation --dataset=wikitext2 models_NativeOpenVINO/Qwen3-8B_ov-int4-CHw
optimum-cli export openvino -m microsoft/Phi-4-reasoning --weight-format int4 --sym --group-size -1 --ratio 1 --all-layers --awq --scale-estimation --dataset=wikitext2 models_NativeOpenVINO/Phi-4-reasoning_ov-int4-CHw
# GPU
optimum-cli export openvino -m Qwen/Qwen3-8B --weight-format int4 --sym --group-size 128 --ratio 1 --awq --scale-estimation --dataset=wikitext2 models_NativeOpenVINO/Qwen3-8B_ov-int4-GRw
optimum-cli export openvino -m microsoft/Phi-4-reasoning --weight-format int4 --sym --group-size 128 --ratio 1 --all-layers --awq --scale-estimation --dataset=wikitext2 models_NativeOpenVINO/Phi-4-reasoning_ov-int4-GRw

# Text-To-Image
optimum-cli export openvino --model black-forest-labs/FLUX.2-klein-4B ov_flux.2-klein-4b models_NativeOpenVINO/FLUX.2-klein-4B_ov
```

#### 1.3. Create IHV-OV-tokenizers.zip file

MLPerf Client require the tokenizer files inside a zip file.
- Select all final files with the exception of: openvino_model.bin and openvino_model.xml
- With the previous selection, Create zip file called IHV-OV-tokenizers.zip


## Additional Information

- Make sure you update your system with the latest NPU and GPU drivers available. (posteriori to: NPU  32.0.100.4778, GPU 32.0.101.8826)
- Ensure you have sufficient disk space and computational resources, as model conversion can be resource-intensive.
- For a comprehensive guide, refer to https://github.com/huggingface/optimum-intel
- "Export an LLM model via Hugging Face Optimum-Intel", https://docs.openvino.ai/2025/openvino-workflow-generative/inference-with-optimum-intel.html
- Make sure you include openvino_tokenizer.* and openvino_detokenizer.* into IHV-OV-tokenizers.zip
- Linux build was tested on Ubuntu 24.04
- [OpenVINO support setup guide for Linux AI PC](https://medium.com/openvino-toolkit/how-to-run-openvino-on-a-linux-ai-pc-52083ce14a98). Update NPU driver download link to the latest available on the GitHub.
