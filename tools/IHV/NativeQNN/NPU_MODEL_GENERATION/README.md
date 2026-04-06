# Optimizing AIMET Quantization workflow for Phi-4-reasoning, Phi-3.5-mini-instruct & Llama-3.1-8B-Instruct model for SnapDragon devices

## Platform requirements
This notebook is intended to run on a machine with:
  * Ubuntu 22.04
  * NVIDIA driver version equivalent to 525.60.13
  * NVIDIA A100 GPU
  * QAIRT version = 2.43.1.260218

## Install Notebook for Phi4 14B Reasoning chat 
1.) Download Notebook Version **v1.0.1.260219** from https://qpm.qualcomm.com/#/main/tools/details/Tutorial_for_Phi4_Reasoning_14B_Compute

## Install Notebook for Llama3.1 8B instruct
1.) Download Notebook Version **v1.0.1.260219** from https://qpm.qualcomm.com/#/main/tools/details/Tutorial_for_Llama3_1_Compute

## Install Notebook for Phi3.5 mini instruct
1.) Download Notebook Version **v1.0.1.260220** from https://qpm.qualcomm.com/#/main/tools/details/Tutorial_for_Phi3_5_Compute

## Install dependencies
Ensure that you have installed docker and the NVIDIA docker2 runtime: https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/latest/install-guide.html#docker

## Docker Environment setup
After unpacking this notebook package, use the following command to launch the container:

```bash
docker run --rm --gpus all --name=aimet-dev-torch-gpu -v $PWD:$PWD -w $PWD -v /etc/localtime:/etc/localtime:ro -v /etc/timezone:/etc/timezone:ro --network=host --ulimit core=-1 --ipc=host --shm-size=8G --cap-add=SYS_PTRACE --security-opt seccomp=unconfined -it artifacts.codelinaro.org/codelinaro-aimet/aimet-dev:latest.torch-gpu
```


### Install QAIRT

1. Extract the QAIRT 2.43.1.260218 version sdk using 
```bash
wget https://softwarecenter.qualcomm.com/api/download/software/sdks/Qualcomm_AI_Runtime_Community/All/2.43.1.260218/v2.43.1.260218.zip
```
2. Unzip the sdk
```bash
unzip v2.43.1.260218.zip
```
3. Copy QAIRT inside notebook folder

## Clone Source Model

```bash
git lfs install
```

1. Clone source model Phi-4-reasoning inside notebook folder using below commands.

```bash
git clone https://huggingface.co/microsoft/Phi-4-reasoning
```

2. Clone source model Phi-3.5-mini-instruct inside notebook folder using below commands.

```bash
git clone https://huggingface.co/microsoft/Phi-3.5-mini-instruct
```

3. Clone source model Llama-3.1-8B-Instruct inside notebook folder using below commands.

```bash
git clone https://huggingface.co/meta-llama/Llama-3.1-8B-Instruct
```

### Execution Steps

1. Copy files "run_example_1_adascale.py", "run_example_1.py", "run_example_2.py", "run_example_2_sdx_elite.py", "run_npu_bin.sh" and "requirements.txt" inside notebook folder.
2. Go inside folder of notebook and Run
```bash
pip install -r requirements.txt
```
```bash
bash run_npu_bin.sh "$model_name"
```
model_name can be "llama3.1", "phi4" and "phi3.5" based on the model for which user wants to generate npu binaries.