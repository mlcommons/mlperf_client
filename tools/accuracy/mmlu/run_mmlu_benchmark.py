from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import sys
import tarfile
import time
from pathlib import Path
from tempfile import TemporaryDirectory

import pandas as pd
import tqdm
from huggingface_hub import hf_hub_download
from loguru import logger

_accuracy_root = Path(__file__).resolve().parent.parent
if str(_accuracy_root) not in sys.path:
    sys.path.insert(0, str(_accuracy_root))

from mlperf_common import (
    check_existing_config_file,
    download_tokenizer,
    download_zip_with_requests,
    find_existing_results,
    format_chat_prompts,
    generate_mlperf_config_file,
    get_run_id,
    read_last_json_line,
    run_mlperf,
)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="This script is used to run MMLU benchmark using mlperf")
    parser.add_argument("-c", "--config", type=str, required=True,
                        help="Path to the config file (e.g. tools/accuracy/mmlu/mmlu-config.json)")
    parser.add_argument("-r", "--run-config", type=str,
                        help="Path to a vendor default config (overrides RunConfigPath in the config file)")
    parser.add_argument("-p", "--program", type=str,
                        help="Path to mlperf-windows.exe (overrides MLPerfProgramPath in the config file)")
    parser.add_argument("-v", "--verbose", action="store_true", help="If enabled will run the script in verbose mode")
    parser.add_argument("-t", "--type", type=str, default="mmlu", choices=["mmlu", "tinymmlu"], help="Type of the benchmark to run")
    parser.add_argument("-s", "--skip-failed-prompts", action="store_true", 
                        help="If enabled will skip the failed prompts")
    parser.add_argument("--postprocess-only", action="store_true", 
                        help="If enabled will only run postprocessing on existing results")
    parser.add_argument("--run-id", type=int, 
                        help="Run ID to postprocess (required when using --postprocess-only)")
    
    return parser.parse_args()


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


_VERBAL_ANSWER_RE = re.compile(
    r"The\s+correct\s+answer\s+is\s*['\"]?([ABCD])['\"]?\s*\.?",
    re.IGNORECASE,
)


def mmlu_parse_model_choice_detail(
    output_text: str,
    answer_format: str,
    *,
    verbal_lenient: bool = False,
) -> tuple[str | None, str]:
    """Return (A/B/C/D or None, parse_source).

    ``parse_source`` is ``letter``, ``regex``, ``fallback_abcd``, or ``none``.
    For ``verbal`` + ``verbal_lenient``, try the required phrase first, then first A-D in the string.
    """
    fmt = (answer_format or "letter").lower()
    s = (output_text or "").strip()
    if not s:
        return None, "none"
    if fmt == "verbal":
        m = _VERBAL_ANSWER_RE.search(s)
        if m:
            return m.group(1).upper(), "regex"
        if verbal_lenient:
            for ch in s:
                cu = ch.upper()
                if cu in ("A", "B", "C", "D"):
                    return cu, "fallback_abcd"
        return None, "none"
    if len(s) > 0:
        return s[0].upper(), "letter"
    return None, "none"


def mmlu_parse_model_choice(
    output_text: str,
    answer_format: str,
    *,
    verbal_lenient: bool = False,
) -> str | None:
    """Extract A/B/C/D from model output. ``answer_format`` is ``letter`` or ``verbal``."""
    pred, _src = mmlu_parse_model_choice_detail(
        output_text, answer_format, verbal_lenient=verbal_lenient
    )
    return pred


def decode_unicode_escapes_safe(text):
    def replace_match(match):
        try:
            return chr(int(match.group(1), 16))
        except:
            return match.group(0)  # leave as-is if invalid
    return re.sub(r'\\u([0-9a-fA-F]{4})', replace_match, text)


def format_prompt(
    df,
    idx,
    include_answer=True,
    add_section_title=False,
    *,
    verbal_style: bool = False,
):
    """Build one MMLU block (question + A-D).

    ``verbal_style`` (used with ``VerbalAnswerFormat``): few-shots and answers use
    ``The correct answer is X.`` instead of ``Answer:\\n`` + a lone letter, so the
    model is not primed to emit a single character first.
    """
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
    if verbal_style:
        if include_answer:
            raw = str(df.iloc[idx, k + 1]).strip().upper()
            letter = raw[0] if raw and raw[0] in "ABCD" else raw
            prompt += f"\nThe correct answer is {letter}.\n\n"
        else:
            prompt += "\nThe correct answer is "
        return prompt
    prompt += "\nAnswer:\n"
    if include_answer:
        prompt += "{}\n\n".format(df.iloc[idx, k + 1])
    return prompt


# Function to save prompts and answers to files
def save_chunk(
    input_dir: str,
    subject: str,
    input_config_template: dict,
    prompts_chunk: list[str],
    answers_chunk: list[str],
    file_suffix: str = "",
    *,
    answer_format: str = "letter",
    verbal_scoring: str | None = None,
    apply_chat_template: bool = False,
    enable_thinking: bool | None = None,
    **kwargs,
) -> str:
    input_config = input_config_template.copy()
    if apply_chat_template:
        input_config["apply_chat_template"] = True
        if enable_thinking is not None:
            input_config["enable_thinking"] = enable_thinking
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
        }
    input_config["prompts"] = prompts_chunk
    answers_dict = {"answers": answers_chunk, "subject": subject, "answer_format": answer_format}
    if answer_format == "verbal" and verbal_scoring in ("strict", "lenient"):
        answers_dict["verbal_scoring"] = verbal_scoring
    
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


def save_input_files_and_answers(
    input_dir: str,
    subject: str,
    input_config_template: dict,
    prompts: list[str],
    answers: list[str],
    data_split_step: int | None,
    *,
    answer_format: str = "letter",
    verbal_scoring: str | None = None,
    apply_chat_template: bool = False,
    enable_thinking: bool | None = None,
    **kwargs,
) -> list[str]:
    saved_input_files = []

    if data_split_step is None:
        saved_input_files.append(
            save_chunk(
                input_dir,
                subject,
                input_config_template,
                prompts,
                answers,
                answer_format=answer_format,
                verbal_scoring=verbal_scoring,
                apply_chat_template=apply_chat_template,
                enable_thinking=enable_thinking,
                **kwargs,
            )
        )
    else:
        for i in range(0, len(prompts), data_split_step):
            chunk_prompts = prompts[i : i + data_split_step]
            chunk_answers = answers[i : i + data_split_step]
            saved_input_files.append(
                save_chunk(
                    input_dir,
                    subject,
                    input_config_template,
                    chunk_prompts,
                    chunk_answers,
                    file_suffix=f"_{i//data_split_step}",
                    answer_format=answer_format,
                    verbal_scoring=verbal_scoring,
                    apply_chat_template=apply_chat_template,
                    enable_thinking=enable_thinking,
                    **kwargs,
                )
            )
            
    return saved_input_files


def generate_mmlu_input_files(
    input_dir: str,
    input_config_template: dict,
    run_config_template: dict,
    mmlu_data_path: str,
    subjects: list[str] | None,
    data_split_step: int | None,
    few_shot_prompts_number: int,
    *,
    verbal_answer_format: bool = False,
    verbal_answer_max_length: int = 96,
    verbal_answer_lenient: bool = False,
    system_prompt_template: str | None = None,
    prompt_format: str = "auto",
    enable_thinking: bool | None = None,
) -> list[str]:
    saved_input_files = []

    tokenizer, _tok_temp_dir = download_tokenizer(run_config_template, base_dir_context=config)

    model_name = (
        run_config_template
        .get("Scenarios", [{}])[0]
        .get("Name", "")
        .lower()
    )

    if verbal_answer_format:
        default_sys_template = (
            "The following are multiple choice questions about {subject}.\n"
            "For the last question only, respond with exactly one sentence of the form:\n"
            "The correct answer is X\n"
            "where X is exactly one of the letters A, B, C, or D. "
            "Use that wording exactly; do not add other sentences or text before or after.\n\n"
        )
    else:
        default_sys_template = (
            "The following are multiple choice questions about {subject}.\n"
            "Answer the last question by selecting the correct option character, "
            "'A', 'B', 'C' or 'D', do not add any other text.\n\n"
        )
    sys_template = system_prompt_template or default_sys_template

    for filename in os.listdir(os.path.join(mmlu_data_path, "test")):
        subject_name = " ".join(filename.split("_")[:-1])
        if subjects is not None and subject_name not in subjects:
            continue
        logger.info(f"Processing input for subject {subject_name}")

        subject_test_df = pd.read_csv(
            os.path.join(mmlu_data_path, "test", filename),
            names=["question", "A", "B", "C", "D", "answer"]
        )
        subject_dev_df = pd.read_csv(
            os.path.join(mmlu_data_path, "dev", "_".join(subject_name.split() + ["dev.csv"])),
            names=["question", "A", "B", "C", "D", "answer"]
        )

        sys_prompt = sys_template.format(subject=subject_name)

        few_shot_text = ""
        for i in range(few_shot_prompts_number):
            few_shot_text += format_prompt(
                subject_dev_df,
                i,
                include_answer=True,
                verbal_style=verbal_answer_format,
            )

        prompts = []
        answers = []
        kwargs: dict = {}
        if verbal_answer_format:
            kwargs["max_length"] = max(8, int(verbal_answer_max_length))
        elif model_name == "llama2":
            kwargs["max_length"] = 2

        for i in range(subject_test_df.shape[0]):
            user_content = (
                f"{few_shot_text}"
                f"{format_prompt(subject_test_df, i, include_answer=False, verbal_style=verbal_answer_format)}"
            )
            user_content = decode_unicode_escapes_safe(user_content)
            prompts.append(user_content)
            answers.append(subject_test_df.loc[i, "answer"])

        fmt = (prompt_format or "auto").strip().lower()
        if fmt == "app":
            # Delegate chat templating to the C++ app via object-form prompts.
            prompts = [{"system": sys_prompt, "user": p} for p in prompts]
        else:
            prompts = format_chat_prompts(
                tokenizer, prompts, system_prompt=sys_prompt, prompt_format=prompt_format,
                enable_thinking=enable_thinking,
            )

        afmt = "verbal" if verbal_answer_format else "letter"
        vscore = (
            ("lenient" if verbal_answer_lenient else "strict")
            if verbal_answer_format
            else None
        )
        saved_input_files += save_input_files_and_answers(
            input_dir,
            subject_name,
            input_config_template,
            prompts,
            answers,
            data_split_step,
            answer_format=afmt,
            verbal_scoring=vscore,
            apply_chat_template=(fmt == "app"),
            enable_thinking=enable_thinking,
            **kwargs,
        )

    return saved_input_files


def generate_mmlu_input_config_files(
    working_dir: str, config: dict, input_config_template: dict, run_config_template: dict,
    system_prompt_template: str | None = None, prompt_format: str = "auto",
    enable_thinking: bool | None = None,
):
    if not os.path.exists(config["MMLUDataPath"]) or not os.listdir(config["MMLUDataPath"]):
        download_mmlu_test_data(config["MMLUDataPath"])
        
    generated_input_files = generate_mmlu_input_files(
        working_dir,
        input_config_template,
        run_config_template,
        config["MMLUDataPath"],
        config["Subjects"],
        config["DataSplitStep"],
        config["FewShotPromptsNumber"],
        verbal_answer_format=bool(config.get("VerbalAnswerFormat", False)),
        verbal_answer_max_length=int(config.get("VerbalAnswerMaxLength", 96)),
        verbal_answer_lenient=bool(config.get("VerbalAnswerLenientScoring", False)),
        system_prompt_template=system_prompt_template,
        prompt_format=prompt_format,
        enable_thinking=enable_thinking,
    )

    return generated_input_files

def _resolve_hf_token(config: dict):
    """Dataset token: config HfToken, else HF_TOKEN / HUGGING_FACE_HUB_TOKEN env, else None (public)."""
    t = config.get("HfToken")
    if t is not None and str(t).strip() != "":
        return t
    return os.environ.get("HF_TOKEN") or os.environ.get("HUGGING_FACE_HUB_TOKEN") or None


def generate_tinymmlu_input_config_files(
    working_dir: str, config: dict, input_config_template: dict, run_config_template: dict,
    system_prompt_template: str | None = None, prompt_format: str = "auto",
    enable_thinking: bool | None = None,
):
    hf_token = _resolve_hf_token(config)
    if hf_token is None:
        logger.info("TinyMMLU: no HfToken in config or env; using anonymous Hub access (public datasets only)")

    local_path = hf_hub_download(
        repo_id="tinyBenchmarks/tinyMMLU",
        repo_type="dataset",
        filename="all/test-00000-of-00001.parquet",
        token=hf_token
    )
    df = pd.read_parquet(local_path)
    answer_letters = ["A", "B", "C", "D"]

    tokenizer, _tok_temp_dir = download_tokenizer(run_config_template, base_dir_context=config)

    model_name = (
        run_config_template
        .get("Scenarios", [{}])[0]
        .get("Name", "")
        .lower()
    )

    verbal_answer_format = bool(config.get("VerbalAnswerFormat", False))
    verbal_answer_max_length = int(config.get("VerbalAnswerMaxLength", 96))
    verbal_answer_lenient = bool(config.get("VerbalAnswerLenientScoring", False))

    if verbal_answer_format:
        default_sys_template = (
            "The following are multiple choice questions about {subject}.\n"
            "For the last question only, respond with exactly one sentence of the form:\n"
            "The correct answer is X\n"
            "where X is exactly one of the letters A, B, C, or D. "
            "Use that wording exactly; do not add other sentences or text before or after.\n\n"
        )
    else:
        default_sys_template = (
            "The following are multiple choice questions about {subject}.\n"
            "Answer the last question by selecting the correct option character, "
            "'A', 'B', 'C' or 'D', do not add any other text.\n\n"
        )
    sys_template = system_prompt_template or default_sys_template

    kwargs: dict = {}
    if verbal_answer_format:
        kwargs["max_length"] = max(8, verbal_answer_max_length)
    elif model_name == "llama2":
        kwargs["max_length"] = 2

    prompts_by_subject: dict[str, dict] = {}
    for _, row in df.iterrows():
        subject = row["subject"]
        if config["Subjects"] is not None and subject not in config["Subjects"]:
            continue

        user_content = row["input_formatted"]
        answer = answer_letters[row["answer"]]

        if subject not in prompts_by_subject:
            prompts_by_subject[subject] = {"prompts": [], "answers": []}
        prompts_by_subject[subject]["prompts"].append(user_content)
        prompts_by_subject[subject]["answers"].append(answer)

    afmt = "verbal" if verbal_answer_format else "letter"
    vscore = (
        ("lenient" if verbal_answer_lenient else "strict")
        if verbal_answer_format
        else None
    )
    fmt = (prompt_format or "auto").strip().lower()
    saved_input_files = []
    for subject, data in prompts_by_subject.items():
        sys_prompt = sys_template.format(subject=subject)
        if fmt == "app":
            # Delegate chat templating to the C++ app via object-form prompts.
            formatted_prompts = [{"system": sys_prompt, "user": p} for p in data["prompts"]]
        else:
            formatted_prompts = format_chat_prompts(
                tokenizer, data["prompts"], system_prompt=sys_prompt, prompt_format=prompt_format,
                enable_thinking=enable_thinking,
            )
        saved_input_files += save_input_files_and_answers(
            working_dir,
            subject,
            input_config_template,
            formatted_prompts,
            data["answers"],
            config["DataSplitStep"] or 1,
            answer_format=afmt,
            verbal_scoring=vscore,
            apply_chat_template=(fmt == "app"),
            enable_thinking=enable_thinking,
            **kwargs,
        )

    return saved_input_files

def preprocess(config: dict, benchmark_type: str) -> list[tuple[str, bool]]:
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

    system_prompt_template = config.get("SystemPrompt")
    prompt_format = str(config.get("PromptFormat", "raw")).strip().lower()
    enable_thinking = config.get("EnableThinking", False)

    if benchmark_type == "mmlu":
        generated_input_files = generate_mmlu_input_config_files(
            working_dir, config, input_config_template, run_config_template,
            system_prompt_template=system_prompt_template, prompt_format=prompt_format,
            enable_thinking=enable_thinking,
        )
    elif benchmark_type == "tinymmlu":
        generated_input_files = generate_tinymmlu_input_config_files(
            working_dir, config, input_config_template, run_config_template,
            system_prompt_template=system_prompt_template, prompt_format=prompt_format,
            enable_thinking=enable_thinking,
        )
    else:
        raise ValueError(f"Wrong benchmark type {benchmark_type}")
    generated_configs = generate_mlperf_config_file(
            generated_input_files, run_config_template, base_dir_context=config
        )
    
    logger.info("Finished generating config files")
    
    return generated_configs


def postprocess(config: dict, results_jsons: list, elapsed_time: float, skipped_prompts_count: int):
    # assuming there is only one model per scenario
    correct_answers = {}
    if (not results_jsons):
        raise Exception(f"Couldn't find results.json in output_directories")

    mlperf_config_filepath = os.path.join(os.path.dirname(results_jsons[0]), "mlperf_config.json")
    with open(mlperf_config_filepath, "r", encoding="utf-8") as f:
        mlperf_config = json.load(f)
    scenarios = mlperf_config["Scenarios"]
    
    # Initialize keys from the actual results files to avoid case mismatches
    # The results.json uses the actual scenario/provider names as reported by MLPerf
    for results_file_path in results_jsons:
        result_json = read_last_json_line(results_file_path)
        scenario_name = result_json["Scenario Name"]
        provider_name = result_json["Execution Provider Name"]
        answer_key = scenario_name + provider_name
        if answer_key not in correct_answers:
            correct_answers[answer_key] = 0
    
    subjects = set()
    total_answers_per_subject = {}
    total_answers = 0
    verbal_diag = {
        "verbal_items": 0,
        "correct_via_regex": 0,
        "correct_via_fallback_abcd": 0,
        "incorrect_with_prediction": 0,
        "no_prediction": 0,
    }

    for results_file_path in results_jsons:
        result_json = read_last_json_line(results_file_path)
        
        answers_file_path = os.path.join(os.path.dirname(results_file_path), "answers.json")
        with open(answers_file_path, "r", encoding="utf-8") as f:
            answers_file = json.load(f)
        
        answers = answers_file["answers"]
        subject_name = answers_file["subject"]
        answer_format = answers_file.get("answer_format", "letter")
        vs_mode = answers_file.get("verbal_scoring")
        if answer_format == "verbal":
            if vs_mode == "lenient":
                verbal_lenient = True
            elif vs_mode == "strict":
                verbal_lenient = False
            else:
                verbal_lenient = bool(config.get("VerbalAnswerLenientScoring", False))
        else:
            verbal_lenient = False
        
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
                    gold = answers[i].strip().upper()
                    pred, src = mmlu_parse_model_choice_detail(
                        tmp_output,
                        answer_format,
                        verbal_lenient=verbal_lenient,
                    )
                    if answer_format == "verbal":
                        verbal_diag["verbal_items"] += 1
                    if pred is not None and pred == gold:
                        correct_answers[answer_key] += 1
                        correct_answers[answer_subject_key] += 1
                        if answer_format == "verbal":
                            if src == "regex":
                                verbal_diag["correct_via_regex"] += 1
                            elif src == "fallback_abcd":
                                verbal_diag["correct_via_fallback_abcd"] += 1
                    elif answer_format == "verbal":
                        if pred is None:
                            verbal_diag["no_prediction"] += 1
                        else:
                            verbal_diag["incorrect_with_prediction"] += 1
            
                    #else:
                    #    logger.info("incorrect answer: " + output[i].strip() + " expected: " + answers[i])
            else:
                logger.warning(f"No output entries in {results_file_path}; all answers marked incorrect")
        else:
            logger.error(f"MLPerf benchmark failed. see {results_file_path}")
    
    output_json = {}
    output_json["TotalDuration"] = elapsed_time
    output_json["Scenarios"] = scenarios.copy()
    scenarios = output_json["Scenarios"]
    
    logger.info(f"Total duration: {elapsed_time:.2f} seconds")

    if verbal_diag.get("verbal_items", 0) > 0:
        n = verbal_diag["verbal_items"]
        logger.info(
            "Verbal answer diagnostics (this run): "
            f"items={n}, "
            f"correct_via_regex={verbal_diag['correct_via_regex']}, "
            f"correct_via_first_abcd={verbal_diag['correct_via_fallback_abcd']}, "
            f"incorrect_with_prediction={verbal_diag['incorrect_with_prediction']}, "
            f"no_prediction={verbal_diag['no_prediction']}"
        )

    # Build a map of actual scenario+provider keys from correct_answers
    # to handle case mismatches between config and results
    actual_keys = {}
    for key in correct_answers.keys():
        if not any(subject in key for subject in subjects):  # Only base keys, not subject-specific ones
            actual_keys[key.lower()] = key
    
    for scenario in scenarios:
        scenario_name = scenario["Name"]
        models = scenario["Models"]
        model = models[0]
        model["ScoreMMLU"] = []
        
        logger.info(f"Scenario: {scenario_name}, model: {model['ModelName']}")
        providers = scenario["ExecutionProviders"]
        for provider in providers:
            provider_name = provider["Name"]
            config_key = scenario_name + provider_name
            # Use case-insensitive lookup to find the actual key used in results
            answer_key = actual_keys.get(config_key.lower(), config_key)
            
            if answer_key not in correct_answers:
                logger.warning(f"No results found for {answer_key}, skipping")
                continue
                
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

    if verbal_diag.get("verbal_items", 0) > 0:
        output_json["VerbalDiagnostics"] = verbal_diag

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

    if args.run_config:
        config["RunConfigPath"] = args.run_config
        config["RunConfigTemplate"] = None

    if args.program:
        config["MLPerfProgramPath"] = args.program

    try:
        if args.postprocess_only:
            if args.run_id is None:
                raise ValueError("--run-id is required when using --postprocess-only")
            
            logger.info(f"Running in postprocess-only mode for run ID {args.run_id}")
            
            # Find existing results for the specified run ID
            results_jsons = find_existing_results(config["OutputDir"], args.type, args.run_id)
            
            # Run postprocessing with dummy values for elapsed_time and skipped_prompts_count
            # since we don't have the original timing information
            postprocess(config, results_jsons, 0.0, 0)
            
        else:
            # Normal full pipeline execution
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

