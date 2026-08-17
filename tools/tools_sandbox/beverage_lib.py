"""Shared CSV parsing helpers for beverage movement scripts (stdlib only)."""
from __future__ import annotations

import csv
from pathlib import Path
from typing import Any, Iterator

DEFAULT_ENCODING = "utf-8-sig"

PERSONA_DOMINANCE = 0.60
TRANSFER_HEAVY = 0.50


def parse_qty(value: str | None) -> float:
    if value is None:
        return 0.0
    s = value.strip()
    if not s:
        return 0.0
    try:
        return float(s)
    except ValueError:
        return 0.0


def normalize_fieldnames(fieldnames: list[str] | None) -> list[str]:
    if not fieldnames:
        return []
    return [(n or "").strip() for n in fieldnames]


def row_in_period(row: dict[str, str], year: int | None, month: int | None) -> bool:
    if year is not None:
        try:
            if int(row["YEAR"]) != year:
                return False
        except (KeyError, ValueError):
            return False
    if month is not None:
        try:
            if int(row["MONTH"]) != month:
                return False
        except (KeyError, ValueError):
            return False
    return True


def measures(row: dict[str, str]) -> tuple[float, float, float, float]:
    retail = parse_qty(row.get("RETAIL SALES"))
    transfers = parse_qty(row.get("RETAIL TRANSFERS"))
    warehouse = parse_qty(row.get("WAREHOUSE SALES"))
    total = retail + transfers + warehouse
    return retail, transfers, warehouse, total


def channel_persona(retail: float, transfers: float, warehouse: float) -> str:
    total = retail + transfers + warehouse
    if total <= 0:
        return "inactive"
    pr = retail / total
    pt = transfers / total
    pw = warehouse / total
    if pw >= PERSONA_DOMINANCE:
        return "warehouse_led"
    if pr >= PERSONA_DOMINANCE:
        return "retail_led"
    if pt >= TRANSFER_HEAVY:
        return "transfer_heavy"
    return "balanced"


def is_product_row(row: dict[str, str], product_only: bool) -> bool:
    if not product_only:
        return True
    item_type = (row.get("ITEM TYPE") or "").strip()
    supplier = (row.get("SUPPLIER") or "").strip()
    if item_type == "STR_SUPPLIES":
        return False
    if supplier == "Default":
        return False
    return True


def grain_key(row: dict[str, str]) -> tuple[str, str, str, str]:
    return (
        (row.get("YEAR") or "").strip(),
        (row.get("MONTH") or "").strip(),
        (row.get("SUPPLIER") or "").strip(),
        (row.get("ITEM CODE") or "").strip(),
    )


def iter_rows(
    input_path: Path,
    delimiter: str,
    encoding: str,
    year: int | None,
    month: int | None,
    product_only: bool,
) -> Iterator[dict[str, str]]:
    with input_path.open(newline="", encoding=encoding) as f:
        reader = csv.DictReader(f, delimiter=delimiter)
        reader.fieldnames = normalize_fieldnames(reader.fieldnames)
        for row in reader:
            if not row_in_period(row, year, month):
                continue
            if not is_product_row(row, product_only):
                continue
            yield row


def ensure_output_dir(project_root: Path) -> Path:
    out = project_root / "output"
    out.mkdir(parents=True, exist_ok=True)
    return out


def project_root_from_script() -> Path:
    return Path(__file__).resolve().parent.parent
