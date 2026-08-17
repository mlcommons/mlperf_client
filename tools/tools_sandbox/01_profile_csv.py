#!/usr/bin/env python3
"""Profile beverage CSV: counts, time range, totals by ITEM TYPE, top SKUs."""
from __future__ import annotations

import argparse
import csv
import sys
from collections import Counter
from pathlib import Path

from beverage_lib import (
    DEFAULT_ENCODING,
    is_product_row,
    measures,
    normalize_fieldnames,
    row_in_period,
)

TOP_N_SKUS = 10


def main() -> None:
    parser = argparse.ArgumentParser(description="Profile beverage movement CSV.")
    parser.add_argument("--input", required=True, help="Path to input CSV")
    parser.add_argument("--delimiter", default=",", help="Field delimiter")
    parser.add_argument("--encoding", default=DEFAULT_ENCODING, help="File encoding")
    parser.add_argument("--year", type=int, default=None, help="Filter YEAR")
    parser.add_argument("--month", type=int, default=None, help="Filter MONTH (1-12)")
    parser.add_argument("--product-only", action="store_true", help="Exclude STR_SUPPLIES and Default supplier")
    parser.add_argument(
        "--top-n",
        type=int,
        default=TOP_N_SKUS,
        help=f"Top N SKUs by total_units (default {TOP_N_SKUS})",
    )
    args = parser.parse_args()

    input_path = Path(args.input)
    if not input_path.is_file():
        print(f"ERROR: input not found: {input_path}", file=sys.stderr)
        sys.exit(1)

    print("=== Schema summary ===")
    print(f"input:     {input_path}")
    print(f"delimiter: {repr(args.delimiter)}")
    print(f"encoding:  {args.encoding}")
    period = []
    if args.year is not None:
        period.append(f"YEAR={args.year}")
    if args.month is not None:
        period.append(f"MONTH={args.month}")
    print(f"period:    {', '.join(period) if period else 'ALL'}")
    print(f"product-only: {args.product_only}")
    print(f"top_n_skus: {args.top_n}")
    print("output: stdout only")

    row_count = 0
    zero_activity = 0
    single_channel = 0
    parse_issues = 0
    suppliers: set[str] = set()
    skus: set[str] = set()
    item_types: set[str] = set()
    years: list[int] = []
    months: list[int] = []
    by_type: Counter[str] = Counter()
    sku_totals: Counter[str] = Counter()

    with input_path.open(newline="", encoding=args.encoding) as f:
        reader = csv.DictReader(f, delimiter=args.delimiter)
        reader.fieldnames = normalize_fieldnames(reader.fieldnames)
        print(f"columns:   {', '.join(reader.fieldnames or [])}")

        for row in reader:
            if not row_in_period(row, args.year, args.month):
                continue
            if not is_product_row(row, args.product_only):
                continue

            row_count += 1
            suppliers.add((row.get("SUPPLIER") or "").strip())
            skus.add((row.get("ITEM CODE") or "").strip())
            item_types.add((row.get("ITEM TYPE") or "").strip())

            try:
                y = int((row.get("YEAR") or "").strip())
                m = int((row.get("MONTH") or "").strip())
                years.append(y)
                months.append(m)
            except ValueError:
                parse_issues += 1

            retail, transfers, warehouse, total = measures(row)
            if retail < 0 or transfers < 0 or warehouse < 0:
                parse_issues += 1
            itype = (row.get("ITEM TYPE") or "").strip() or "(blank)"
            by_type[itype] += total

            code = (row.get("ITEM CODE") or "").strip()
            desc = (row.get("ITEM DESCRIPTION") or "").strip()[:40]
            sku_totals[f"{code}|{desc}"] += total

            if total <= 0:
                zero_activity += 1
            else:
                nonzero = sum(1 for x in (retail, transfers, warehouse) if x > 0)
                if nonzero == 1:
                    single_channel += 1

    print("\n=== Row counts ===")
    print(f"rows_scanned (after filters): {row_count}")
    print(f"distinct_suppliers: {len(suppliers)}")
    print(f"distinct_item_codes: {len(skus)}")
    print(f"distinct_item_types: {len(item_types)}")
    if years:
        print(f"year_range: {min(years)} .. {max(years)}")
    if months:
        print(f"month_range: {min(months)} .. {max(months)}")
    print(f"zero_activity_rows: {zero_activity}")
    print(f"single_channel_rows: {single_channel}")
    print(f"parse_or_negative_flags: {parse_issues}")

    print("\n=== Total units by ITEM TYPE ===")
    for itype, vol in by_type.most_common():
        print(f"  {itype:20s}  {vol:14.2f}")

    print(f"\n=== Top {args.top_n} SKUs by total_units ===")
    for key, vol in sku_totals.most_common(args.top_n):
        code, desc = key.split("|", 1)
        print(f"  {code:10s}  {vol:12.2f}  {desc}")


if __name__ == "__main__":
    main()
