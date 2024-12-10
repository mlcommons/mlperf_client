## Model Building
ONNXRuntime GenAI (ORT GenAI) requires models to be built using its specific Model Builder. It doesn't work with standard `.onnx` files. The process involves converting and optimizing models for use with ONNXRuntime GenAI.
### Prerequisites
1. Install Python (3.8 or later recommended)
2. Install ONNXRuntime GenAI (for DirectML) and its dependencies:

```
pip install numpy transformers torch onnx onnxruntime
pip install onnxruntime-genai-directml --pre
```
### Building a Quantized ONNX Model
To create a quantized ONNX model for DirectML EP using the ONNXRuntime Model Builder:
```
python -m onnxruntime_genai.models.builder -m meta-llama/Llama-2-7b-chat-hf -e dml -p int4 -o ./llama-2-7b-chat-dml/ -c llama2-dml_cache 
```

This command:
- Uses the Llama-2-7b-chat-hf model from huggingface
- Targets the DirectML execution provider (`-e dml`)
- Applies int4 quantization (`-p int4`)
- Outputs to the specified directory (`-o ./llama-2-7b-chat-dml/`)
- Uses a cache for faster processing (`-c llama2-dml_cache`)

The result is a folder containing all necessary components for ONNXRuntime GenAI:
- Model (`model.onnx`)
- Weights(`model.onnx.data`)
- Tokenizer(`tokenizer.json, special_tokens_map.json`, )
- Configuration files (`genai_config.json, tokenizer_config.json`)

**Note**: The created model is specific to the chosen execution provider (EP). In this case, the model can only be executed with DirectML.
## Additional Information

- For a comprehensive guide, refer to the official Microsoft documentation: [Generate models using Model Builder](https://onnxruntime.ai/docs/genai/howto/build-model.html) and [Generate() API Python Examples](https://github.com/microsoft/onnxruntime-genai/blob/main/examples/python/README.md).
- Ensure you have sufficient disk space and computational resources, as model conversion can be resource-intensive.

## Usage
ONNXRuntime GenAI and its implementation in MLperf require the entire model folder which is created by the ONNXRuntime GenAI Builder, here is an example MLperf config, this example is using local paths for testing purposes and need to be updated once the model is in the MLC share: 
```
{
  "SystemConfig": {
    "Comment": "ONNXRuntime GenAI example config.",
    "TempPath": ""
  },
  "Scenarios": [
    {
      "Name": "Llama2",
      "Models": [
        {
          "ModelName": "Llama2 llama-2-7b-chat-dml",
          "FilePath": "https://client.mlcommons-storage.org/deps/0.5/scenario_files/llm/llama2/models/OrtGenAI/llama-2-7b-chat-dml/model.onnx",
          "DataFilePath": "https://client.mlcommons-storage.org/deps/0.5/scenario_files/llm/llama2/models/OrtGenAI/llama-2-7b-chat-dml/model.onnx.data.zip",
          "TokenizerPath": "https://client.mlcommons-storage.org/deps/0.5/scenario_files/llm/llama2/models/OrtGenAI/llama-2-7b-chat-dml/tokenizer.zip"
        }
      ],
       "InputFilePath": [
        "https://client.mlcommons-storage.org/deps/0.5/scenario_files/llm/llama2/data/content_generation/greedy-prompt_cot.39329.json"
      ],
      "AssetsPath": [],
      "ResultsVerificationFile": "https://client.mlcommons-storage.org/deps/0.5/scenario_files/llm/llama2/data/generation-test-greedy-expected-results.json",
      "DataVerificationFile": "",
      "Iterations": 5,
      "ExecutionProviders": [
        {
          "Name": "OrtGenAI",
          "Config": {
            "device_type": "GPU",
            "device_id": 0
          },
          "LibraryPath": "IHV/OrtGenAI/IHV_OrtGenAI.dll"
        }
      ]
    }
  ]
}
```

The `.onnx` and `.onnx.data.zip` files are downloaded separately and the tokenizer and its config files zipped and downloaded together.  MLperf needs to detect a `.onnx` file to run, therefore we can not zip the entire model. It is important that all files that the builder creates are unpacked into the same directory, otherwise ONNXRuntime GenAI does not detect the folder as a valid model. The Tokenizer files are still required even though they remain unused. 
The `.onnx.data.zip` model should be a ZIP including the `.onnx.data` file and the `genai_config.json` file. The latter is loaded by the IHV. Both files will be unpacked in the same directory as the `model.onnx` file, ensuring all data is in the same directory.
The `tokenizer.zip` should include `tokenizer_config.json`, `tokenizer.json` and `special_tokens_map.json` files.

An example config with files downloaded from web is available at `data/configs/vendors_default/NVIDIA_ORTGenAI-DML_GPU.json`. `LibraryPath` is not set, it allows the app to automatically download dependencies and unpack the builtin `IHV_OrtGenAi.dll`. 
Debugger is still going to work with it. However, if you prefer to use the LibraryPath as above, make sure that all DLLs dependencies are inside i.e. `onnxruntime.dll`, `DirectML.dll`, `onnxruntime-genai.dll`. These files are not in the repository, they can be downloaded from web. Follow `OrtGenAi` dependencies from `data/ep_dependencies_config_windows_x64.json`.

## Version

The repository contains prebuilt OGA library compatible with ONNX Runtime 1.19.0 release, using the main commit 7998f13e25f31caaba803db4197f737df99a0bb9.
It includes a custom patch from deps/ONNXRuntimeGenAI/oga_directml_adapters_choice.patch which allows to use LUUID for DirectML, forcing to run on a specific adapter