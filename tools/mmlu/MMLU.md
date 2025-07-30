
# MMLU Benchmark Script

## Overview

The `run_mmlu_benchmark.py` script is designed to run the MLPerf program on the Massive Multitask Language Understanding (MMLU) test data, automating the process of testing and reporting accuracy across various subjects. It performs the following steps:

1. **Download MMLU Data** (if needed)
2. **Generate Input Files**: Creates JSON files with formatted prompts for MLPerf.
3. **Generate Config Files**: Builds configuration files for MLPerf.
4. **Run MLPerf Program**: Executes MLPerf on the generated configurations.
5. **Calculate Accuracy**: Processes results to calculate accuracy.
6. **Generate Report**: Produces a report based on accuracy and inference data.

## Requirements

- **MLPerf Program**: Specify the path to the MLPerf executable in the configuration file.
- **Python Packages**: Install required packages:

    ```bash
    pip install -r requirements-mmlu.txt
    ```

This script has been tested with **Python 3.12**. It is recommended to use Python 3.12 or higher to ensure compatibility.

## Configuration File

The script takes a JSON configuration file as input. Below is an example configuration:

```json
{
    "MMLUDataPath": "mmlu_data",  
    "MLPerfProgramPath": "path/to/mlperf-windows.exe",
    "OutputDir": "output",  
    "DataSplitStep": null,  
    "Subjects": ["abstract algebra", "astronomy"],
    "HfToken": "hf_*****",
    "RunID": null,  
    "FewShotPromptsNumber": 5,  
    "InputConfigPath": null,
    "RunConfigPath": null,
    "InputConfigTemplate": {  
        "model_config": {
            "model": {
                "bos_token_id": 1,
                "context_length": 4096,
                "decoder": {
                    "head_size": 128,
                    "hidden_size": 4096,
                    "inputs": {
                        "input_ids": "input_ids",
                        "attention_mask": "attention_mask",
                        "position_ids": "position_ids",
                        "past_key_names": "past_key_values.%d.key",
                        "past_value_names": "past_key_values.%d.value"
                    },
                    "outputs": {
                        "logits": "logits",
                        "present_key_names": "present.%d.key",
                        "present_value_names": "present.%d.value"
                    },
                    "num_attention_heads": 32,
                    "num_hidden_layers": 32,
                    "num_key_value_heads": 32
                },
                "eos_token_id": 2,
                "pad_token_id": 0,
                "vocab_size": 32000
            }
        }
    },
    "RunConfigTemplate": {
        "SystemConfig": {
            "Comment": "Default config",
            "TempPath": "",
            "EPDependenciesConfigPath": ""
        },
        "Scenarios": [
            {
                "Name": "Llama2",
                "Models": [
                    {
                        "ModelName": "Llama2 int4-cpu",
                        "FilePath": "https://mlperf-public-files.s3.amazonaws.com/scenario_files/llm/llama2/models/llama-2-7b-chat/llama2-cpu-int4/model.onnx",
                        "DataFilePath": "https://mlperf-public-files.s3-accelerate.amazonaws.com/scenario_files/llm/llama2/models/llama-2-7b-chat/llama2-cpu-int4/model.onnx.data",
                        "TokenizerPath": "https://mlperf-public-files.s3.amazonaws.com/scenario_files/llm/llama2/models/llama-2-7b-chat/tokenizer.zip"
                    }
                ],
                "AssetsPath": [],
                "DataVerificationFile": "",
                "ExecutionProviders": [
                    {
                        "Name": "CUDA",
                        "Config": {
                            "device_type": "GPU",
                            "device_id": 0
                        }
                    }
                ]
            }
        ]
    }
}
```

### Configuration Parameters

- **MMLUDataPath**: Path to the MMLU data. If empty, data is downloaded automatically.
- **MLPerfProgramPath**: Path to the MLPerf executable.
- **OutputDir**: Directory for all generated files and results.
- **DataSplitStep**: Controls how prompts are split and stored in configuration files (set to `null` by default). When set to an integer, the program splits prompts into separate input configuration files, each containing the specified number of prompts, and runs them individually. When set to `null`, prompts are grouped by subject into a single configuration file, and all prompts are run at once
- **Subjects**: List of subjects to calculate accuracy for. If set to `null`, all subjects are used.
- **RunID**: Use to resume a previous run (`null` starts a new run).
- **FewShotPromptsNumber**: Number of few-shot prompts to include.
- **InputConfigPath** and **RunConfigPath**: These specify paths to pre-defined configuration files. When `InputConfigPath` is `null`, the script will use `InputConfigTemplate` to define model architecture details; otherwise, it will use the specified path in `InputConfigPath`. Similarly, if `RunConfigPath` is `null`, the script defaults to `RunConfigTemplate` to specify model and tokenizer paths, required files, and execution providers for running the model; otherwise, it uses the provided path in `RunConfigPath`

### Customizing the Templates

Adjust the `RunConfigTemplate` and `InputConfigTemplate` sections for model configurations and settings that match your MLPerf requirements.

## Running the Script

To run the script, execute the following command, passing the configuration JSON file:

```bash
python run_mmlu_benchmark.py --config <path_to_config.json>
```

## Output

- **Generated Files**: Input prompts and config files saved in `OutputDir`.
- **Inference Results**: MLPerf results stored in `OutputDir`.
- **Accuracy Report**: A final report based on calculated accuracy.

## Skipping failed prompts

also you can skip the failed prompts during benchmarking, in the generated `results.json` files you will be able to see `Skipped Prompts` indexes. To enable this feature you need to run the program with `-s` or `--skip-failed-prompts` option

# TinyMMLU

to run script on `TinyMMLU` data you need to add your huggingface token in config file and when running script choose benchmark type `tinymmlu`, like this

```bash
python run_mmlu_benchmark.py --config <path_to_config.json> -t tinymmlu
```