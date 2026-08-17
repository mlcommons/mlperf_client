#!/usr/bin/env python3
"""Category channel mix and row-level channel personas (stdout only)."""
from __future__ import annotations

import argparse
import csv
import sys
from collections import Counter, defaultdict
from pathlib import Path

from beverage_lib import (
    DEFAULT_ENCODING,
    PERSONA_DOMINANCE,
    TRANSFER_HEAVY,
    channel_persona,
    iter_rows,
    measures,
)

OUT_COLUMNS = [
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
    "pct_retail",
    "pct_transfers",
    "pct_warehouse",
    "channel_persona",
]


def main() -> None:
    parser = argparse.ArgumentParser(description="Channel mix and personas.")
    parser.add_argument("--input", required=True)
    parser.add_argument("--delimiter", default=",")
    parser.add_argument("--encoding", default=DEFAULT_ENCODING)
    parser.add_argument("--year", type=int, default=None)
    parser.add_argument("--month", type=int, default=None)
    parser.add_argument("--product-only", action="store_true")
    parser.add_argument(
        "--persona-dominance",
        type=float,
        default=PERSONA_DOMINANCE,
        help="Min share for retail_led / warehouse_led (default 0.60)",
    )
    parser.add_argument(
        "--transfer-heavy",
        type=float,
        default=TRANSFER_HEAVY,
        help="Min transfer share for transfer_heavy (default 0.50)",
    )
    parser.add_argument(
        "--no-persona-detail",
        action="store_true",
        help="Skip per-row persona CSV block on stdout (summaries only)",
    )
    args = parser.parse_args()

    import beverage_lib as bl

    bl.PERSONA_DOMINANCE = args.persona_dominance
    bl.TRANSFER_HEAVY = args.transfer_heavy

    input_path = Path(args.input)
    if not input_path.is_file():
        print(f"ERROR: input not found: {input_path}", file=sys.stderr)
        sys.exit(1)

    print("=== Schema summary ===")
    print(f"input: {input_path}")
    print(f"period: YEAR={args.year} MONTH={args.month}" if args.year or args.month else "period: ALL")
    print(f"thresholds: persona_dominance={args.persona_dominance}, transfer_heavy={args.transfer_heavy}")
    print("output: stdout only")

    type_retail: Counter[str] = Counter()
    type_transfers: Counter[str] = Counter()
    type_warehouse: Counter[str] = Counter()
    persona_by_type: dict[str, Counter[str]] = defaultdict(Counter)
    detail_rows: list[dict[str, str]] = []
    row_count = 0

    for row in iter_rows(
        input_path, args.delimiter, args.encoding, args.year, args.month, args.product_only
    ):
        row_count += 1
        retail, transfers, warehouse, total = measures(row)
        itype = (row.get("ITEM TYPE") or "").strip() or "(blank)"
        type_retail[itype] += retail
        type_transfers[itype] += transfers
        type_warehouse[itype] += warehouse

        persona = channel_persona(retail, transfers, warehouse)
        persona_by_type[itype][persona] += 1

        if total > 0:
            pr, pt, pw = retail / total, transfers / total, warehouse / total
        else:
            pr = pt = pw = 0.0

        if not args.no_persona_detail:
            detail_rows.append(
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
                    "pct_retail": f"{pr:.4f}",
                    "pct_transfers": f"{pt:.4f}",
                    "pct_warehouse": f"{pw:.4f}",
                    "channel_persona": persona,
                }
            )

    print(f"\nrows_scanned: {row_count}")

    print("\n=== Channel totals by ITEM TYPE ===")
    print(
        f"{'ITEM TYPE':20s} {'retail':>12s} {'transfers':>12s} {'warehouse':>12s} {'total':>12s} {'%R':>7s} {'%T':>7s} {'%W':>7s}"
    )
    all_types = sorted(set(type_retail) | set(type_transfers) | set(type_warehouse))
    for itype in all_types:
        r = type_retail[itype]
        t = type_transfers[itype]
        w = type_warehouse[itype]
        tot = r + t + w
        if tot > 0:
            pr, pt, pw = 100 * r / tot, 100 * t / tot, 100 * w / tot
        else:
            pr = pt = pw = 0.0
        print(f"{itype:20s} {r:12.2f} {t:12.2f} {w:12.2f} {tot:12.2f} {pr:6.2f}% {pt:6.2f}% {pw:6.2f}%")

    print("\n=== Persona counts by ITEM TYPE ===")
    for itype in sorted(persona_by_type):
        counts = persona_by_type[itype]
        parts = ", ".join(f"{k}={counts[k]}" for k in sorted(counts))
        print(f"  {itype}: {parts}")

    if False and not args.no_persona_detail and detail_rows:
        print("\n=== channel_personas (CSV) ===")
        writer = csv.DictWriter(sys.stdout, fieldnames=OUT_COLUMNS, lineterminator="\n")
        writer.writeheader()
        writer.writerows(detail_rows)


if __name__ == "__main__":
    main()
