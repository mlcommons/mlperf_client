#!/usr/bin/env python3
"""Supplier volume concentration and ITEM TYPE breakdown for top suppliers."""
from __future__ import annotations

import argparse
import sys
from collections import Counter, defaultdict
from pathlib import Path

from beverage_lib import DEFAULT_ENCODING, iter_rows, measures

TOP_N_SUPPLIERS = 10


def main() -> None:
    parser = argparse.ArgumentParser(description="Supplier concentration analysis.")
    parser.add_argument("--input", required=True)
    parser.add_argument("--delimiter", default=",")
    parser.add_argument("--encoding", default=DEFAULT_ENCODING)
    parser.add_argument("--year", type=int, default=None)
    parser.add_argument("--month", type=int, default=None)
    parser.add_argument("--product-only", action="store_true")
    parser.add_argument(
        "--top-n",
        type=int,
        default=TOP_N_SUPPLIERS,
        help=f"Top N suppliers (default {TOP_N_SUPPLIERS})",
    )
    args = parser.parse_args()

    input_path = Path(args.input)
    if not input_path.is_file():
        print(f"ERROR: input not found: {input_path}", file=sys.stderr)
        sys.exit(1)

    print("=== Schema summary ===")
    print(f"input: {input_path}")
    print(f"product-only: {args.product_only}")
    print(f"top_n: {args.top_n}")
    print("output: stdout only")

    by_supplier: Counter[str] = Counter()
    supplier_type: dict[str, Counter[str]] = defaultdict(Counter)
    total_volume = 0.0
    rows = 0

    for row in iter_rows(
        input_path, args.delimiter, args.encoding, args.year, args.month, args.product_only
    ):
        rows += 1
        supplier = (row.get("SUPPLIER") or "").strip() or "(blank)"
        _, _, _, total = measures(row)
        by_supplier[supplier] += total
        itype = (row.get("ITEM TYPE") or "").strip() or "(blank)"
        supplier_type[supplier][itype] += total
        total_volume += total

    print(f"rows_scanned: {rows}")
    print(f"total_units: {total_volume:.2f}")

    if total_volume <= 0:
        print("No positive volume in scope.")
        return

    ranked = by_supplier.most_common()
    top1 = ranked[0][1] if ranked else 0.0
    top5 = sum(v for _, v in ranked[:5])
    print(f"\nconcentration:")
    print(f"  share_top1: {100 * top1 / total_volume:.2f}%")
    print(f"  share_top5: {100 * top5 / total_volume:.2f}%")

    print(f"\n=== Top {args.top_n} suppliers by total_units ===")
    print(f"{'SUPPLIER':50s} {'units':>14s} {'share%':>8s}")
    for name, vol in ranked[: args.top_n]:
        print(f"{name[:50]:50s} {vol:14.2f} {100 * vol / total_volume:7.2f}%")

    print("\n=== ITEM TYPE mix for top 5 suppliers ===")
    for name, vol in ranked[:5]:
        print(f"\n--- {name} (total {vol:.2f}) ---")
        for itype, tvol in supplier_type[name].most_common():
            pct = 100 * tvol / vol if vol > 0 else 0.0
            print(f"  {itype:20s} {tvol:12.2f}  ({pct:.1f}%)")


if __name__ == "__main__":
    main()
