#!/usr/bin/env python3
"""Detect duplicates, warehouse spikes, negatives, and inactive rows (stdout only)."""
from __future__ import annotations

import argparse
import csv
import sys
from collections import Counter
from pathlib import Path

from beverage_lib import (
    DEFAULT_ENCODING,
    grain_key,
    measures,
    normalize_fieldnames,
    row_in_period,
    is_product_row,
)

WAREHOUSE_SPIKE_MIN = 50.0
LOW_OTHER_CHANNELS_MAX = 5.0

ANOMALY_COLUMNS = [
    "YEAR",
    "MONTH",
    "SUPPLIER",
    "ITEM CODE",
    "ITEM DESCRIPTION",
    "ITEM TYPE",
    "RETAIL SALES",
    "RETAIL TRANSFERS",
    "WAREHOUSE SALES",
    "total_units",
    "reason",
]


def main() -> None:
    parser = argparse.ArgumentParser(description="Anomaly review (stdout).")
    parser.add_argument("--input", required=True)
    parser.add_argument("--delimiter", default=",")
    parser.add_argument("--encoding", default=DEFAULT_ENCODING)
    parser.add_argument("--year", type=int, default=None)
    parser.add_argument("--month", type=int, default=None)
    parser.add_argument("--product-only", action="store_true")
    parser.add_argument(
        "--warehouse-spike-min",
        type=float,
        default=WAREHOUSE_SPIKE_MIN,
        help="Min WAREHOUSE SALES for spike rule",
    )
    parser.add_argument(
        "--low-other-max",
        type=float,
        default=LOW_OTHER_CHANNELS_MAX,
        help="Max retail+transfers for warehouse spike",
    )
    parser.add_argument(
        "--no-detail",
        action="store_true",
        help="Skip per-row anomaly CSV block on stdout (counts only)",
    )
    args = parser.parse_args()

    input_path = Path(args.input)
    if not input_path.is_file():
        print(f"ERROR: input not found: {input_path}", file=sys.stderr)
        sys.exit(1)

    print("=== Schema summary ===")
    print(f"input: {input_path}")
    print(f"warehouse_spike_min: {args.warehouse_spike_min}")
    print(f"low_other_max: {args.low_other_max}")
    print("output: stdout only")

    key_counts: Counter[tuple[str, str, str, str]] = Counter()
    rows_for_pass2: list[dict[str, str]] = []

    with input_path.open(newline="", encoding=args.encoding) as f:
        reader = csv.DictReader(f, delimiter=args.delimiter)
        reader.fieldnames = normalize_fieldnames(reader.fieldnames)
        for row in reader:
            if not row_in_period(row, args.year, args.month):
                continue
            if not is_product_row(row, args.product_only):
                continue
            key_counts[grain_key(row)] += 1
            rows_for_pass2.append(row)

    duplicate_keys = {k for k, c in key_counts.items() if c > 1}
    print(f"rows_in_scope: {len(rows_for_pass2)}")
    print(f"duplicate_keys: {len(duplicate_keys)}")

    reason_counts: Counter[str] = Counter()
    anomaly_records: list[dict[str, str]] = []

    for row in rows_for_pass2:
        retail, transfers, warehouse, total = measures(row)
        reasons: list[str] = []

        if grain_key(row) in duplicate_keys:
            reasons.append("duplicate_key")
        if total <= 0:
            reasons.append("inactive")
        if retail < 0 or transfers < 0 or warehouse < 0:
            reasons.append("negative_qty")
        if warehouse >= args.warehouse_spike_min and (retail + transfers) <= args.low_other_max:
            reasons.append("warehouse_spike")
        if total > 0 and retail > 0 and warehouse == 0 and transfers == 0:
            reasons.append("retail_only")

        if not reasons:
            continue

        for r in reasons:
            reason_counts[r] += 1
        anomaly_records.append(
            {
                "YEAR": row.get("YEAR", ""),
                "MONTH": row.get("MONTH", ""),
                "SUPPLIER": row.get("SUPPLIER", ""),
                "ITEM CODE": row.get("ITEM CODE", ""),
                "ITEM DESCRIPTION": row.get("ITEM DESCRIPTION", ""),
                "ITEM TYPE": row.get("ITEM TYPE", ""),
                "RETAIL SALES": row.get("RETAIL SALES", ""),
                "RETAIL TRANSFERS": row.get("RETAIL TRANSFERS", ""),
                "WAREHOUSE SALES": row.get("WAREHOUSE SALES", ""),
                "total_units": f"{total:.4f}",
                "reason": "|".join(reasons),
            }
        )

    print(f"anomaly_rows: {len(anomaly_records)}")
    print("\n=== Reason counts (row may have multiple) ===")
    for reason, cnt in reason_counts.most_common():
        print(f"  {reason}: {cnt}")

    if False and not args.no_detail and anomaly_records:
        print("\n=== anomaly_review (CSV) ===")
        writer = csv.DictWriter(sys.stdout, fieldnames=ANOMALY_COLUMNS, lineterminator="\n")
        writer.writeheader()
        writer.writerows(anomaly_records)


if __name__ == "__main__":
    main()
