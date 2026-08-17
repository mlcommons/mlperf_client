#!/usr/bin/env python3
""Bootstrap 95% confidence interval for the mean (stdlib only).""

from __future__ import annotations

import argparse
import random
from typing import Iterable, Sequence


def _mean(values: Sequence[float]) -> float:
    if not values:
        raise ValueError("values must not be empty")
    return sum(values) / len(values)


def _percentile(sorted_data: list[float], p: float) -> float:
    """Linear-interpolation percentile; p in [0, 100]. data must be sorted.""
    n = len(sorted_data)
    if n == 1:
        return sorted_data[0]
    k = (n - 1) * p / 100.0
    f = int(k)
    c = min(f + 1, n - 1)
    return sorted_data[f] + (k - f) * (sorted_data[c] - sorted_data[f])


def bootstrap_mean_ci(
    data: Sequence[float],
    *,
    n_iterations: int = 10_000,
    ci_level: float = 0.95,
    seed: int | None = None,
) -> tuple[float, float, float]:
    ""
    Percentile bootstrap CI for the sample mean.

    Returns (point_estimate, lower, upper).
    ""
    x = list(data)

    rng = random.Random(seed)
    n = len(x)
    boot_means: list[float] = []
    for _ in range(n_iterations):
        sample = [x[rng.randrange(n)] for _ in range(n)]
        boot_means.append(_mean(sample))

    boot_means.sort()
    alpha = 1.0 - ci_level
    lower = _percentile(boot_means, 100 * alpha / 2)
    upper = _percentile(boot_means, 100 * (1 - alpha / 2))
    return _mean(x), lower, upper


def main() -> None:
    parser = argparse.ArgumentParser(description="Bootstrap 95% CI for the mean")
    parser.add_argument("values", nargs="+", type=float, help="Sample values")
    parser.add_argument(
        "-n",
        "--iterations",
        type=int,
        default=10_000,
        help="Number of bootstrap resamples (default: 10000)",
    )
    parser.add_argument("--seed", type=int, default=None, help="RNG seed")
    args = parser.parse_args()

    mean, lo, hi = bootstrap_mean_ci(
        args.values,
        n_iterations=args.iterations,
        seed=args.seed,
    )
    print(f"sample mean: {mean:.6f}")
    print(f"95% CI:      [{lo:.6f}, {hi:.6f}]")
    print(f"iterations:  {args.iterations}")


if __name__ == "__main__":
    main()
