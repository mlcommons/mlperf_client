import argparse
import glob
import json
import shutil
import subprocess
import sys
import os
from datetime import datetime
from pathlib import Path

_accuracy_root = Path(__file__).resolve().parent.parent
if str(_accuracy_root) not in sys.path:
    sys.path.insert(0, str(_accuracy_root))

from mlperf_common import delete_cont_config, find_latest_run_id, make_cont_config  # noqa: E402

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
BENCHMARK_SCRIPT = os.path.join(SCRIPT_DIR, "run_mmlu_benchmark.py")
EXECUTION_CONFIGS_DIR = os.path.join(SCRIPT_DIR, "execution_configs")
OUTPUT_MMLU_DIR = os.path.join(SCRIPT_DIR, "output", "mmlu")
OUTPUT_MMLU_REPORT_DIR = os.path.join(SCRIPT_DIR, "output")
OUTPUT_MMLU_LOG = os.path.join(SCRIPT_DIR, "mmlu.log")
MAX_RETRIES = 3

def find_configs(config_dir: str) -> list[str]:
    """Return all .json files in the given directory, sorted. Excludes *_cont.json files."""
    configs = sorted(glob.glob(os.path.join(config_dir, "*.json")))
    configs = [c for c in configs if not c.endswith("_cont.json")]
    return configs

def get_latest_run_id() -> int | None:
    """Find the highest integer-named subfolder under output/mmlu/."""
    return find_latest_run_id(OUTPUT_MMLU_DIR)

def rename_existing_report():
    """Rename output/mmlu/<latest>/report.json to report_old.json if it exists."""
    run_id = get_latest_run_id()
    if run_id is None:
        return
    report_path = os.path.join(OUTPUT_MMLU_REPORT_DIR, "report.json")
    if os.path.isfile(report_path):
        old_path = os.path.join(os.path.dirname(report_path), "report_old.json")
        os.replace(report_path, old_path)
        print(f"Renamed {report_path} -> report_old.json")

def report_exists() -> bool:
    """Check if output/mmlu/report.json exists."""
    return os.path.isfile(os.path.join(OUTPUT_MMLU_REPORT_DIR, "report.json"))


def run_single(config_path: str, verbose: bool = False, bench_type: str = "mmlu",
               skip_failed_prompts: bool = True, run_config: str | None = None,
               program: str | None = None) -> int:
    """Run the benchmark script once. Returns the process exit code."""
    cmd = [sys.executable, BENCHMARK_SCRIPT, "--config", config_path, "-t", bench_type]
    if verbose:
        cmd.append("-v")
    if skip_failed_prompts:
        cmd.append("-s")
    if run_config:
        cmd.extend(["-r", run_config])
    if program:
        cmd.extend(["-p", program])
    result = subprocess.run(cmd, cwd=SCRIPT_DIR)
    return result.returncode

def _get_log_size() -> int:
    """Return current size (in bytes) of OUTPUT_MMLU_LOG, or 0 if it doesn't exist."""
    try:
        return os.path.getsize(OUTPUT_MMLU_LOG)
    except OSError:
        return 0

def _save_log_delta(config_path: str, log_offset_before: int, timestamp: str):
    """Save the text appended to OUTPUT_MMLU_LOG since log_offset_before into a per-run log file."""
    if not os.path.isfile(OUTPUT_MMLU_LOG):
        return
    current_size = os.path.getsize(OUTPUT_MMLU_LOG)
    if current_size <= log_offset_before:
        return  # nothing new was written

    os.makedirs(OUTPUT_MMLU_DIR, exist_ok=True)
    base = os.path.splitext(os.path.basename(config_path))[0]
    run_id = get_latest_run_id()
    log_name = f"report_{run_id}_{base}_{timestamp}.log"
    log_dest = os.path.join(OUTPUT_MMLU_DIR, log_name)

    with open(OUTPUT_MMLU_LOG, "r", encoding="utf-8", errors="replace") as f:
        f.seek(log_offset_before)
        delta = f.read()

    with open(log_dest, "w", encoding="utf-8") as f:
        f.write(delta)
    print(f"Saved log delta -> {log_name}")

def _archive_report(timestamp: str):
    """Copy the successful report.json into OUTPUT_MMLU_DIR with the given timestamp suffix."""
    run_id = get_latest_run_id()
    if run_id is None:
        return
    report_src = os.path.join(OUTPUT_MMLU_REPORT_DIR, "report.json")
    if not os.path.isfile(report_src):
        return
    os.makedirs(OUTPUT_MMLU_DIR, exist_ok=True)
    report_dest = os.path.join(OUTPUT_MMLU_DIR, f"report_{run_id}_{timestamp}.json")
    shutil.copy2(report_src, report_dest)
    print(f"Archived report -> {os.path.basename(report_dest)}")

def run_benchmark(config_path: str, verbose: bool = False, bench_type: str = "mmlu",
                  skip_failed_prompts: bool = True, run_config: str | None = None,
                  program: str | None = None) -> bool:
    """Run a benchmark config with up to MAX_RETRIES attempts. Returns True if report.json was produced."""
    current_config = config_path

    for attempt in range(1, MAX_RETRIES + 1):
        print(f"\n{'='*60}")
        print(f"Running: {os.path.basename(current_config)}  (attempt {attempt}/{MAX_RETRIES})")
        print(f"{'='*60}")

        rename_existing_report()

        # Snapshot log size before the run
        log_offset_before = _get_log_size()

        run_id_before = get_latest_run_id()
        exit_code = run_single(current_config, verbose=verbose, bench_type=bench_type,
                               skip_failed_prompts=skip_failed_prompts, run_config=run_config,
                               program=program)
        run_id_after = get_latest_run_id()

        # Generate a timestamp for this attempt's artifacts
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")

        # Save the log delta for this attempt (success or failure)
        _save_log_delta(current_config, log_offset_before, timestamp)

        if report_exists():
            print(f"SUCCESS: report.json created for {os.path.basename(current_config)}")
            _archive_report(timestamp)
            return True

        print(f"WARNING: report.json not found after attempt {attempt} (exit code {exit_code})")

        if attempt < MAX_RETRIES:
            # Determine the run ID to continue
            cont_run_id = run_id_after if run_id_after is not None else (run_id_before if run_id_before is not None else 1)
            current_config = make_cont_config(config_path, cont_run_id)

    print(f"FAILED: report.json not produced after {MAX_RETRIES} attempts for {os.path.basename(config_path)}, moving on.")
    return False

def main():
    parser = argparse.ArgumentParser(description="MMLU benchmark runner with retry support")
    parser.add_argument("-c", "--config", type=str, nargs="+",
                        help="One or more config files to run (instead of scanning execution_configs/)")
    parser.add_argument("-r", "--run-config", type=str,
                        help="Path to a vendor default config (overrides RunConfigPath in each config)")
    parser.add_argument("-p", "--program", type=str,
                        help="Path to mlperf-windows.exe (overrides MLPerfProgramPath in each config)")
    parser.add_argument("-v", "--verbose", action="store_true", default=False,
                        help="Run the script in verbose mode")
    parser.add_argument("-t", "--type", type=str, default="mmlu", choices=["mmlu", "tinymmlu"],
                        help="Type of the benchmark to run")
    parser.add_argument("-s", "--skip-failed-prompts", action="store_true", default=False,
                        help="Skip the failed prompts")
    args = parser.parse_args()

    print("Arguments:")
    for key, value in vars(args).items():
        print(f"  {key}: {value}")

    global OUTPUT_MMLU_DIR
    OUTPUT_MMLU_DIR = os.path.join(SCRIPT_DIR, "output", args.type)

    if args.program:
        args.program = os.path.abspath(args.program)

    if args.config:
        all_configs = [os.path.abspath(c) for c in args.config]
    else:
        all_configs = find_configs(EXECUTION_CONFIGS_DIR)

    if not all_configs:
        print("No matching config files found.")
        sys.exit(1)

    print(f"Found {len(all_configs)} config(s).")
    for cfg in all_configs:
        print(f"  - {os.path.basename(cfg)}")

    succeeded = 0
    failed = 0
    for config_path in all_configs:
        delete_cont_config(config_path)
        if run_benchmark(config_path, verbose=args.verbose, bench_type=args.type,
                         skip_failed_prompts=args.skip_failed_prompts,
                         run_config=args.run_config, program=args.program):
            succeeded += 1
        else:
            failed += 1
        delete_cont_config(config_path)

    print(f"\nDone: {succeeded} succeeded, {failed} failed out of {len(all_configs)} configs.")
    if failed:
        sys.exit(1)

if __name__ == "__main__":
    main()
