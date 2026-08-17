#!/usr/bin/env python3
"""Aggregate total_units by YEAR, MONTH, and ITEM TYPE for seasonality scan."""
from __future__ import annotations

import argparse
import sys
from collections import defaultdict
from pathlib import Path

from beverage_lib import DEFAULT_ENCODING, iter_rows, measures


def main() -> None:
    parser = argparse.ArgumentParser(description="Monthly trend by ITEM TYPE.")
    parser.add_argument("--input", required=True)
    parser.add_argument("--delimiter", default=",")
    parser.add_argument("--encoding", default=DEFAULT_ENCODING)
    parser.add_argument("--year", type=int, default=None, help="Optional: limit to one year")
    parser.add_argument("--product-only", action="store_true")
    args = parser.parse_args()

    input_path = Path(args.input)
    if not input_path.is_file():
        print(f"ERROR: input not found: {input_path}", file=sys.stderr)
        sys.exit(1)

    print("=== Schema summary ===")
    print(f"input: {input_path}")
    print(f"year_filter: {args.year}")
    print(f"product-only: {args.product_only}")
    print("output: stdout only")

    # (year, month) -> item_type -> units
    grid: dict[tuple[int, int], dict[str, float]] = defaultdict(lambda: defaultdict(float))
    periods: set[tuple[int, int]] = set()

    for row in iter_rows(
        input_path, args.delimiter, args.encoding, args.year, None, args.product_only
    ):
        try:
            y = int((row.get("YEAR") or "").strip())
            m = int((row.get("MONTH") or "").strip())
        except ValueError:
            continue
        itype = (row.get("ITEM TYPE") or "").strip() or "(blank)"
        _, _, _, total = measures(row)
        grid[(y, m)][itype] += total
        periods.add((y, m))

    if not periods:
        print("No rows matched filters.")
        return

    all_types: set[str] = set()
    for type_map in grid.values():
        all_types |= set(type_map.keys())
    type_list = sorted(all_types)

    sorted_periods = sorted(periods)
    print(f"\nperiods: {len(sorted_periods)} (from {sorted_periods[0]} to {sorted_periods[-1]})")

    header = ["YEAR", "MONTH"] + type_list + ["TOTAL"]
    widths = [6, 6] + [max(10, len(t)) for t in type_list] + [12]
    fmt = " ".join(f"{{:{w}s}}" for w in widths)

    print("\n=== total_units by period and ITEM TYPE ===")
    print(fmt.format(*header))
    for y, m in sorted_periods:
        cells = [str(y), str(m)]
        row_total = 0.0
        for t in type_list:
            v = grid[(y, m)].get(t, 0.0)
            row_total += v
            cells.append(f"{v:.2f}")
        cells.append(f"{row_total:.2f}")
        print(fmt.format(*cells))

    print("\n=== trend_by_month (TSV) ===")
    print("\t".join(header))
    for y, m in sorted_periods:
        row_total = sum(grid[(y, m)].get(t, 0.0) for t in type_list)
        line = [str(y), str(m)] + [f"{grid[(y, m)].get(t, 0.0):.4f}" for t in type_list]
        line.append(f"{row_total:.4f}")
        print("\t".join(line))


if __name__ == "__main__":
    main()
