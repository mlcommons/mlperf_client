"""Shared helpers for MLPerf-driven accuracy benchmarks (MMLU, IFEval, etc.)."""

from __future__ import annotations

import copy
import json
import os
import shutil
import subprocess
import zipfile
from pathlib import Path
from tempfile import TemporaryDirectory
from urllib.parse import urlparse, unquote

import requests
import tqdm
from loguru import logger


def resolve_file_uri(url, base_dir=None):
    """Resolve a file:// URI to an absolute local path, or None for non-file URIs."""
    parsed = urlparse(url)
    if parsed.scheme != "file":
        return None

    raw_path = url[len("file://") :] if url.startswith("file://") else url[len("file:") :]
    raw_path = unquote(raw_path)

    if os.name == "nt" and len(raw_path) > 2 and raw_path[0] == "/" and raw_path[2] == ":":
        raw_path = raw_path[1:]

    local_path = os.path.normpath(raw_path)

    if not os.path.isabs(local_path):
        if base_dir:
            local_path = os.path.normpath(os.path.join(base_dir, local_path))
        else:
            local_path = os.path.abspath(local_path)

    return local_path


def to_absolute_file_uri(url, base_dir=None):
    """Convert a relative file:// URI to absolute. Non-file URIs pass through unchanged."""
    local_path = resolve_file_uri(url, base_dir=base_dir)
    if local_path is None:
        return url
    return "file://" + local_path


def get_base_dir(config_template, fallback_template=None):
    """Return resolved BaseDir, or infer it from MLPerfProgramPath when available."""

    def _resolve_from_template(template):
        if not isinstance(template, dict):
            return None
        raw = template.get("SystemConfig", {}).get("BaseDir", "")
        if raw:
            return resolve_file_uri(raw)

        mlperf_program_path = template.get("MLPerfProgramPath", "")
        if not mlperf_program_path:
            return None

        if isinstance(mlperf_program_path, str) and mlperf_program_path.startswith("file:"):
            resolved_program_path = resolve_file_uri(mlperf_program_path)
        else:
            resolved_program_path = os.path.normpath(str(mlperf_program_path))
            if not os.path.isabs(resolved_program_path):
                resolved_program_path = os.path.abspath(resolved_program_path)

        return os.path.dirname(resolved_program_path) if resolved_program_path else None

    return _resolve_from_template(config_template) or _resolve_from_template(fallback_template)


def download_zip_with_requests(url, download_path):
    try:
        Path(download_path).parent.mkdir(parents=True, exist_ok=True)

        parsed_url = urlparse(url)
        local_file_path = resolve_file_uri(url)

        if local_file_path is not None:
            if not os.path.exists(local_file_path):
                raise FileNotFoundError(f"Local file not found: {local_file_path}")
            shutil.copy2(local_file_path, download_path)
            print(
                f"Successfully copied {os.path.getsize(download_path)} bytes from {local_file_path} to: {download_path}"
            )

        elif parsed_url.scheme in ["http", "https"]:
            headers = {
                "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"
            }
            response = requests.get(url, headers=headers, stream=True)
            response.raise_for_status()
            with open(download_path, "wb") as f:
                for chunk in response.iter_content(chunk_size=8192):
                    f.write(chunk)
            print(f"Successfully downloaded {os.path.getsize(download_path)} bytes to: {download_path}")

        else:
            raise ValueError(
                f"Unsupported URL scheme: {parsed_url.scheme}. Only 'http', 'https', and 'file' are supported."
            )

    except requests.exceptions.HTTPError as e:
        print(f"HTTP Error: {e}")
        raise
    except FileNotFoundError as e:
        print(f"File Error: {e}")
        raise
    except Exception as e:
        print(f"Error: {e}")
        raise


def check_existing_config_file(file_path: str, config: dict, raise_error: bool = False) -> bool:
    if os.path.exists(file_path):
        with open(file_path, "r", encoding="utf-8") as f:
            existing_config = json.load(f)
        if existing_config != config:
            logger.error(f"Existing config {file_path}:\n{existing_config}\n\nGenerated config:\n{config}\n")

            if raise_error:
                raise ValueError(
                    f"The existing config and generated config are different {file_path}."
                    "If you want to run new experiment please set RunID to 'null' in the benchmark config."
                )

        logger.info(f"{file_path} is existing")
        return True
    return False


def find_latest_run_id(output_dir: str) -> int | None:
    """Find the highest integer-named subfolder under output_dir, or None if none exist."""
    if not os.path.isdir(output_dir):
        return None
    ids = [int(d) for d in os.listdir(output_dir)
           if d.isdigit() and os.path.isdir(os.path.join(output_dir, d))]
    return max(ids) if ids else None


def make_cont_config(config_path: str, run_id: int) -> str:
    """Create a _cont.json config with RunID set to continue a failed/interrupted run."""
    cont_path = config_path.replace(".json", "_cont.json")
    with open(config_path, "r", encoding="utf-8") as f:
        config = json.load(f)
    config["RunID"] = run_id
    with open(cont_path, "w", encoding="utf-8") as f:
        json.dump(config, f, ensure_ascii=False, indent=4)
    print(f"Created continuation config: {os.path.basename(cont_path)} (RunID={run_id})")
    return cont_path


def delete_cont_config(config_path: str) -> None:
    """Delete the _cont.json counterpart of a config file, if it exists."""
    cont_path = config_path.replace(".json", "_cont.json")
    if os.path.isfile(cont_path):
        os.remove(cont_path)
        print(f"Deleted continuation config: {os.path.basename(cont_path)}")


def get_run_id(input_files_dir: str, config_run_id: int | None) -> int:
    run_id = None
    if config_run_id is None:
        if os.path.exists(input_files_dir):
            items = os.listdir(input_files_dir)
            ids = [
                int(item)
                for item in items
                if item.isdigit() and os.path.isdir(os.path.join(input_files_dir, item))
            ]
            if ids:
                run_id = max(ids) + 1
            else:
                run_id = 1
        else:
            run_id = 1
    elif config_run_id > 0:
        run_id = config_run_id
    else:
        raise ValueError(
            f"RunID in the config file can be null or any positive integer, but it is {config_run_id!r}"
        )

    logger.info(f"Run ID is {run_id}")
    return run_id


def _write_stub_results_json_for_failed_job(output_dir: str, prompts_number: int) -> None:
    """Minimal results line so resume can skip the job (IFEval uses continue_on_job_failure)."""
    stub = {"Benchmark Success": False, "Output": [""] * prompts_number}
    results_path = os.path.join(output_dir, "results.json")
    with open(results_path, "w", encoding="utf-8") as f:
        f.write(json.dumps(stub, ensure_ascii=False) + "\n")


def _prompt_count_from_job_dir(output_dir: str) -> int:
    inp = os.path.join(output_dir, "input_file.json")
    if not os.path.isfile(inp):
        return 1
    with open(inp, encoding="utf-8") as f:
        return len(json.load(f).get("prompts", []))


def generate_mlperf_config_file(
    input_files: list[str],
    config_template: dict,
    *,
    base_dir_context: dict | None = None,
    skip_if_results_match_prompt_count: bool = False,
):
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

        mlperf_config = copy.deepcopy(config_template)
        base_dir = get_base_dir(config_template, fallback_template=base_dir_context)

        for i in range(len(mlperf_config["Scenarios"])):
            mlperf_config["Scenarios"][i]["InputFilePath"] = ["file://" + input_file_path]
            mlperf_config["Scenarios"][i]["ResultsVerificationFile"] = "file://" + dummy_results_verification_filepath
            mlperf_config["Scenarios"][i]["Iterations"] = 1
            mlperf_config["Scenarios"][i]["WarmUp"] = 0
            mlperf_config["Scenarios"][i]["Delay"] = 0

            if not base_dir:
                for model in mlperf_config["Scenarios"][i].get("Models", []):
                    for key, value in model.items():
                        if isinstance(value, str) and value.startswith("file:"):
                            model[key] = to_absolute_file_uri(value)

        if check_existing_config_file(mlperf_config_filepath, mlperf_config):
            valid_results = False
            results_filepath = os.path.join(input_dir, "results.json")
            if os.path.exists(results_filepath) and os.path.getsize(results_filepath) > 10:
                with open(results_filepath, "r", encoding="utf-8") as f:
                    for line in f:
                        results_data = json.loads(line)
                        outs = results_data.get("Output")
                        if not isinstance(outs, list) or len(outs) != prompts_number:
                            continue
                        if results_data.get("Benchmark Success"):
                            valid_results = True
                            break
                        if skip_if_results_match_prompt_count:
                            valid_results = True
                            break

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


def read_last_json_line(path):
    with open(path, "r", encoding="utf-8") as f:
        last_line = None
        for line in f:
            line = line.strip()
            if line:
                last_line = line
        if last_line is None:
            raise ValueError("File is empty or contains only blank lines")
        return json.loads(last_line)


def find_existing_results(output_dir: str, benchmark_type: str, run_id: int) -> list[str]:
    results_jsons = []
    run_dir = os.path.join(output_dir, benchmark_type, str(run_id))

    if not os.path.exists(run_dir):
        raise ValueError(f"Run directory does not exist: {run_dir}")

    for root, dirs, files in os.walk(run_dir):
        if "results.json" in files:
            results_path = os.path.join(root, "results.json")
            try:
                read_last_json_line(results_path)
                results_jsons.append(results_path)
            except Exception as e:
                logger.warning(f"Skipping invalid results file {results_path}: {e}")

    if not results_jsons:
        raise ValueError(f"No valid results.json files found in {run_dir}")

    logger.info(f"Found {len(results_jsons)} results files for run ID {run_id}")
    return results_jsons


def run_mlperf(
    mlperf_program_path: str,
    config_paths: list[tuple[str, bool]],
    skip_failed_prompts: bool,
    continue_on_job_failure: bool = False,
) -> tuple[list[str], int]:
    results_jsons = []
    skipped_prompts_count = 0
    for config_path, skip in tqdm.tqdm(config_paths):
        output_dir = os.path.dirname(config_path)
        results_path = os.path.join(output_dir, "results.json")

        if skip:
            results_jsons.append(results_path)
            continue

        mlperf_args = [
            mlperf_program_path,
            "--config",
            config_path,
            "--pause",
            "false",
            "--output-dir",
            output_dir,
        ]
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

        if process.returncode != 0:
            msg = f"Non-zero return code {process.returncode}. Args: {mlperf_args}"
            if continue_on_job_failure:
                n = _prompt_count_from_job_dir(output_dir)
                _write_stub_results_json_for_failed_job(output_dir, n)
                logger.error(f"{output_dir}: {msg} - wrote stub results.json, continuing")
                continue
            raise Exception(msg)

        if not os.path.exists(results_path):
            msg = f"results.json does not exist: {results_path}"
            if continue_on_job_failure:
                n = _prompt_count_from_job_dir(output_dir)
                _write_stub_results_json_for_failed_job(output_dir, n)
                logger.error(f"{output_dir}: {msg} - wrote stub results.json, continuing")
                continue
            raise Exception(msg)

        try:
            results_data = read_last_json_line(results_path)
        except Exception as e:
            msg = f"Invalid or empty results.json: {e}"
            if continue_on_job_failure:
                n = _prompt_count_from_job_dir(output_dir)
                _write_stub_results_json_for_failed_job(output_dir, n)
                logger.error(f"{output_dir}: {msg} - wrote stub results.json, continuing")
                continue
            raise

        skipped_prompts = results_data.get("Skipped Prompts", None)
        if (
            skip_failed_prompts
            and not results_data["Benchmark Success"]
            and skipped_prompts is not None
            and len(skipped_prompts[0]) > 0
        ):
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
                shutil.copy(
                    os.path.join(output_dir, "mlperf_config.json"),
                    os.path.join(output_dir, "updated_files", "mlperf_config.json"),
                )
                results_jsons.append(updated_results_path)
            else:
                # No answers.json (e.g. image-gen jobs) - nothing to trim, keep the raw results.json.
                results_jsons.append(results_path)
        else:
            results_jsons.append(results_path)

    return results_jsons, skipped_prompts_count


def download_tokenizer(
    run_config_template: dict, 
    base_dir: str | None = None,
    base_dir_context: dict | None = None):
    """Download and load a tokenizer from the TokenizerPath in a run config template.

    Returns (tokenizer, temp_dir_path). The caller should keep temp_dir_path alive
    for the lifetime of the tokenizer (or copy what it needs).
    """
    from tokenizer_utils import load_tokenizer

    tokenizer_url = (
        run_config_template
        .get("Scenarios", [{}])[0]
        .get("Models", [{}])[0]
        .get("TokenizerPath", "")
    )
    if not tokenizer_url:
        raise ValueError("No TokenizerPath found in the run config template")

    if base_dir is None:
        base_dir = get_base_dir(run_config_template, fallback_template=base_dir_context)
    tokenizer_url = to_absolute_file_uri(tokenizer_url, base_dir=base_dir)

    temp_dir = TemporaryDirectory(prefix="mlperf_tokenizer_")
    temp_dir_path = temp_dir.name
    temp_zip_path = os.path.join(temp_dir_path, "tokenizer.zip")

    logger.info(f"Downloading tokenizer from {tokenizer_url}")
    download_zip_with_requests(tokenizer_url, temp_zip_path)
    with zipfile.ZipFile(temp_zip_path, "r") as zf:
        zf.extractall(temp_dir_path)

    tokenizer = load_tokenizer(
        model_path=temp_dir_path,
        tokenizer_config_extra={"trust_remote_code": True},
        eos_token_ids=None,
    )

    if tokenizer.chat_template is None:
        ct = None
        config_path = os.path.join(temp_dir_path, "tokenizer_config.json")
        if os.path.isfile(config_path):
            with open(config_path, "r", encoding="utf-8") as f:
                tok_cfg = json.load(f)
            ct = tok_cfg.get("chat_template")
        if ct is None:
            jinja_path = os.path.join(temp_dir_path, "chat_template.jinja")
            if os.path.isfile(jinja_path):
                with open(jinja_path, "r", encoding="utf-8") as f:
                    ct = f.read()
        if ct is None:
            json_sidecar = os.path.join(temp_dir_path, "chat_template.json")
            if os.path.isfile(json_sidecar):
                with open(json_sidecar, "r", encoding="utf-8") as f:
                    try:
                        chat_cfg = json.load(f)
                        ct = chat_cfg.get("chat_template")
                    except json.JSONDecodeError:
                        pass
        if ct is not None:
            tokenizer.chat_template = ct

    return tokenizer, temp_dir


def format_chat_prompts(
    tokenizer,
    prompts: list[str],
    system_prompt: str | None = None,
    prompt_format: str = "auto",
    enable_thinking: bool | None = None,
) -> list[str]:
    """Apply the tokenizer's chat template to a list of prompts.

    - prompt_format="raw": prepend system_prompt as plain text (no chat tokens).
    - If the tokenizer has no chat_template: return prompts unchanged (with warning).
    - Otherwise: wrap each prompt as a user message (with optional system message)
      via tokenizer.apply_chat_template().
    - enable_thinking: when not None and the chat template supports it (e.g. Qwen 3),
      forwarded to apply_chat_template to enable/disable reasoning mode. Ignored for
      templates that don't reference it, so non-thinking models are unaffected.
    """
    fmt = (prompt_format or "auto").strip().lower()
    if fmt == "raw":
        if system_prompt:
            return [f"{system_prompt}{p}" for p in prompts]
        return prompts

    if fmt == "app":
        # Templating is delegated to the C++ app; pass prompt text through here.
        return list(prompts)

    if tokenizer is None or getattr(tokenizer, "chat_template", None) is None:
        logger.warning("No chat_template available on tokenizer; returning prompts unchanged")
        return prompts

    apply_kwargs = {"tokenize": False, "add_generation_prompt": True}
    if enable_thinking is not None:
        chat_template = getattr(tokenizer, "chat_template", "") or ""
        if "enable_thinking" in chat_template:
            apply_kwargs["enable_thinking"] = enable_thinking
        else:
            logger.warning(
                "enable_thinking requested but the chat template does not support it; ignoring"
            )

    formatted = []
    for prompt in prompts:
        messages: list[dict[str, str]] = []
        if system_prompt:
            messages.append({"role": "system", "content": system_prompt})
        messages.append({"role": "user", "content": prompt})
        text = tokenizer.apply_chat_template(messages, **apply_kwargs)
        formatted.append(text)
    return formatted
