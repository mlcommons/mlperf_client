## Usage

```bash
mlperf-windows.exe -c Intel_OpenVINO_NPU_Default.json
mlperf-windows.exe -c Intel_OpenVINO_GPU_Default.json
```

An example config with files downloaded from web is available at `data/configs/vendors_default/Intel_OpenVINO_GPU_Default.json`. If `LibraryPath` is not set within the config file, it allows the app to automatically download dependencies and unpack the builtin `IHV_NativeOpenVINO.dll`.

## Model Building

There are three steps to create the model

### 1. Create the model using optimum-cli

Optimum Intel provides a simple interface to optimize Transformer and Diffuser models, convert them to the OpenVINO Intermediate Representation (IR) format to run inference using OpenVINO Runtime.

#### 1.1. Installation

To install latest Optimum Intel release with required dependencies one can use `pip` as follows:

```bash
python --version
# Python 3.12.4
cd openvino_genai_2025.0\python
pip install -r requirements.txt
pip install --upgrade --upgrade-strategy eager "optimum[openvino]"

```

#### 1.2. Convert model to OpenVINO IR format

Create the model from Meta Llama-2-7b-chat-hf model using following commands:

NPU model:

```bash
pip freeze | grep nncf
# nncf==2.15.0
# upgrade nncf module using: pip install --upgrade nncf
pip freeze | grep optimum-intel
# optimum-intel-1.22.0
optimum-cli export openvino -m meta-llama/Llama-2-7b-chat-hf --weight-format int4 --sym --group-size -1 --ratio 1.0 --all-layers --awq --scale-estimation --dataset=wikitext2  Llama-2-7b-hf_INT4-optimum-sym-CHw
```

GPU model:
```bash
optimum-cli export openvino --model meta-llama/Llama-2-7b-chat-hf --sym --weight-format int4 --group-size 128 --ratio 1.0 Llama-2-7b-hf_INT4-optimum-sym
```

## Additional Information

- Make sure you update your system with the latest NPU and GPU drivers available. (posteriori to: NPU 32.0.100.3967, GPU 32.0.101.6647)
- Ensure you have sufficient disk space and computational resources, as model conversion can be resource-intensive.
- For a comprehensive guide, refer to https://github.com/huggingface/optimum-intel
- "Export an LLM model via Hugging Face Optimum-Intel", https://docs.openvino.ai/2024/learn-openvino/llm_inference_guide/llm-inference-hf.html

