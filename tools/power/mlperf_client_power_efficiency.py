import argparse
import os
from pathlib import Path
import subprocess
import json
import logging
import sys
from datetime import datetime

# Import necessary modules
try:
    from power_log_parser import logger as power_log_parser_logger, parse_power_log, PowerEfficiencyStats
except ImportError as e:
    from tools.power.power_log_parser import logger as power_log_parser_logger, parse_power_log, PowerEfficiencyStats

logger = logging.getLogger(__name__)
logger.setLevel(level=logging.INFO)

EXECUTOR_LOG_NAME = {
    "Phi3": "phi3_5_executor.log",
    "Phi4": "phi4_executor.log",
    "Llama2": "llama2_executor.log",
    "Llama3": "llama3_executor.log",
}

CATEGORIES = [
    "Content Generation",
    "Creative Writing",
    "Summarization, Light",
    "Summarization, Moderate",
    "Code Analysis"
]

def format_results(stats: PowerEfficiencyStats, results_json: dict):
    ep_config = results_json.get("EP Configuration", {})
    ep_name = results_json.get("Execution Provider Name", "Unknown")

    print("Model: {}:".format(results_json.get("Model Name", "Unknown")))
    print(f"    {results_json.get('Device Type', 'Unknown')}:")
    print(f"        {ep_name} {json.dumps(ep_config)}:")

    if "overall_results" in results_json:
        overall = results_json["overall_results"]
        print(f"            OVERALL:\tGeomean Time to First Token: {overall.get('Geomean Time to First Token', 'N/A')} s, "
              f"Geomean 2nd+ Token Generation Rate: {overall.get('Geomean 2nd+ Token Generation Rate', 'N/A')} tokens/s, "
              f"Avg Input Tokens: {overall.get('Avg Input Tokens', 'N/A')}, "
              f"Avg Generated Tokens: {overall.get('Avg Generated Tokens', 'N/A')}, "
              f"Overall Efficiency: {stats.overall_efficiency:.3f} tokens/Joule")

    for category in CATEGORIES:
        if category in results_json["category_results"]:
            cat_results = results_json["category_results"][category]
            efficiency = stats.category_efficiency.get(category)

            efficiency_str = f"Power Efficiency: {efficiency.mean:.3f} +- {efficiency.err:.3f} tokens/Joule"
            print(f"            {category}:\tAvg Time to First Token: {cat_results.get('Avg Time to First Token', 'N/A')} s, "
                  f"Avg 2nd+ Token Generation Rate: {cat_results.get('Avg 2nd+ Token Generation Rate', 'N/A')} tokens/s, "
                  f"Avg Input Tokens: {cat_results.get('Avg Input Tokens', 'N/A')}, "
                  f"Avg Generated Tokens: {cat_results.get('Avg Generated Tokens', 'N/A')}, {efficiency_str}")
            
    if stats.steady_state_power:
        print(f"        Steady State Power: {stats.steady_state_power.stat.mean:.3f} +- {stats.steady_state_power.stat.err:.3f} Watts")
    if stats.cooldown_power:
        print(f"        Cooldown Power: {stats.cooldown_power.stat.mean:.3f} +- {stats.cooldown_power.stat.err:.3f} Watts")

def main():
    # Parse command line arguments
    parser = argparse.ArgumentParser(description="MLPerf Client Power Efficiency Analysis tool")
    parser.add_argument("-c", "--config", type=str, default=None,
                        help="Path to the JSON config file with")
    parser.add_argument("--ip", type=str, default=None,
                        help="IP address of the server")

    parser.add_argument("--ntp_server", type=str, default="time.google.com",
                        help="NTP server to sync time with")
    parser.add_argument("-p", "--parse_only", default=False, action='store_true',
                        help="Only parse power log without running the benchmark")
    parser.add_argument("-s", "--stable_power", default=False, action='store_true',
                        help="Whether to compute steady state power")
    parser.add_argument("-v", "--verbose", default=False, action='store_true',
                        help="Enable verbose logging")
    parser.add_argument("--show_plot", default=False, action='store_true',
                        help="Show power efficiency plot")
    parser.add_argument("-r", "--results_folder", type=str, default=None,
                        help="Path to results.json files for parsing")
    parser.add_argument("-o", "--output", type=str, default=None,
                        help="Path to save the power efficiency plot and events log")

    args = parser.parse_args()

    #ToDo: Check batery status before and fater running

    if args.show_plot and args.output:
        parser.error("Cannot use --show_plot and --output together, choose one")
    
    # Conditional validation
    if args.parse_only and not args.results_folder:
        parser.error("--results_folder is required when --parse_only is set")
    if args.results_folder is not None:
        results_folder = Path(args.results_folder)
        if not results_folder.is_dir():
            parser.error(f"The specified results file does not exist: {args.results_folder}")

    if not args.parse_only and (not args.config or not args.ip):
        parser.error("Both --config and --ip are required unless --parse_only is set")

    if args.verbose:
        logger.setLevel(level=logging.DEBUG)
        power_log_parser_logger.setLevel(level=logging.DEBUG)

    # Execute the benchmark command here
    sleep = "--sleep 240" if args.stable_power else "--sleep 30"
    run_cmd = f"python.exe ./tools/power/run_mlperf.py -c {args.config} {sleep}"
    logger.debug(f"Executing command: {run_cmd}")
        
    out_dir = "./power_logs"
    mlperf_out_dir = "./logs"
    # make sure directory doesn't exists
    old_dir = Path('logs')
    if old_dir.is_dir():
        logger.error(f"Please remove mlperf_out_dir before running: {old_dir}")
        sys.exit(1)

    loadgen_other_files1 = "debug.log"
    loadgen_other_files2 = "results.json"
    
    loadgen_log_file = "executor.log"
    if args.config:
        config_lower = args.config.lower()
        # Find matching key based on substring
        for key in EXECUTOR_LOG_NAME:
            if key.lower() in config_lower:
                loadgen_log_file = EXECUTOR_LOG_NAME[key]
                break

    if loadgen_log_file:
        logger.debug(f"Selected log file: {loadgen_log_file}")
    else:
        logger.error("No matching scenario found in config.")
        sys.exit(1)
        
    if not args.parse_only:
        label = os.path.basename(args.config).strip('.json')
        client_cmd = ["./tools/power/ptd_client_server/client.py",
                    "-l", label, "-g", loadgen_log_file, "-w", run_cmd, "-L", mlperf_out_dir, "-a", args.ip,
                    "-n", args.ntp_server, "-o", out_dir, "-j", loadgen_other_files1, loadgen_other_files2
                    ]
        logger.debug(f"Executing command: {client_cmd}")
        logger.debug("Running benchmark...")
        subprocess.run(["python.exe"] + client_cmd)

    #ToDo: improve the way we find the results_folder
    if args.results_folder is None:
        log_files_folder = Path(out_dir)
        timestamp = datetime.now().strftime('%Y-%m-%d_')
        pattern = f"{timestamp}*_{label}"
        matching_dirs = list(log_files_folder.glob(pattern))
        if matching_dirs:
            results_folder = matching_dirs[0]
            logger.debug(f"Found results folder: {results_folder}")
        else:
            logger.error(f"No matching directory found. {pattern}")
            sys.exit(1)

    results_file = results_folder / "run_1/results.json"
    debug_log = results_folder / "run_1/debug.log"
    power_log = results_folder / "run_1/spl.txt"
    executor_log = results_folder / "run_1" / loadgen_log_file

    if not os.path.exists(results_file):
        logger.error(f" ** Error ** something went wrong: {results_file} does not exists !")
        sys.exit(1)

    logger.debug(f"Reading results from: {results_file}")

    results_json = json.loads(results_file.read_text())
    warmup_iters = results_json.get("WarmUp")
    bench_iters = results_json.get("Iterations")
    logger.debug(f"Warmup iters: {warmup_iters}, Bench iters: {bench_iters}")

    stats = parse_power_log(debug_log, executor_log, power_log,
                            warmup_iters, bench_iters,
                            calculate_steady_state=args.stable_power)

    print("Power Efficiency Results:\n")
    format_results(stats, results_json)

    if args.show_plot:
        stats.plot.show()

    if args.output:
        output_path = Path(args.output)
        output_path.mkdir(parents=True, exist_ok=True)
        plot_file = output_path / "power_efficiency_plot.png"
        stats.plot.savefig(plot_file, format='png')
        logger.info(f"Power efficiency plot saved to: {plot_file}")

        events_log_file = output_path / "power_events.csv"
        with open(events_log_file, 'w') as f:
            f.write('"# Power Events Log"\n')
            f.write('"# Timestamp format: YYYY-MM-DDTHH:MM:SS.sss"\n')
            f.write('"# EVENT TYPES:"\n')
            f.write('"# - POWER: Power measurements (Watts)"\n')
            f.write('"# - INFERENCE_START: Timestamp when prompt processing started"\n')
            f.write('"# - INFERENCE_END: Timestamp when prompt processing ended"\n')
            f.write('"# - ITERATION_METRIC: Avg power during inference (Watts), Inference duration, Efficiency (tokens/Joule)"\n')
            f.write('"# - PROMPT_METRIC: Prompt Category, Mean Efficiency (tokens/Joule), Error"\n')
            f.write('"# - STEADY_START: Timestamp when steady state measurement started"\n')
            f.write('"# - STEADY_END: Timestamp when steady state measurement ended, Mean Power (Watts), Error"\n')
            f.write('"# - COOLDOWN_START: Timestamp when cooldown measurement started"\n')
            f.write('"# - COOLDOWN_END: Timestamp when cooldown measurement ended, Mean Power (Watts), Error"\n')

            f.write("Timestamp, Event Type, Values...\n")
            for event in stats.power_events:
                timestamp_str = event[0].strftime('%Y-%m-%dT%H:%M:%S.%f') # [:-3].replace(' ', 'T')
                # timestamp_str = event[0].strftime('%Y-%m-%d %H:%M:%S.%f')[:-3]
                event_type = event[1].name
                value_str = ','.join(map(lambda x: f'"{x}"' if isinstance(x, str) else str(x), event[2])) if event[2] else ''
                f.write(f"{timestamp_str},{event_type},{value_str}\n")
        logger.info(f"Power events log saved to: {events_log_file}")

if __name__ == "__main__":
    main()
