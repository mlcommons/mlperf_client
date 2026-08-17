"""IFEval benchmark runner with retry support.

Wraps run_ifeval_benchmark.py with automatic retries on crash/failure.
IFEval already has built-in resume (via RunID + skip_if_results_match_prompt_count),
so retries simply re-invoke the script with the same RunID to continue from where it left off.
"""

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
BENCHMARK_SCRIPT = os.path.join(SCRIPT_DIR, "run_ifeval_benchmark.py")
MAX_RETRIES = 3

_accuracy_root = Path(__file__).resolve().parent.parent
if str(_accuracy_root) not in sys.path:
    sys.path.insert(0, str(_accuracy_root))

from mlperf_common import delete_cont_config, find_latest_run_id, make_cont_config  # noqa: E402


def get_output_dir(config_path: str) -> str:
    """Read OutputDir from the config file."""
    with open(config_path, "r", encoding="utf-8") as f:
        config = json.load(f)
    return os.path.join(os.path.dirname(os.path.abspath(config_path)), config.get("OutputDir", "output"))


def report_exists(config_path: str) -> bool:
    """Check if ifeval_report.json exists in the latest run directory."""
    output_dir = get_output_dir(config_path)
    ifeval_dir = os.path.join(output_dir, "ifeval")
    run_id = find_latest_run_id(ifeval_dir)
    if run_id is None:
        return False
    return os.path.isfile(os.path.join(ifeval_dir, str(run_id), "ifeval_report.json"))


def get_latest_run_id(config_path: str) -> int | None:
    """Find the highest integer-named subfolder under output/ifeval/."""
    output_dir = get_output_dir(config_path)
    return find_latest_run_id(os.path.join(output_dir, "ifeval"))


def run_single(config_path: str, verbose: bool = False,
               run_config: str | None = None, program: str | None = None,
               extra_args: list[str] | None = None) -> int:
    """Run the benchmark script once. Returns the process exit code."""
    cmd = [sys.executable, BENCHMARK_SCRIPT, "--config", config_path,
           "--instruction-following-eval-dir", os.path.join(SCRIPT_DIR, "instruction_following_eval"),
           "--canonical-data-path", os.path.join(SCRIPT_DIR, "instruction_following_eval", "data", "input_data.jsonl")]
    if verbose:
        cmd.append("-v")
    if run_config:
        cmd.extend(["-r", run_config])
    if program:
        cmd.extend(["-p", program])
    if extra_args:
        cmd.extend(extra_args)
    result = subprocess.run(cmd, cwd=SCRIPT_DIR)
    return result.returncode


def run_benchmark(config_path: str, verbose: bool = False,
                  run_config: str | None = None, program: str | None = None,
                  extra_args: list[str] | None = None) -> bool:
    """Run IFEval with up to MAX_RETRIES attempts. Returns True if ifeval_report.json was produced."""
    current_config = config_path

    for attempt in range(1, MAX_RETRIES + 1):
        print(f"\n{'='*60}")
        print(f"Running: {os.path.basename(current_config)}  (attempt {attempt}/{MAX_RETRIES})")
        print(f"{'='*60}")

        exit_code = run_single(current_config, verbose=verbose,
                               run_config=run_config, program=program,
                               extra_args=extra_args)

        if report_exists(config_path):
            print(f"SUCCESS: ifeval_report.json created for {os.path.basename(config_path)}")
            return True

        print(f"WARNING: ifeval_report.json not found after attempt {attempt} (exit code {exit_code})")

        if attempt < MAX_RETRIES:
            run_id = get_latest_run_id(config_path)
            if run_id is not None:
                current_config = make_cont_config(config_path, run_id)
            else:
                current_config = config_path

    print(f"FAILED: ifeval_report.json not produced after {MAX_RETRIES} attempts for {os.path.basename(config_path)}")
    return False


def main():
    parser = argparse.ArgumentParser(description="IFEval benchmark runner with retry support")
    parser.add_argument("-c", "--config", type=str, required=True,
                        help="IFEval config file (e.g. ifeval-NVIDIA_llamacpp-CUDA_GPU.json)")
    parser.add_argument("-r", "--run-config", type=str,
                        help="Path to a vendor default config (overrides RunConfigPath in the config)")
    parser.add_argument("-p", "--program", type=str,
                        help="Path to mlperf-windows.exe (overrides MLPerfProgramPath in the config)")
    parser.add_argument("-v", "--verbose", action="store_true", default=False,
                        help="Run the script in verbose mode")
    args = parser.parse_args()

    print("Arguments:")
    for key, value in vars(args).items():
        print(f"  {key}: {value}")

    if args.program:
        args.program = os.path.abspath(args.program)

    config_path = os.path.abspath(args.config)
    if not os.path.isfile(config_path):
        print(f"Config file not found: {config_path}")
        sys.exit(1)

    delete_cont_config(config_path)
    success = run_benchmark(config_path, verbose=args.verbose,
                            run_config=args.run_config, program=args.program)
    delete_cont_config(config_path)

    if not success:
        sys.exit(1)


if __name__ == "__main__":
    main()
