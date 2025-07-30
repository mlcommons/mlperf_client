
# Model Building

This document describes how to build, quantize, and compile models for WindowsML—targeting OpenVINO—with Olive, 
including recipes that reproduce quantized and compiled variants of **Llama 2 7B Chat**, **Llama 3.1 8B Instruct**, and **Phi-3.5 Mini Instruct**, .

| Model | OpenVINO (Intel) |
|-------|----------------|
| **Llama 2 7B Chat** | - |
| **Llama 3.1 8B Instruct** | - |
| **Phi‑3.5** | [`https://github.com/microsoft/Olive/blob/main/examples/phi3_5/openvino/Phi-3.5-mini-instruct_context_ov_dynamic_sym_gs128_bkp_int8_sym.json`](https://github.com/microsoft/Olive/blob/main/examples/phi3_5/openvino/Phi-3.5-mini-instruct_context_ov_dynamic_sym_gs128_bkp_int8_sym.json) |
| **Phi-4-reasoning** | ['Phi-4-reasoning_context_ov_dynamic_sym_gs128_bkp_int8_sym.json'](https://github.com/microsoft/Olive/blob/main/examples/phi4/openvino/phi_4_reasoning/Phi-4-reasoning_context_ov_dynamic_sym_gs128_bkp_int8_sym.json) |

> **Tip**  
> Each recipe fully describes the required passes and target device; you usually only need to edit the `model_path` field if you prefer a different Hugging Face repository or a local checkpoint.

---

## 1. Common Prerequisites

Follow the README provided in the examples folder of the Olive repository to install Olive and its dependencies.
[`Llama 2 7B Chat`](https://github.com/microsoft/Olive/tree/main/examples/llama2)
[`Llama 3 8B Instruct`](https://github.com/microsoft/Olive/tree/main/examples/llama3)
[`Phi 3.5 Mini Instruct`](https://github.com/microsoft/Olive/tree/main/examples/phi3_5)
[`Phi 4 Reasoning`](https://github.com/microsoft/Olive/tree/main/examples/phi4)



## 2. Running an Olive Recipe (OpenVINO)

### OpenVINO models:

#### NPU models
```bash
olive run --config Llama-2-7b-chat-hf_ov-int4-CHw.json
olive run --config Llama-3.1-8B-Instruct_ov-int4-CHw.json
olive run --config Phi-3.5-mini-instruct_ov-int4-CHw.json
olive run --config Phi-4-reasoning_ov-int4-CHw.json

```

#### GPU models
```bash
olive run --config Llama-2-7b-chat-hf_ov-int4-GRw.json
olive run --config Llama-3.1-8B-Instruct_ov-int4-GRw.json
olive run --config Phi-3.5-mini-instruct_ov-int4-GRw.json
olive run --config Phi-4-reasoning_ov-int4-GRw.json
```

#### GPU model config example json file for Llama-2-7b-chat-hf_ov-int4-GRw
Change input_model and output_dir accordingly
```bash
$ more Llama-2-7b-chat-hf-ov-gw.json
{
    "input_model": { "type": "HfModel", "model_path": "meta-llama/Llama-2-7b-chat-hf" },
    "systems": {
        "local_system": {
            "type": "LocalSystem",
            "accelerators": [ { "execution_providers": [ "OpenVINOExecutionProvider" ] } ]
        }
    },
    "passes": {
        "optimum_convert": {
            "type": "OpenVINOOptimumConversion",
            "extra_args": { "device": "gpu" },
            "ov_quant_config": {
                "weight_format": "int4",
                "group_size": 128,
                "dataset": "wikitext2",
                "ratio": 1,
                "all-layers": true,
                "awq": true,
                "scale_estimation": true,
                "sym": true,
                "trust_remote_code": true,
                "backup_precision": "int8_asym",
                "sensitivity_metric": "weight_quantization_error"
            }
        },
        "io_update": { "type": "OpenVINOIoUpdate", "static": false, "reuse_cache": true },
        "encapsulation": {
            "type": "OpenVINOEncapsulation",
            "target_device": "gpu",
            "keep_ov_dynamic_dims": true,
            "ov_version": "2025.2",
            "reuse_cache": true
        }
    },
    "search_strategy": false,
    "host": "local_system",
    "cache_dir": "cache",
    "clean_cache": true,
    "evaluate_input_model": false,
    "output_dir": "models/windowsml/Llama-2-7b-chat-hf_ov-int4-GRw"
}
```

#### NPU model config example json file for Llama-2-7b-chat-hf_ov-int4-CHw
Change input_model and output_dir accordingly
```bash
$ more Llama-2-7b-chat-hf-ov_chw.json
{
    "input_model": { "type": "HfModel", "model_path": "meta-llama/Llama-2-7b-chat-hf" },
    "systems": {
        "local_system": {
            "type": "LocalSystem",
            "accelerators": [ { "execution_providers": [ "OpenVINOExecutionProvider" ] } ]
        }
    },
    "passes": {
        "optimum_convert": {
            "type": "OpenVINOOptimumConversion",
            "extra_args": { "device": "npu" },
            "ov_quant_config": {
                "weight_format": "int4",
                "group_size": -1,
                "dataset": "wikitext2",
                "ratio": 1,
                "all-layers": true,
                "awq": true,
                "scale_estimation": true,
                "sym": true,
                "trust_remote_code": true,
                "backup_precision": "int8_asym",
                "sensitivity_metric": "weight_quantization_error"
            }
        },
        "io_update": { "type": "OpenVINOIoUpdate", "static": false, "reuse_cache": true },
        "encapsulation": {
            "type": "OpenVINOEncapsulation",
            "target_device": "npu",
            "keep_ov_dynamic_dims": true,
            "ov_version": "2025.2",
            "reuse_cache": true
        }
    },
    "search_strategy": false,
    "host": "local_system",
    "cache_dir": "cache",
    "clean_cache": true,
    "evaluate_input_model": false,
    "output_dir": "models/windowsml/Llama-2-7b-chat-hf_ov-int4-CHw"
}
```

## 3. Troubleshooting & Tips

Please use `rel-0.9.1` Olive branch for the recipes in this document. If you encounter issues, consider the following:
- Ensure you have the correct version of Olive installed.
- Check the ONNX version installed, ONNX==1.17, for IR_Version_ = 10
- WindowsML path tries to run the model on all devices. You can use `device_ep` and/or `device_vendor` config options to specify the target EP and Vendor if the model provided is not compatible with all devices on this path.
```
 "ExecutionProviders": [
        {
          "Name": "WindowsML",
          "Config": {
            "device_type": "GPU",
            "device_ep": ""OpenVINO",
            "device_vendor": "Intel"
          },
        }
      ]
```

---

### References

* Olive repo – <https://github.com/microsoft/Olive>  
* ONNXRuntime GenAI docs – <https://onnxruntime.ai/docs/genai/>  
