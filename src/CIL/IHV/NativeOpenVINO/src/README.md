## Usage

```bash
mlperf-windows.exe -c Intel_OpenVINO_GPU_Default.json.json
```

An example config with files downloaded from web is available at `data/configs/vendors_default/Intel_OpenVINO_GPU_Default.json`. If `LibraryPath` is not set within the config file, it allows the app to automatically download dependencies and unpack the builtin `IHV_NativeOpenVINO.dll`.

## Model Building

There are three steps to create the model

### 1. Create the model using optimum-cli

Optimum Intel provides a simple interface to optimize Transformer and Diffuser models, convert them to the OpenVINO Intermediate Representation (IR) format to run inference using OpenVINO Runtime.

#### 1.1. Installation

To install latest Optimum Intel release with required dependencies one can use `pip` as follows:

```bash
pip install --upgrade --upgrade-strategy eager "optimum[openvino]"
```

#### 1.2. Convert model to OpenVINO IR format

Create GPU model from Meta Llama2-7b model using following commands:

GPU model:
```bash
optimum-cli export openvino --model meta-llama/Llama-2-7b-chat-hf --sym --weight-format int4 --group-size 128 --ratio 1.0 Llama-2-7b-hf_INT4-optimum-sym
```

## Additional Information

- Make sure you update your system with the latest GPU drivers available. (posteriori to:  GPU 32.0.101.6048)
- Ensure you have sufficient disk space and computational resources, as model conversion can be resource-intensive.
- For a comprehensive guide, refer to https://github.com/huggingface/optimum-intel/blob/main/README.md

