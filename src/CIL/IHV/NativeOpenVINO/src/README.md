## Usage

```bash
mlperf-windows.exe -c Intel_OpenVINO_NPU_llama2.json
mlperf-windows.exe -c Intel_OpenVINO_GPU_llama2.json

mlperf-windows.exe -c Intel_OpenVINO_NPU_llama3.json
mlperf-windows.exe -c Intel_OpenVINO_GPU_llama3.json

mlperf-windows.exe -c Intel_OpenVINO_NPU_phi3.json
mlperf-windows.exe -c Intel_OpenVINO_GPU_phi3.json
```

An example config with files downloaded from web is available at `data/configs/vendors_default/Intel_OpenVINO_GPU_llama2.json`. If `LibraryPath` is not set within the config file, it allows the app to automatically download dependencies and unpack the builtin `IHV_NativeOpenVINO.dll`.

## Model Building

There are three steps to create the model

### 1. Create the model using optimum-cli

Optimum Intel provides a simple interface to optimize Transformer and Diffuser models, convert them to the OpenVINO Intermediate Representation (IR) format to run inference using OpenVINO Runtime.

#### 1.1. Installation

To install latest Optimum Intel release with required dependencies one can use `pip` as follows:

Download OpenVINO 2025.2.0.0rc2 or later from:
https://storage.openvinotoolkit.org/repositories/openvino_genai/packages/pre-release/2025.2.0.0rc2/openvino_genai_windows_2025.2.0.0rc2_x86_64.zip


```bash
python --version
# Python 3.12.4
cd openvino_genai_2025.2.0.0rc2/python
pip install -r requirements.txt
pip install --upgrade --upgrade-strategy eager "optimum[openvino]"

pip freeze | grep nncf
# nncf==2.16.0
# upgrade nncf module using: pip install --upgrade nncf
pip freeze | grep optimum-intel
# optimum-intel-1.23.0
```

#### 1.2. Convert model to OpenVINO IR format

Create the model from Hugging Face using following commands:

##### NPU models:
```bash
optimum-cli export openvino -m meta-llama/Llama-2-7b-chat-hf --weight-format int4 --sym --group-size -1 --ratio 1.0 --all-layers --awq --scale-estimation --dataset=wikitext2  Llama-2-7b-chat-hf_ov-int4-CHw
optimum-cli export openvino -m meta-llama/Llama-3.1-8B-Instruct --weight-format int4 --sym --group-size -1 --ratio 1.0 --all-layers --awq --scale-estimation --dataset=wikitext2 Llama-3.1-8B-Instruct_ov-int4-CHw
optimum-cli export openvino -m microsoft/Phi-3.5-mini-instruct --weight-format int4 --sym --group-size -1 --ratio 1.0 --all-layers --awq --scale-estimation --dataset=wikitext2 Phi-3.5-mini-instruct_ov-int4-CHw
```

##### GPU models:
```bash
optimum-cli export openvino -m meta-llama/Llama-2-7b-chat-hf --weight-format int4 --sym --group-size 128 --ratio 1.0 --all-layers --awq --scale-estimation --dataset=wikitext2 Llama-2-7b-chat-hf_ov-int4-GRw
optimum-cli export openvino -m meta-llama/Llama-3.1-8B-Instruct --weight-format int4 --sym --group-size 128 --ratio 1.0 --all-layers --awq --scale-estimation --dataset=wikitext2 Llama-3.1-8B-Instruct_ov-int4-GRw
optimum-cli export openvino -m microsoft/Phi-3.5-mini-instruct --weight-format int4 --sym --group-size 128 --ratio 1.0 --all-layers --awq --scale-estimation --dataset=wikitext2 Phi-3.5-mini-instruct_ov-int4-GRw
```

##### Experimental models:
```bash
# NPU
optimum-cli export openvino -m microsoft/Phi-4-reasoning  --weight-format int4 --sym --group-size -1 --ratio 1.0 --all-layers --awq --scale-estimation --dataset=wikitext2 Phi-4-reasoning_ov-int4-CHw
# GPU
optimum-cli export openvino -m microsoft/Phi-4-reasoning --weight-format int4 --sym --group-size 128 --ratio 1.0 --all-layers --awq --scale-estimation --dataset=wikitext2 Phi-4-reasoning_ov-int4-GRw
```

#### 1.3. Create IHV-OV-tokenizers.zip file

MLPerf Client require the tokenizer files inside a zip file.
- Select all final with the exception of: openvino_model.bin and openvino_model.xml
- With the previous selection, Create zip file called IHV-OV-tokenizers.zip


## Additional Information

- Make sure you update your system with the latest NPU and GPU drivers available. (posteriori to: NPU 32.0.100.4023, GPU 32.0.101.6881)
- Ensure you have sufficient disk space and computational resources, as model conversion can be resource-intensive.
- For a comprehensive guide, refer to https://github.com/huggingface/optimum-intel
- "Export an LLM model via Hugging Face Optimum-Intel", https://docs.openvino.ai/2025/openvino-workflow-generative/inference-with-optimum-intel.html
- Make sure you include openvino_tokenizer.* and openvino_detokenizer.* into IHV-OV-tokenizers.zip

