import matplotlib.pyplot as plt
import numpy as np
import re

from dataclasses import dataclass
from datetime import datetime, timedelta
from enum import Enum, auto
from math import prod
from pathlib import Path
from typing import Optional, Any

# configure logging
import logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

@dataclass(frozen=True)
class MeanEstimate:
    n: int      # number of samples
    mean: float # mean value
    std: float  # standard deviation
    lo: float   # lower bound of the confidence interval
    hi: float   # upper bound of the confidence interval
    err: float  # half-width of the confidence interval

# Enum for events types
class EventType(Enum):
    POWER = auto()

    INFERENCE_END = auto()
    ITERATION_METRIC = auto()
    PROMPT_METRIC = auto()
    INFERENCE_START = auto()

    STEADY_START = auto()
    STEADY_END = auto()
    COOLDOWN_START = auto()
    COOLDOWN_END = auto()

    def __str__(self):
        return self.name.lower()
    
    def __lt__(self, other):
        if not isinstance(other, EventType):
            return NotImplemented
        return self.value < other.value

    def __le__(self, other):
        if not isinstance(other, EventType):
            return NotImplemented
        return self.value <= other.value

    def __gt__(self, other):
        if not isinstance(other, EventType):
            return NotImplemented
        return self.value > other.value

    def __ge__(self, other):
        if not isinstance(other, EventType):
            return NotImplemented
        return self.value >= other.value

    def __eq__(self, other):
        if not isinstance(other, EventType):
            return NotImplemented
        return self.value == other.value

    def __ne__(self, other):
        if not isinstance(other, EventType):
            return NotImplemented
        return self.value != other.value
    
# Type alias for power event tuple
PowerEvent = tuple[datetime, EventType, Optional[list[Any]]]

@dataclass(frozen=True)
class PowerEfficiencyStats:
    category_efficiency: dict[str, MeanEstimate]
    overall_efficiency: float
    steady_state_power: Optional[MeanEstimate]
    cooldown_power: Optional[MeanEstimate]
    power_events: list[PowerEvent] # (timestamp, event type, value)
    plot: plt.Figure

def bootstrap_ci(data, num_samples=1000, ci=0.95) -> MeanEstimate:
    """Bootstrap resampling to estimate mean and confidence interval."""
    means = []
    n = len(data)
    for _ in range(num_samples):
        sample = np.random.choice(data, size=n, replace=True)
        means.append(np.mean(sample))
    means = np.array(means)
    mean = np.mean(means)
    std = np.std(means)
    lower_bound = np.percentile(means, (1 - ci) / 2 * 100)
    upper_bound = np.percentile(means, (1 + ci) / 2 * 100)
    return MeanEstimate(n=n, mean=mean, std=std, 
                        lo=lower_bound, hi=upper_bound,
                        err=(upper_bound - lower_bound) / 2)

def pooled_ci(data: list[tuple[datetime, MeanEstimate]]) -> MeanEstimate:
    """Calculate pooled mean and CI from list of MeanEstimate."""
    pooled_mean = sum(d.mean * d.n for d in data) / sum(d.n for d in data)
    SE = sum((d.std ** 2) / d.n for d in data) ** 0.5
    return MeanEstimate(
        n=sum(d.n for d in data),
        mean=pooled_mean,
        std=SE,
        lo=pooled_mean - 1.96 * SE,
        hi=pooled_mean + 1.96 * SE,
        err=1.96 * SE
    )

TIMESTAMP_FORMAT = "%m-%d-%Y %H:%M:%S.%f"
POWER_LOG_TRIM_SECONDS = 4 * 60  # Trim 4 minutes from start and end of power log

# regex to extract the timestamp from each line
def extract_timestamp(line: str) -> Optional[datetime]:
    match = re.search(r"(\d{2}-\d{2}-\d{4} \d{2}:\d{2}:\d{2}\.\d{3})", line)
    if match:
        return datetime.strptime(match.group(1), TIMESTAMP_FORMAT)
    return None

def CalculateEfficiency(start: datetime, end: datetime, tokens: int,
                        power_data: list[tuple[datetime, float]]) -> tuple[datetime, datetime, float, float, float]:
    """Calculate efficiency (tokens/joule) for a given inference interval."""
    # find index of first and last power_data entry within the start and end timestamps
    index_start = next((i for i, p in enumerate(power_data) if p[0] > start), None)
    index_end = next((i for i, p in enumerate(power_data) if p[0] > end), None)

    if index_start is None or index_end is None:
        raise ValueError(f"[ERROR] Could not find power data for interval {start} to {end}")

    # We add one entry before and after to allow trapezoidal integration
    # for intervals that don't align exactly with power_data timestamps
    power_data_slice = power_data[index_start-1:index_end+1]

    # calculate average watts, volts, amps, pf over this interval using trapezoidal rule
    total_energy = 0.0  # in Joule
    total_time = 0.0  # in seconds
    for i in range(1, len(power_data_slice)):
        t0, w0 = power_data_slice[i-1]
        t1, w1 = power_data_slice[i]
        dt = min(t1,end) - max(t0, start) # Also clip to the start and end of the interval
        ms_delta = dt.total_seconds()
        avg_watts = (w0 + w1) / 2
        total_energy += avg_watts * ms_delta
        total_time += ms_delta

    avg_watts = total_energy / (total_time) if total_time > 0 else 0
    return (start, end, avg_watts, total_time, (float(tokens) / total_energy) if total_energy > 0 else 0)

@dataclass(frozen=True)
class MeanPower:
    start: datetime
    end: datetime
    stat: MeanEstimate

def CalculateSteadyState(power_data: list[tuple[datetime, float]],
                         debug_ts: list[datetime]) -> tuple[Optional[MeanPower], Optional[MeanPower]]:
    '''Calculate steady state and cooldown power statistics.'''
    mlperf_start = debug_ts[0]
    mlperf_end = debug_ts[-1]

    STEADY_STATE_DURATION = timedelta(minutes=2)
    COOLDOWN_DURATION = timedelta(seconds=30)

    steady_state_start = mlperf_start - COOLDOWN_DURATION - STEADY_STATE_DURATION
    steady_state_end = steady_state_start + STEADY_STATE_DURATION

    steady_state_data = [p for p in power_data if steady_state_start <= p[0] and p[0] <= steady_state_end]

    if len(steady_state_data) > 0 and power_data[0][0] <= steady_state_start:
        steady_stats = MeanPower(start=steady_state_start, end=steady_state_end,
                                 stat=bootstrap_ci([p[1] for p in steady_state_data]))
    else:
        steady_stats = None

    cooldown_state_start = mlperf_end + COOLDOWN_DURATION
    cooldown_state_end = cooldown_state_start + STEADY_STATE_DURATION

    cooldown_state_data = [p for p in power_data if cooldown_state_start <= p[0] and p[0] <= cooldown_state_end]

    if len(cooldown_state_data) > 0 and power_data[-1][0] >= cooldown_state_end:
        cooldown_stats = MeanPower(start=cooldown_state_start, end=cooldown_state_end,
                                   stat=bootstrap_ci([p[1] for p in cooldown_state_data]))
    else:
        cooldown_stats = None

    return steady_stats, cooldown_stats

def append_power_events(events: list[PowerEvent],
                        new_events: list[tuple[datetime, Any | list[Any]] | datetime],
                        event_type: EventType):
    """Append new events to the events list with the specified event type."""
    for e in new_events:
        if isinstance(e, tuple) and len(e) == 2 and isinstance(e[0], datetime):
            if isinstance(e[1], list):
                events.append((e[0], event_type, e[1]))
            else:
                events.append([e[0], event_type, [e[1]]])
        elif isinstance(e, datetime):
            events.append([e, event_type, None])
        else:
            raise ValueError(f"Invalid event format: {e}")

def parse_power_log(
        debug_log: Path,
        executor_log: Path,
        power_log: Path,
        warmup_iters: int,
        bench_iters: int,
        calculate_steady_state: bool = False) -> PowerEfficiencyStats:

    if not debug_log.exists():
        raise FileNotFoundError(f"Debug log file not found: {debug_log}")
    if not executor_log.exists():
        raise FileNotFoundError(f"Executor log file not found: {executor_log}")
    if not power_log.exists():
        raise FileNotFoundError(f"Power log file not found: {power_log}")
    
    with open(executor_log, 'r', encoding='utf-8') as f:
        exec_log_str = f.read().splitlines()

    with open(power_log, 'r') as f:
        power_log_str = f.read().splitlines()

    with open(debug_log, 'r') as f:
        debug_log_str = f.read().splitlines()

    power_events: list[PowerEvent] = []

    # Extract power measurement timestamps and categories from executor log
    # TODO: Better log pattern matching and error handling
    power_begin_ts = [extract_timestamp(s) for s in exec_log_str if "power_begin" in s]
    append_power_events(power_events, power_begin_ts, EventType.INFERENCE_START)
    power_end_ts = [extract_timestamp(s) for s in exec_log_str if "power_end" in s]
    append_power_events(power_events, power_end_ts, EventType.INFERENCE_END)
    logger.debug(f"Power measurement timestamps count {len(power_begin_ts)}")

    # Extract per prompt category from executor log
    prompt_category = [re.search(r"Category:\s*(.+)", s).group(1)
                       for s in exec_log_str if "Category:" in s]  # assume always matches
    logger.debug(f"Categories found: {'; '.join(set(prompt_category))}")

    if any(t is None for t in power_begin_ts + power_end_ts) \
        or len(power_begin_ts) != len(power_end_ts):
        raise ValueError("Could not extract all power measurement timestamps from executor log.")

    run_inf_pattern = re.compile(r"Ran inference and got\s*(\d+)", re.IGNORECASE)

    tokens = [int(run_inf_pattern.search(s).group(1)) 
              for s in exec_log_str if "Ran inference and got" in s]
    if len(tokens) != len(power_begin_ts) or len(tokens) != len(power_end_ts):
        raise ValueError("Mismatch in number of power measurements and token counts.")
    logger.debug(f"Total iterations: {len(tokens)}")
    logger.debug(f"Token outputs: {tokens}")

    # Infrence intervals (start, end, tokens)
    inference_ts: list[tuple[datetime, datetime, int]] = \
        list(zip(power_begin_ts, power_end_ts, tokens))

    # Debug log timestampss
    debug_ts: list[datetime] = [extract_timestamp(s) for s in debug_log_str]
    debug_ts = [t for t in debug_ts if t is not None]

    power_data: list[tuple[datetime, float]] = []
    for line in (s for s in power_log_str if s.startswith("Time,")):
        parts = line.strip().split(',')
        if len(parts) < 3:
            raise ValueError(f"Malformed power log line: {line}")
        try:
            ts = datetime.strptime(parts[1], TIMESTAMP_FORMAT)
            power = abs(float(parts[3]))
            power_data.append((ts, power))
        except ValueError:
            raise ValueError(f"Invalid timestamp or power value in line: {line}")
    # trim power_data to the range of timestamps in debug log +- POWER_LOG_TRIM_SECONDS
    power_data = [p for p in power_data
                  if p[0] >= (debug_ts[0] - timedelta(seconds=POWER_LOG_TRIM_SECONDS))
                  and p[0] <= (debug_ts[-1] + timedelta(seconds=POWER_LOG_TRIM_SECONDS))]
    if not power_data:
        raise ValueError("No power data found in the specified time range.")
    append_power_events(power_events, power_data, EventType.POWER)
    
    # Compute per iteration efficiency
    #  [start, end, avg_power, seconds, efficiency (tokens/joule)]
    per_prompts_efficiency: list[datetime, datetime, float, float, float] = []
    for start, end, toks in inference_ts:
        prompt_efficiency = CalculateEfficiency(start, end, toks, power_data)
        per_prompts_efficiency.append(prompt_efficiency)
        append_power_events(power_events, [(prompt_efficiency[0], list(prompt_efficiency[2:]))], EventType.ITERATION_METRIC)
    logger.debug(f"Calculated efficiency for {len(per_prompts_efficiency)} iterations.")

    ### Compute statistics
    total_iters = warmup_iters + bench_iters

    # Split per_prompts_efficiency in tuples of bench iterations only (skip warmup)
    grouped_power_data = [per_prompts_efficiency[i+warmup_iters:i+total_iters] for i in range(0, len(per_prompts_efficiency), total_iters)]

    avg_inference_power = sum( (sum(x[2] for x in group)) for group in grouped_power_data) / (len(grouped_power_data) * bench_iters)
    logger.debug(f"Average inference power over all iterations: {avg_inference_power:.2f} Watts")

    per_category_res: dict[str, list[MeanEstimate] | MeanEstimate] = {category: [] for category in prompt_category}
    per_prompt_res: list[tuple[datetime, MeanEstimate]] = []
    for group, category in zip(grouped_power_data, prompt_category):
        stat = bootstrap_ci([x[4] for x in group])
        logger.debug(f"Prompts Efficiency - Category: '{category}', Efficiency: {stat.mean:.2f} Tokens/Joule (CI: {stat.lo:.2f}-{stat.hi:.2f}) over {stat.n} samples")

        per_prompt_res.append((group[0][0], stat))
        per_category_res[category].append(stat)
        append_power_events(power_events, [(group[0][0], [category, stat.mean, stat.err])], EventType.PROMPT_METRIC)

    for category in per_category_res:
        per_category_res[category] = pooled_ci(per_category_res[category])
        logger.debug(f"Category efficiency - Category '{category}', Pooled Efficiency: {per_category_res[category].mean:.2f} Tokens/Joule (CI: {per_category_res[category].lo:.2f}-{per_category_res[category].hi:.2f}) over {per_category_res[category].n} samples")


    # Geometric mean of per-category means
    # CI is not defined for geomean, so we only report the geaomean of mean values
    geomean_efficiency = prod([r.mean for _, r in per_category_res.items()]) ** (1 / len(per_category_res))
    logger.debug(f"Overall Geomean Efficiency: {geomean_efficiency:.2f} Tokens/Joule")

    steady_stats, cooldown_stats = None, None
    if calculate_steady_state:
        steady_stats, cooldown_stats = CalculateSteadyState(power_data, debug_ts)
        if steady_stats:
            append_power_events(power_events, [steady_stats.start], EventType.STEADY_START)
            append_power_events(power_events, [(steady_stats.end, [steady_stats.stat.mean, steady_stats.stat.err])], EventType.STEADY_END)
            logger.debug(f"Steady State Power: {steady_stats.stat.mean:.2f} Watts (CI: {steady_stats.stat.lo:.2f}-{steady_stats.stat.hi:.2f}) over {steady_stats.stat.n} samples")
        if cooldown_stats:
            append_power_events(power_events, [cooldown_stats.start], EventType.COOLDOWN_START)
            append_power_events(power_events, [(cooldown_stats.end, [cooldown_stats.stat.mean, cooldown_stats.stat.err])], EventType.COOLDOWN_END)
            logger.debug(f"Cooldown Power: {cooldown_stats.stat.mean:.2f} Watts (CI: {cooldown_stats.stat.lo:.2f}-{cooldown_stats.stat.hi:.2f}) over {cooldown_stats.stat.n} samples")


    ## Plot results
    ax = plt.gca()

    # plot the results power
    ax.errorbar([x[0] for x in per_prompt_res], [x[1].mean for x in per_prompt_res], yerr=[x[1].err for x in per_prompt_res], color='red', fmt='o', label='Prompt Avg Efficiency (Tokens/Joule)')
    # plot all power samples as background
    plt.scatter([x[0] for x in per_prompts_efficiency], [x[4] for x in per_prompts_efficiency], color='gray', alpha=0.5, s=10, label='Prompt iteration Tokens/Joule')
    # set min y to 0
    plt.ylim(bottom=0)

    # plot horizontal line for geomean with value label
    plt.axhline(y=geomean_efficiency, color='green', linestyle='--', label='Geomean Metric')
    plt.text(power_data[0][0], geomean_efficiency * 1.01, f'geomean_efficiency: {geomean_efficiency:.2f} Tokens/Joule', color='green')

    plt.ylabel('Efficiency (Tokens/Joule)')

    pwr_ax = plt.twinx()
    pwr_ax.plot([x[0] for x in power_data], [x[1] for x in power_data], color='blue', alpha=0.3, label='System Power (Watts)')
    pwr_ax.axhline(y=avg_inference_power, color='blue', linestyle='--', label='Avg Inference Power')
    # add text label for average inference power line
    pwr_ax.text(power_data[0][0], avg_inference_power * 1.01, f'Avg Inference Power: {avg_inference_power:.2f} Watts', color='blue')

    # # Print average power during steady state on the plot
    if steady_stats is not None:
        plt.axvspan(steady_stats.start, steady_stats.end, color='orange', alpha=0.3, label='Steady State Interval')
        plt.text(steady_stats.start, steady_stats.stat.mean * 1.05, f'{steady_stats.stat.mean:.2f} +-{steady_stats.stat.err:.2f}\nWatts', color='black')
    if cooldown_stats is not None:
        plt.axvspan(cooldown_stats.start, cooldown_stats.end, color='green', alpha=0.3, label='Cooldown Interval')
        plt.text(cooldown_stats.start, cooldown_stats.stat.mean * 1.05, f'{cooldown_stats.stat.mean:.2f} +-{cooldown_stats.stat.err:.2f} \nWatts', color='black')

    # Double plot width
    plt.gcf().set_size_inches(12, 6)

    plt.xlabel('Start Time (ms since epoch)')
    plt.ylabel('System power (Watts)', color='blue')

    plt.title('Power efficiency and consumption')

    plt.grid(True)

    # collect handles/labels from both axes (main and power axis)
    h1, l1 = ax.get_legend_handles_labels()
    h2, l2 = pwr_ax.get_legend_handles_labels()
    handles = h1 + h2
    labels = l1 + l2
    ax.legend(handles, labels, loc='upper center', bbox_to_anchor=(0.5, -0.12), ncol=3)
    plt.tight_layout()  # make room for the outside legend

    power_events.sort(key=lambda x: (x[0],x[1]))  # sort by (timestamp, event type)

    return PowerEfficiencyStats(
        category_efficiency=per_category_res,
        overall_efficiency=geomean_efficiency,
        steady_state_power=steady_stats,
        cooldown_power=cooldown_stats,
        power_events=power_events,
        plot=plt
    )

def parse_power_log_cli():
    import argparse

    parser = argparse.ArgumentParser(description="Parse power log and compute efficiency stats.")
    parser.add_argument("-d", "--debug_log", type=Path, required=True, help="Path to debug log file")
    parser.add_argument("-e", "--executor_log", type=Path, required=True, help="Path to executor log file")
    parser.add_argument("-p", "--power_log", type=Path, required=True, help="Path to power log file")
    parser.add_argument("-w", "--warmups", type=int, default=1, help="Number of warmup iterations")
    parser.add_argument("-i", "--iterations", type=int, default=3, help="Number of benchmark iterations")
    parser.add_argument("-s", "--steady_state", action='store_true', help="Calculate steady state and cooldown power stats")
    parser.add_argument("-v", "--verbose", action='store_true', help="Enable verbose logging")

    args = parser.parse_args()

    if args.verbose:
        logger.setLevel(logging.DEBUG)

    stats = parse_power_log(
        debug_log=args.debug_log,
        executor_log=args.executor_log,
        power_log=args.power_log,
        warmup_iters=args.warmups,
        bench_iters=args.iterations,
        calculate_steady_state=args.steady_state
    )

    print("Power Efficiency Stats:")
    print(f"  Overall Efficiency: {stats.overall_efficiency:.2f} Tokens/Joule")

    for category, estimate in stats.category_efficiency.items():
        print(f"    Category '{category}' Efficiency: {estimate.mean:.2f} Tokens/Joule (CI: {estimate.lo:.2f}-{estimate.hi:.2f}) over {estimate.n} samples")

    if stats.steady_state_power:
        print(f"  Steady State Power: {stats.steady_state_power.stat.mean:.2f} Watts (CI: {stats.steady_state_power.stat.lo:.2f}-{stats.steady_state_power.stat.hi:.2f}) over {stats.steady_state_power.stat.n} samples")
    if stats.cooldown_power:
        print(f"  Cooldown Power: {stats.cooldown_power.stat.mean:.2f} Watts (CI: {stats.cooldown_power.stat.lo:.2f}-{stats.cooldown_power.stat.hi:.2f}) over {stats.cooldown_power.stat.n} samples")

    stats.plot.show()

if __name__ == "__main__":
    parse_power_log_cli()
