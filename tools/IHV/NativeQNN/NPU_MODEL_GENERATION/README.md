# Optimizing AIMET Quantization workflow for Llama-2-7b-chat-hf, Phi-3.5-mini-instruct & Llama-3.1-8B-Instruct model for SnapDragon devices

## Platform requirements
This notebook is intended to run on a machine with:
  * Ubuntu 22.04
  * NVIDIA driver version equivalent to 525.60.13
  * NVIDIA A100 GPU
  * AIMETPRO version 1.35.0 for Llama2 & Phi3.5
  * AIMETPRO version 2.0.0.52 for Llama3.1
  * QAIRT version = 2.35.3.250617

## Install Notebook for Llama2 7B chat 
1.) Download Notebook Version **v0.4.1.180325** from https://qpm.qualcomm.com/#/main/tools/details/Tutorial_for_Llama2_Compute

## Install Notebook for Llama3.1 8B instruct
1.) Download Notebook Version **v0.2.0.250621** from https://qpm.qualcomm.com/#/main/tools/details/Tutorial_for_Llama3_1_Compute

## Install Notebook for Phi3.5 mini instruct
1.) Download Notebook Version **v0.1.0.250620** from https://qpm.qualcomm.com/#/main/tools/details/Tutorial_for_Phi3_5_Compute

## Install dependencies
Ensure that you have installed docker and the NVIDIA docker2 runtime: https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/latest/install-guide.html#docker

## Docker Environment setup
After unpacking this notebook package, use the following command to launch the container:

```bash
docker run --rm --gpus all --name=aimet-dev-torch-gpu -v $PWD:$PWD -w $PWD -v /etc/localtime:/etc/localtime:ro -v /etc/timezone:/etc/timezone:ro --network=host --ulimit core=-1 --ipc=host --shm-size=8G --cap-add=SYS_PTRACE --security-opt seccomp=unconfined -it artifacts.codelinaro.org/codelinaro-aimet/aimet-dev:latest.torch-gpu
```


### Install QPM CLI

1. Download QPM3 for Linux from the Qualcomm site from this link: https://qpm.qualcomm.com/#/main/tools/details/QPM3
2. Run the following command in the Docker container using the downloaded .deb package:
    ```bash
    dpkg -i QualcommPackageManager*.Linux-x86.deb
    ```
3. Check if the package was installed correctly:
    ```bash
    which qpm-cli
    ```
   The output should be `/usr/bin/qpm-cli`.

### Install AIMETPro 1.35.0

1. Download the latest AIMETPro 1.35.0 version for Linux from this link: https://qpm.qualcomm.com/#/main/tools/details/Qualcomm_AIMET_Pro
2. Create an installation path config JSON file named `qpm_aimetpro_config.json` with the following content:
    ```json
    {
        "CustomInstallPath" : "/tmp/aimetpro"
    }
    ```
3. Use the QPM CLI to extract tarballs from the downloaded QIK file:
    ```bash
    qpm-cli --extract ./Qualcomm_AIMET_Pro.1.35.0*.Linux-AnyCPU.qik --config qpm_aimetpro_config.json
    ```
4. Extract the needed torch-gpu-pt113-release variant:
    ```bash
    cd /tmp/aimetpro && tar -xvzf aimetpro-release-1.35.0_build-*.torch-gpu-pt113-release.tar.gz
    ```
5. Go to the folder to install required packages for the target variant (torch-gpu-pt113-release):
    ```bash
    cd aimetpro-release-1.35.0_build-*.torch-gpu-pt113-release
    python3 -m pip install cumm-cu117 spconv-cu117 torch==1.13.1+cu117 torchvision==0.14.1+cu117 -f https://download.pytorch.org/whl/torch_stable.html
    python3 -m pip install pip/*.whl
    ```

### Install AIMETPro 2.0.0
1. Download the latest AIMETPro 2.0.0 version for Linux from this link: https://qpm.qualcomm.com/#/main/tools/details/Qualcomm_AIMET_Pro
2. Create an installation path config JSON file named `qpm_aimetpro2_config.json` with the following content:
    ```json
    {
        "CustomInstallPath" : "/tmp/aimetpro2"
    }
    ```
3. Use the QPM CLI to extract tarballs from the downloaded QIK file:
    ```bash
    qpm-cli --extract ./Qualcomm_AIMET_Pro.2.0.0.52.Linux-AnyCPU --config qpm_aimetpro2_config.json
    ```
4. Extract the needed torch-gpu-pt113-release variant:
    ```bash
    cd /tmp/aimetpro2 && tar -xvzf /tmp/aimetpro2/aimetpro-release*.torch-gpu-release.tar.gz
    ```
5. Go to the folder to install required packages for the target variant (torch-gpu-pt113-release):
    ```bash
    cd aimetpro-release-2.0.0_build-0.216.0.52.torch-gpu-release
    python3 -m pip install /tmp/aimetpro2/aimetpro-release*.torch-gpu-release/pip/*.whl -f https://download.pytorch.org/whl/torch_stable.html
    ```

### Install QAIRT

1. Extract the QAIRT 2.35.3.250617 version sdk using 
```bash
wget https://softwarecenter.qualcomm.com/api/download/software/sdks/Qualcomm_AI_Runtime_Community/All/2.35.3.250617/v2.35.3.250617.zip
```
2. Unzip the sdk
```bash
unzip v2.35.3.250617.zip
```
3. Copy QAIRT inside notebook folder

## Clone Source Model

```bash
git lfs install
```

1. Clone source model Llama-2-7b-chat-hf inside notebook folder using below commands.

```bash
git clone https://huggingface.co/meta-llama/Llama-2-7b-chat-hf
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

1. Copy files "htpBackendExtConfig.json", "run_example_1.py", "run_example_1.py", "run_npu_bin.sh" and "requirements.txt" inside notebook folder.
2. Go inside folder of notebook and Run
```bash
pip install -r requirements.txt
```
```bash
./run_npu_bin.sh "$model_name"
```
model_name can be "llama3.1", "llama2" and "phi3.5" based on the model for which user wants to generate npu binaries.