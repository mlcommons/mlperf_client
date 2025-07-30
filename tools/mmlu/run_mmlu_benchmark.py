import re
import os
import sys
import tqdm
import json
import time
import shutil
import tarfile
import zipfile
import requests
import argparse
import subprocess
import pandas as pd
from loguru import logger
from pathlib import Path
from tempfile import TemporaryDirectory
from huggingface_hub import hf_hub_download
from tokenizer_utils import load_tokenizer


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="This script is used to run MMLU benchmark using mlperf")
    parser.add_argument("-c", "--config", type=str, required=True, 
                        help="Path to the config file to run MMLU benchmark (see tools/mmlu/mmlu-config.json)")
    parser.add_argument("-v", "--verbose", action="store_true", help="If enabled will run the script in verbose mode")
    parser.add_argument("-t", "--type", type=str, default="mmlu", choices=["mmlu", "tinymmlu"], help="Type of the benchmark to run")
    parser.add_argument("-s", "--skip-failed-prompts", action="store_true", 
                        help="If enabled will skip the failed prompts")
    
    return parser.parse_args()


def get_run_id(input_files_dir: str, config_run_id: int) -> int:
    run_id = None
    if config_run_id is None:
        if os.path.exists(input_files_dir):
            items = os.listdir(input_files_dir)
            ids = [int(item) for item in items if item.isdigit() 
                and os.path.isdir(os.path.join(input_files_dir, item))]
            if ids:
                run_id = max(ids) + 1
            else:
                run_id = 1
        else:
            run_id = 1
    elif config_run_id > 0:
        run_id = config_run_id
    else:
        raise ValueError(f"RunID in the config file can be null or any positive integer, but it is {config['RunID']}")
    
    logger.info(f"Run ID is {run_id}")
    return run_id


def download_mmlu_test_data(output_path: str):
    mmlu_data_url = "https://people.eecs.berkeley.edu/~hendrycks/data.tar"
    
    with TemporaryDirectory("mmlu_data") as temp:
        temp_tar_file_path = os.path.join(temp, 'data.tar')
        
        os.makedirs(output_path, exist_ok=True)
        
        logger.info("Downloading MMLU data")
        download_zip_with_requests(mmlu_data_url, temp_tar_file_path)
        
        # Extract the tar file
        with tarfile.open(temp_tar_file_path, 'r', encoding="utf-8") as tar_ref:
            tar_ref.extractall(path=os.path.dirname(temp_tar_file_path))

        temp_test_data_path = os.path.join(os.path.dirname(temp_tar_file_path), "data", "test")
        temp_dev_data_path = os.path.join(os.path.dirname(temp_tar_file_path), "data", "dev")
        
        shutil.move(temp_test_data_path, output_path)
        shutil.move(temp_dev_data_path, output_path)
            
    logger.info(f"Downloaded MMLU test and dev data to {output_path}")


def decode_unicode_escapes_safe(text):
    def replace_match(match):
        try:
            return chr(int(match.group(1), 16))
        except:
            return match.group(0)  # leave as-is if invalid
    return re.sub(r'\\u([0-9a-fA-F]{4})', replace_match, text)


def format_prompt(df, idx, include_answer=True, add_section_title=False):
    if add_section_title:
        prompt = "Question:\n```"
        prompt += df.iloc[idx, 0]
        prompt += "```\nOptions:"
    else:
        prompt = df.iloc[idx, 0]
    k = df.shape[1] - 2
    choices = df.columns[1: -1].tolist()
    for j in range(k):
        prompt += "\n{}) {}".format(choices[j], df.iloc[idx, j + 1])
    prompt += "\nAnswer:\n"
    if include_answer:
        prompt += "{}\n\n".format(df.iloc[idx, k + 1])
    return prompt

def get_phi35_prompt(
    subject_name,
    few_shot_prompts_number,
    subject_dev_df,
    subject_test_df,
    idx
):
    sys_prompt = (
        f"You are a professor in {subject_name} and an expert at answering multiple-choice questions.\n"
        "Carefully analyze, understand and answer the 'Test Question' by choosing the best answer only from A, B, C, or D, never add explanations or extra text."
    )
    examples = ""
    for i in range(few_shot_prompts_number):
        examples += format_prompt(subject_dev_df, i, include_answer=True, add_section_title=True)
    return (
        f"{sys_prompt}\n\n"
        f"{examples}"
        f"Test {format_prompt(subject_test_df, idx, include_answer=False, add_section_title=True)}"
    )

def check_existing_config_file(file_path: str, config: dict, raise_error: bool = False) -> True:
    if os.path.exists(file_path):
        with open(file_path, "r", encoding="utf-8") as f:
            existing_config = json.load(f)
        if existing_config != config:
            logger.error(f"Existing config {file_path}:\n{existing_config}\n\nGenerated config:\n{config}\n")
            
            if raise_error:
                raise ValueError(f"The existing config and generated config are different {file_path}." 
                                "If you want to run new experiment please set RunID to 'null' in MMLU config.")
             
        logger.info(f"{file_path} is existing")
        return True
    return False


# Function to save prompts and answers to files
def save_chunk(input_dir: str, subject: str, input_config_template: dict, 
               prompts_chunk: list[str], answers_chunk: list[str], 
               file_suffix: str = "", **kwargs) -> str:
    input_config = input_config_template.copy()
    if "search" in input_config["model_config"].keys():
        input_config["model_config"]["search"].update({
            "method": "greedy",
            "num_beams": 1,
            "temperature": 0,
            "top_k": 1,
            "stop_on_eos": True,
            "max_length": kwargs.get("max_length", 1),
        })
    else:
        input_config["model_config"]["search"] = {
            "method": "greedy",
            "num_beams": 1,
            "temperature": 0,
            "top_k": 1,
            "stop_on_eos": True,
            "max_length": kwargs.get("max_length", 1),
            "max_total_length": 4096
        }
    input_config["prompts"] = prompts_chunk
    answers_dict = {"answers": answers_chunk, "subject": subject}
    
    chunk_dir_path = os.path.join(input_dir, "_".join(subject.split()) + file_suffix)
    os.makedirs(chunk_dir_path, exist_ok=True)
    
    input_file_path = os.path.join(chunk_dir_path, "input_file.json")
    answers_file_path = os.path.join(os.path.dirname(input_file_path), "answers.json")
    
    if check_existing_config_file(input_file_path, input_config) and check_existing_config_file(answers_file_path, answers_dict):
        return input_file_path
    
    with open(input_file_path, "w", encoding="utf-8") as f:
        f.write(json.dumps(input_config, ensure_ascii=False, indent=4))
    
    with open(answers_file_path, "w", encoding="utf-8") as f:
        f.write(json.dumps(answers_dict, ensure_ascii=False, indent=4))
    
    logger.info(f"Saved {input_file_path}")
        
    return input_file_path


def save_input_files_and_answers(input_dir: str, subject: str, input_config_template: dict, 
                                prompts: list[str], answers: list[str], 
                                data_split_step: int | None, **kwargs) -> list[str]:
    saved_input_files = []
    
    if data_split_step is None:
        saved_input_files.append(save_chunk(input_dir, subject, input_config_template, prompts, answers, **kwargs))
    else:
        for i in range(0, len(prompts), data_split_step):
            chunk_prompts = prompts[i:i + data_split_step]
            chunk_answers = answers[i:i + data_split_step]
            saved_input_files.append(save_chunk(input_dir, subject, input_config_template, chunk_prompts, 
                                                chunk_answers, file_suffix=f"_{i//data_split_step}", **kwargs))
            
    return saved_input_files

def download_zip_with_requests(url, download_path):
    headers = {
        'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36'
    }
    try:
        Path(download_path).parent.mkdir(parents=True, exist_ok=True)
        response = requests.get(url, headers=headers, stream=True)
        response.raise_for_status()
        with open(download_path, 'wb') as f:
            for chunk in response.iter_content(chunk_size=8192):
                f.write(chunk)
        print(f"Successfully downloaded {os.path.getsize(download_path)} bytes to: {download_path}")
    except requests.exceptions.HTTPError as e:
        print(f"HTTP Error: {e}")
        raise
    except Exception as e:
        print(f"Error: {e}")
        raise
    
def generate_mmlu_input_files(input_dir: str, input_config_template: dict, run_config_template: dict,
                              mmlu_data_path: str, subjects: list[str] | None, 
                              data_split_step: int | None, few_shot_prompts_number: int) -> list[str]:
    kwargs = {}
    saved_input_files = []
    tokenizer_config_extra = {"trust_remote_code": True}
    tokenizer_data_path = (
        run_config_template
        .get("Scenarios", [{}])[0]
        .get("Models", [{}])[0]
        .get("TokenizerPath", "")
    )
    execution_provider_name = (
        run_config_template
        .get("Scenarios", [{}])[0]
        .get("ExecutionProviders", [{}])[0]
        .get("Name", "")
        .lower()
    )
    model_name = (
        run_config_template
        .get("Scenarios", [{}])[0]
        .get("Name", "")
        .lower()
    )
    if tokenizer_data_path:
        with TemporaryDirectory("temp_mmlu_tokenizer") as temp_mmlu_tokenizer_dir_path:
            temp_tokenizer_zip_path = os.path.join(temp_mmlu_tokenizer_dir_path, 'tokenizer.zip')
            logger.info("Downloading Tokenizer data")
            download_zip_with_requests(tokenizer_data_path, temp_tokenizer_zip_path)
            with zipfile.ZipFile(temp_tokenizer_zip_path, 'r') as zip_ref:
                zip_ref.extractall(temp_mmlu_tokenizer_dir_path)

            tokenizer = load_tokenizer(
                model_path=temp_mmlu_tokenizer_dir_path,
                tokenizer_config_extra=tokenizer_config_extra,
                eos_token_ids=None
            )
            if tokenizer.chat_template is None:
                if os.path.exists(os.path.join(temp_mmlu_tokenizer_dir_path, 'tokenizer_config.json')):
                    with open(os.path.join(temp_mmlu_tokenizer_dir_path, 'tokenizer_config.json'), 'r', encoding="utf-8") as f:
                        tokenizer_config = json.load(f)
                    chat_template = tokenizer_config.get("chat_template", None)
                    if chat_template is not None:
                        tokenizer.chat_template = chat_template

            for filename in os.listdir(os.path.join(mmlu_data_path, "test")):
                subject_name = " ".join(filename.split("_")[:-1])
                if subjects is None or subject_name in subjects:
                    logger.info(f"Processing input for subject {subject_name}")

                    subject_test_df = pd.read_csv(
                        os.path.join(mmlu_data_path, "test", filename),
                        names=["question", "A", "B", "C", "D", "answer"]
                    )
                    subject_dev_df = pd.read_csv(
                        os.path.join(mmlu_data_path, "dev", "_".join(subject_name.split() + ["dev.csv"])),
                        names=["question", "A", "B", "C", "D", "answer"]
                    )

                    sys_prompt = (
                        f"The following are multiple choice questions about {subject_name}.\n"
                        "Answer the last question by selecting the correct option character, 'A', 'B', 'C' or 'D', do not add any other text.\n\n"
                    )
                    examples = ""
                    for i in range(few_shot_prompts_number):
                        examples += format_prompt(subject_dev_df, i, include_answer=True)

                    prompts = []
                    answers = []

                    for i in range(subject_test_df.shape[0]):
                        prompt = (
                            f"{examples}"
                            f"{format_prompt(subject_test_df, i, include_answer=False)}"
                        )
                        if (
                            (tokenizer.chat_template is not None)
                            & (execution_provider_name == "mlx")
                            & (model_name not in ["llama2", "phi3.5", "phi4"])
                        ):
                            messages = [
                                {"role": "user", "content": f"{sys_prompt}{prompt}"}
                            ]
                            prompt = tokenizer.apply_chat_template(
                                messages,
                                tokenize=False,
                                add_generation_prompt=True
                            )
                        else:
                            if model_name == "llama2":
                                kwargs["max_length"] = 2
                            if model_name == "phi3.5":
                                prompt = get_phi35_prompt(
                                    subject_name=subject_name,
                                    few_shot_prompts_number=few_shot_prompts_number,
                                    subject_dev_df=subject_dev_df,
                                    subject_test_df=subject_test_df,
                                    idx=i
                                )
                            else:
                                prompt = (
                                    f"{sys_prompt}"
                                    f"{prompt}"
                                )
                        prompt = decode_unicode_escapes_safe(prompt)
                        prompts.append(prompt)
                        answers.append(subject_test_df.loc[i, "answer"])
                    
                    saved_input_files += save_input_files_and_answers(
                        input_dir,
                        subject_name,
                        input_config_template,
                        prompts,
                        answers,
                        data_split_step,
                        **kwargs
                    )
    else:
        raise ValueError(f"No TokenizerPath found in the config file !")

    return saved_input_files


def generate_mlperf_config_file(input_files: list[str], config_template: dict):
    generated_configs = []
    
    for input_file_path in input_files:
        input_file_path = os.path.abspath(input_file_path)
        input_dir = os.path.dirname(input_file_path)
        with open(input_file_path, "r", encoding="utf-8") as f:
            input_file = json.load(f)
        
        prompts_number = len(input_file["prompts"])
        
        dummy_results_verification_filepath = os.path.join(input_dir, "dummy_results_verification_file.json")
        
        dummy_results_verification_json = {"expected_tokens": [[1]] * prompts_number}
        
        if not check_existing_config_file(dummy_results_verification_filepath, dummy_results_verification_json):
            with open(dummy_results_verification_filepath, "w", encoding="utf-8") as f:
                f.write(json.dumps(dummy_results_verification_json, ensure_ascii=False, indent=4))
                
            logger.info(f"Saved dummy results verification file: {dummy_results_verification_filepath}")
    
        mlperf_config_filepath = os.path.join(input_dir, "mlperf_config.json")
        
        mlperf_config = config_template.copy()
        
        for i in range(len(mlperf_config["Scenarios"])):
            mlperf_config["Scenarios"][i]["InputFilePath"] = ["file://" + input_file_path]
            mlperf_config["Scenarios"][i]["ResultsVerificationFile"] = "file://" + dummy_results_verification_filepath
            mlperf_config["Scenarios"][i]["Iterations"] = 1
            mlperf_config["Scenarios"][i]["WarmUp"] = 0
        
        if check_existing_config_file(mlperf_config_filepath, mlperf_config):
            valid_results = False
            results_filepath = os.path.join(input_dir, "results.json")
            if os.path.exists(results_filepath) and os.path.getsize(results_filepath) > 10:
                with open(results_filepath, "r", encoding="utf-8") as f:
                    for line in f:
                        results_data = json.loads(line)
                        if results_data["Benchmark Success"] and ("Output" in results_data.keys()):
                            valid_results = True
            
            skip = False
            if valid_results:
                logger.info(f"Results file are existing for the config {mlperf_config_filepath}, skipping it")
                skip = True
                
            generated_configs.append((mlperf_config_filepath, skip))
            continue
        
        with open(mlperf_config_filepath, "w", encoding="utf-8") as f:
            f.write(json.dumps(mlperf_config, ensure_ascii=False, indent=4))
            
        logger.info(f"Saved MLPerf config file: {mlperf_config_filepath}")
        
        generated_configs.append((mlperf_config_filepath, False))
    
    return generated_configs

def generate_mmlu_input_config_files(working_dir: str, config: dict, input_config_template: dict, run_config_template: dict):
    if not os.path.exists(config["MMLUDataPath"]) or not os.listdir(config["MMLUDataPath"]):
        download_mmlu_test_data(config["MMLUDataPath"])
        
    generated_input_files = generate_mmlu_input_files(working_dir, input_config_template, run_config_template,
                                                      config["MMLUDataPath"], config["Subjects"],
                                                      config["DataSplitStep"], config["FewShotPromptsNumber"])
    
    return generated_input_files

def generate_tinymmlu_input_config_files(working_dir: str, config: dict, input_config_template: dict, run_config_template: dict):
    if config["HfToken"] is None:
        raise ValueError(f"For TinyMMLU you should specify huggingface token in HFToken field of config file")
    
    # HfFolder.save_token(config["HfToken"])
    local_path = hf_hub_download(
        repo_id="tinyBenchmarks/tinyMMLU",
        repo_type="dataset",
        filename="all/test-00000-of-00001.parquet",
        token=config["HfToken"]
    )
    df = pd.read_parquet(local_path)
    answers = ["A", "B", "C", "D"]
    
    tokenizer_config_extra = {"trust_remote_code": True}
    tokenizer_data_path = (
        run_config_template
        .get("Scenarios", [{}])[0]
        .get("Models", [{}])[0]
        .get("TokenizerPath", "")
    )
    execution_provider_name = (
        run_config_template
        .get("Scenarios", [{}])[0]
        .get("ExecutionProviders", [{}])[0]
        .get("Name", "")
        .lower()
    )
    model_name = (
        run_config_template
        .get("Scenarios", [{}])[0]
        .get("Name", "")
        .lower()
    )
    
    prompts_by_subject = {}
    if tokenizer_data_path:
        with TemporaryDirectory("temp_tinymmlu_tokenizer") as temp_tinymmlu_tokenizer_dir_path:
            temp_tokenizer_zip_path = os.path.join(temp_tinymmlu_tokenizer_dir_path, 'tokenizer.zip')
            logger.info("Downloading Tokenizer data")
            download_zip_with_requests(tokenizer_data_path, temp_tokenizer_zip_path)
            with zipfile.ZipFile(temp_tokenizer_zip_path, 'r') as zip_ref:
                zip_ref.extractall(temp_tinymmlu_tokenizer_dir_path)

            tokenizer = load_tokenizer(
                model_path=temp_tinymmlu_tokenizer_dir_path,
                tokenizer_config_extra=tokenizer_config_extra,
                eos_token_ids=None
            )
            if tokenizer.chat_template is None:
                if os.path.exists(os.path.join(temp_tinymmlu_tokenizer_dir_path, 'tokenizer_config.json')):
                    with open(os.path.join(temp_tinymmlu_tokenizer_dir_path, 'tokenizer_config.json'), 'r', encoding="utf-8") as f:
                        tokenizer_config = json.load(f)
                    chat_template = tokenizer_config.get("chat_template", None)
                    if chat_template is not None:
                        tokenizer.chat_template = chat_template
            kwargs = {}
            for i, row in df.iterrows():
                subject = row["subject"]
                if config["Subjects"] is not None and subject not in config["Subjects"]:
                    continue
                
                sys_prompt = (
                    f"The following are multiple choice questions about {subject}.\n"
                    "Answer the last question by selecting the correct option character, 'A', 'B', 'C' or 'D', do not add any other text.\n\n"
                )

                prompt = row["input_formatted"]
                answer = answers[row["answer"]]
                if (tokenizer.chat_template is not None) and \
                    (execution_provider_name == "mlx") and \
                    (model_name not in ["llama2", "phi3.5", "phi4"]):
                    messages = [
                        {"role": "user", "content": f"{sys_prompt}{prompt}"}
                    ]
                    prompt = tokenizer.apply_chat_template(
                        messages,
                        tokenize=False,
                        add_generation_prompt=True
                    )
                else:
                    if model_name == "llama2":
                        kwargs = {"max_length": 2}
                    prompt = f"{sys_prompt}{prompt}"
                        
                if subject not in prompts_by_subject:
                    prompts_by_subject[subject] = {
                        "prompts": [prompt],
                        "answers": [answer]
                    }
                else:
                    prompts_by_subject[subject]["prompts"].append(prompt)
                    prompts_by_subject[subject]["answers"].append(answer)
            
            saved_input_files = []
            for subject, data in prompts_by_subject.items():
                saved_input_files += save_input_files_and_answers(working_dir, subject, input_config_template,
                                                                data["prompts"], data["answers"], 
                                                                config["DataSplitStep"], **kwargs)
    else:
        raise ValueError(f"No TokenizerPath found in the config file !")
    
    return saved_input_files

def preprocess(config: dict, benchmark_type: str) -> list[str]:
    output_dir = os.path.join(config["OutputDir"], benchmark_type)
    run_id = get_run_id(output_dir, config["RunID"])

    if os.path.exists(output_dir):
        items = os.listdir(output_dir)
        if str(run_id) in items and os.path.isdir(os.path.join(output_dir, str(run_id))):
            logger.info("Continuing benchmark")

    working_dir = os.path.join(output_dir, str(run_id))
    logger.info(f"Input files will be saved in {working_dir}")
    os.makedirs(working_dir, exist_ok=True)
    
    if config["InputConfigPath"]:
        with open(config["InputConfigPath"], "r", encoding="utf-8") as f:
            input_config_template = json.load(f)
    else:
        input_config_template = config["InputConfigTemplate"]    
        
    if config["RunConfigPath"]:
        with open(config["RunConfigPath"], "r", encoding="utf-8") as f:
            run_config_template = json.load(f)
    else:
        run_config_template = config["RunConfigTemplate"]

    if benchmark_type == "mmlu":
        generated_input_files = generate_mmlu_input_config_files(working_dir, config, input_config_template, run_config_template)
    elif benchmark_type == "tinymmlu":
        generated_input_files = generate_tinymmlu_input_config_files(working_dir, config, input_config_template, run_config_template)
    else:
        raise ValueError(f"Wrong benchmark type {benchmark_type}")
        
    generated_configs = generate_mlperf_config_file(generated_input_files, run_config_template)
    
    logger.info("Finished generating config files")
    
    return generated_configs

def create_indexed_directory(base_name, root_path='.'):
    index = 0
    while True:
        dir_name = f"{base_name}_{index}"
        full_path = os.path.join(root_path, dir_name)
        if not os.path.exists(full_path):
            os.makedirs(full_path)
            print(f"Directory created: {full_path}")
            return full_path
        index += 1

def read_last_json_line(path):
    with open(path, 'r', encoding='utf-8') as f:
        last_line = None
        for line in f:
            line = line.strip()
            if line:
                last_line = line
        if last_line is None:
            raise ValueError("File is empty or contains only blank lines")
        return json.loads(last_line)

def run_mlperf(mlperf_program_path: str, config_paths: list[tuple[str, bool]], skip_failed_prompts) -> tuple[list[str], int]:
    """
    This function executes the MLPerf program with the specified configurations and handles the results.

    Args:
        mlperf_program_path (str): Path to the MLPerf program executable.
        config_paths (list[tuple[str, bool]]): List of tuples where each tuple contains the path to a configuration file and a boolean indicating whether to skip that configuration.
        skip_failed_prompts (_type_): If True, the function will skip prompts that failed during execution.

    Raises:
        Exception: If the MLPerf program returns a non-zero exit code or if the results.json file does not exist after execution.

    Returns:
        tuple[list[str], int]: A tuple containing a list of paths to the results.json files and the count of skipped prompts.
    """
    results_jsons = []
    skipped_prompts_count = 0
    for config_path, skip in tqdm.tqdm(config_paths):
        output_dir = os.path.dirname(config_path)
        results_path = os.path.join(output_dir, "results.json")
        
        if skip:
            results_jsons.append(results_path)
            continue
                
        mlperf_args = [mlperf_program_path, "--config", config_path, "--pause", "false", "--output-dir", output_dir]
        if skip_failed_prompts:
            mlperf_args.append("--skip-failed-prompts")

        process = subprocess.Popen(mlperf_args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                        
        for stdout_line in iter(process.stdout.readline, ""):
            logger.debug(stdout_line.strip())
        for stderr_line in iter(process.stderr.readline, ""):
            logger.error(stderr_line.strip())
        
        process.stdout.close()
        process.stderr.close()
        
        process.wait()

        if process.returncode == 0:
            if os.path.exists(results_path):
                results_data = read_last_json_line(results_path)
                skipped_prompts = results_data.get("Skipped Prompts", None)
                if skip_failed_prompts and not results_data["Benchmark Success"] and skipped_prompts is not None and len(skipped_prompts[0]) > 0:
                    skipped_prompts_count += len(skipped_prompts[0])
                    answers_file_path = os.path.join(output_dir, "answers.json")
                    if os.path.exists(answers_file_path):
                        answers_data = json.load(open(answers_file_path, "r", encoding="utf-8"))
                        for skipped_prompt_idx in skipped_prompts[0]:
                            answers_data["answers"].pop(skipped_prompt_idx)
                            
                        os.makedirs(os.path.join(output_dir, "updated_files"), exist_ok=True)
                        with open(os.path.join(output_dir, "updated_files", "answers.json"), "w", encoding="utf-8") as f:
                            f.write(json.dumps(answers_data, ensure_ascii=False, indent=4))
                        
                        updated_results_path = os.path.join(output_dir, "updated_files", "results.json")
                        shutil.copy(results_path, updated_results_path)
                        shutil.copy(os.path.join(output_dir, "mlperf_config.json"), os.path.join(output_dir, "updated_files", "mlperf_config.json"))
                        results_jsons.append(updated_results_path)
                else:
                    results_jsons.append(results_path)
            else:
                raise Exception(f"results.json does not exist: {results_path}")
        else:
            raise Exception(f"Non-zero return code. {process.returncode}. Here is the program arguments {mlperf_args}")
    
    return results_jsons, skipped_prompts_count


def postprocess(config: dict, results_jsons: list, elapsed_time: float, skipped_prompts_count: int):
    # assuming there is only one model per scenario
    correct_answers = {}
    if (not results_jsons):
        raise Exception(f"Couldn't find results.json in output_directories")

    mlperf_config_filepath = os.path.join(os.path.dirname(results_jsons[0]), "mlperf_config.json")
    with open(mlperf_config_filepath, "r", encoding="utf-8") as f:
        mlperf_config = json.load(f)
    scenarios = mlperf_config["Scenarios"]
    for scenario in scenarios:
        scenario_name = scenario["Name"]
        providers = scenario["ExecutionProviders"]
        for provider in providers:
            provider_name = provider["Name"]
            correct_answers[scenario_name + provider_name] = 0
    
    subjects = set()
    total_answers_per_subject = {}
    total_answers = 0
    
    for results_file_path in results_jsons:
        result_json = read_last_json_line(results_file_path)
        
        answers_file_path = os.path.join(os.path.dirname(results_file_path), "answers.json")
        with open(answers_file_path, "r", encoding="utf-8") as f:
            answers_file = json.load(f)
        
        answers = answers_file["answers"]
        subject_name = answers_file["subject"]
        
        answers_count = len(answers)
        total_answers += answers_count
        
        is_new_subject = not subject_name in subjects
        if is_new_subject:
            subjects.add(subject_name)
            total_answers_per_subject[subject_name] = answers_count
        else:
            total_answers_per_subject[subject_name] += answers_count

        scenario_name = result_json["Scenario Name"]
        provider_name = result_json["Execution Provider Name"]
        answer_key = scenario_name + provider_name
        answer_subject_key = answer_key + subject_name
        if is_new_subject:
            correct_answers[answer_subject_key] = 0
        
        if ("Output" in result_json.keys()):
            output = result_json["Output"]
            if len(output) == answers_count:
                for i in range(answers_count):
                    tmp_output = output[i].strip()
                    if len(tmp_output) > 0:
                        if(tmp_output[0].upper() == answers[i].upper()):
                            correct_answers[answer_key] += 1
                            correct_answers[answer_subject_key] += 1
            
                    #else:
                    #    logger.info("incorrect answer: " + output[i].strip() + " expected: " + answers[i])
            else:
                # fail all answers, todo: show error?
                pass
        else:
            logger.error(f"MLPerf benchmark failed. see {results_file_path}")
    
    output_json = {}
    output_json["TotalDuration"] = elapsed_time
    output_json["Scenarios"] = scenarios.copy()
    scenarios = output_json["Scenarios"]
    
    logger.info(f"Total duration: {elapsed_time:.2f} seconds")
    
    for scenario in scenarios:
        scenario_name = scenario["Name"]
        models = scenario["Models"]
        model = models[0]
        model["ScoreMMLU"] = []
        
        logger.info(f"Scenario: {scenario_name}, model: {model['ModelName']}")
        providers = scenario["ExecutionProviders"]
        for provider in providers:
            provider_name = provider["Name"]
            answer_key = scenario_name + provider_name
            correct_answer = correct_answers[answer_key]
            
            mmlu_score = correct_answer / total_answers * 100
            
            score = {}
            score["ExecutionProvider"] = provider_name
            score["Total"] = mmlu_score
            score["PerSubject"] = {}
            model["ScoreMMLU"].append(score)
            
            logger.info(f"Provider: {provider_name}")
            logger.info(f"  Correct answers {correct_answer} from {total_answers}")
            logger.info(f"  MMLU Score {mmlu_score:.2f}")
            
            for subject_name in subjects:
                answer_subject_key = answer_key + subject_name
                if answer_subject_key in correct_answers:
                    correct_answer = correct_answers[answer_subject_key]
                    total_per_subject = total_answers_per_subject[subject_name]
                    score_per_subject = correct_answer / total_per_subject * 100
                    
                    score["PerSubject"][subject_name] = score_per_subject
                    logger.info(f"  Subject: {subject_name}, MMLU Score {score_per_subject:.2f}")
    if skipped_prompts_count > 0:
        logger.warning(f"Skipped {skipped_prompts_count} prompts due to failed answers")
        output_json["SkippedPromptsCount"] = skipped_prompts_count
    
    # save output
    output_dir = config["OutputDir"]
    output_file_path = os.path.join(output_dir, "report.json")
    with open(output_file_path, "w", encoding="utf-8") as f:
        f.write(json.dumps(output_json, ensure_ascii=False, indent=4))


if __name__ == "__main__":
    args = parse_arguments()
    logger.remove()
    
    logger_format = "<green>{time:YYYY-MM-DD HH:mm:ss.SSS}</green> | " \
                    "<level>{level: <8}</level> | " \
                    "<cyan>{module}</cyan>:<cyan>{function}</cyan>:<cyan>{line}</cyan> - <level>{message}</level>"
    
    if not args.verbose:
        logger.add("mmlu.log", format=logger_format, level="INFO", rotation="10 MB")
        logger.add(sys.stdout, format=logger_format, level="INFO")
    else:
        logger.add("mmlu.log", format=logger_format, level="DEBUG", rotation="10 MB")
        logger.add(sys.stdout, format=logger_format, level="DEBUG")
    
    with open(args.config) as f:
        config = json.load(f)
    
    try:
        if not os.path.exists(config["MLPerfProgramPath"]):
            raise ValueError(f"{config['MLPerfProgramPath']} does not exist")
        if config["InputConfigTemplate"] is None and config["InputConfigPath"] is None:
            raise ValueError(f"Either 'InputConfigTemplate' or 'InputConfigPath' must be provided in the config file. Both cannot be null.")
        if config["RunConfigTemplate"] is None and config["RunConfigPath"] is None:
            raise ValueError(f"Either 'RunConfigTemplate' or 'RunConfigPath' must be provided in the config file. Both cannot be null.")
    
        generated_mlperf_config_paths = preprocess(config, args.type)
        
        start_time = time.time()
        results_jsons, skipped_prompts_count = run_mlperf(config["MLPerfProgramPath"], generated_mlperf_config_paths, args.skip_failed_prompts)
        elapsed_time = time.time() - start_time
        
        postprocess(config, results_jsons, elapsed_time, skipped_prompts_count)
        
    except Exception as e:
        logger.error(e)
        raise e