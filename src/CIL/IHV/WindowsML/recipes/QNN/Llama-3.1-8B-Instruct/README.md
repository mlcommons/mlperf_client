QCOM-NPU: Llama-3.1-8B-Instruct Model Optimization
==================================================

This repository demonstrates the optimization of the [Llama-3.1-8B-Instruct](https://huggingface.co/meta-llama/Llama-3.1-8B-Instruct) model using **post-training quantization (PTQ)** techniques.

### Quantization Python Environment Setup
Quantization is resource-intensive and requires GPU acceleration. In an x64 Python environment, install the required packages:

```bash
pip install -r requirements.txt

# Disable CUDA extension build
# Linux
export BUILD_CUDA_EXT=0
# Windows
# set BUILD_CUDA_EXT=0

# Install olive from source
pip install --no-build-isolation git+https://github.com/microsoft/Olive.git@b9c975f5651a4446283d9183348add9dcd4ed45f

# Install GptqModel from source
pip install --no-build-isolation git+https://github.com/CodeLinaro/GPTQModel.git@rel_4.2.5
```

### AOT Compilation Python Environment Setup
Model compilation using QNN Execution Provider requires a Python environment with onnxruntime-qnn installed. In a separate Python environment, install the required packages:

```bash
# Install Olive
pip install --no-build-isolation git+https://github.com/microsoft/Olive.git@b9c975f5651a4446283d9183348add9dcd4ed45f

# Install ONNX Runtime QNN
pip install -r https://raw.githubusercontent.com/microsoft/onnxruntime/refs/heads/main/requirements.txt
pip install --index-url https://aiinfra.pkgs.visualstudio.com/PublicPackages/_packaging/ORT-Nightly/pypi/simple "onnxruntime-qnn==1.23.2" --no-deps
```

Replace `/path/to/qnn/env/bin` in [config.json](config.json) with the path to the directory containing your QNN environment's Python executable. This path can be found by running the following command in the environment:

```bash
# Linux
command -v python
# Windows
# where python
```

This command will return the path to the Python executable. Set the parent directory of the executable as the `/path/to/qnn/env/bin` in the config file.

### Run the Quantization + Compilation Config
Activate the **Quantization Python Environment** and run the workflow:

```bash
olive run --config x2_elite_config.json
```

Olive will run the AOT compilation step in the **AOT Compilation Python Environment** specified in the config file using a subprocess. All other steps will run in the **Quantization Python Environment** natively.

✅ Optimized model saved in: `models/llama_3.1_8b_Instruct/`

> ⚠️ If optimization fails during context binary generation, rerun the command. The process will resume from the last completed step.
