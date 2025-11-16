'''
This script is used to run the MLPerf benchmark with specified configuration
and handle pre- and post-benchmark sleep intervals.
'''
import argparse
import subprocess
import time
from pathlib import Path
from datetime import datetime

def main():
    # Parse command line arguments
    parser = argparse.ArgumentParser(description="MLPerf Client Power Efficiency Analysis tool")
    parser.add_argument("-c", "--config", type=str, required=True,
                        help="Path to the JSON config file with")
    parser.add_argument("--sleep", type=int, required=True, default=240,
                        help="Sleep time in seconds before and after the benchmark run")
    
    args = parser.parse_args()

    # Rename mlperf logs directory
    old_dir = Path('logs')
    if old_dir.is_dir():
        timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
        new_dir = old_dir.parent/f"{old_dir.name}_ranging_{timestamp}"
        old_dir.rename(new_dir)
        print(f"ranging log renamed to: {new_dir}")
    else:
        print(f"ranging directory '{old_dir}' not found.")

    time.sleep(args.sleep)

    # Execute the benchmark command here
    subprocess.run(["mlperf-windows.exe", "-p", "false", "-c", args.config])

    time.sleep(args.sleep)

if __name__ == "__main__":
    main()
