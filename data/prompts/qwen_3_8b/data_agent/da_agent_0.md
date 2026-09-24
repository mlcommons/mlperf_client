Dataset Structure Report

Files: tools_sandbox/Warehouse_and_Retail_Sales.csv
Format: Comma (,), UTF-8 encoding, quoted fields observed: Yes
Grain: One row per sale transaction
Columns:
NAME	TYPE	NOTES
SUPPLIER	string	Some rows have “Default” or “STR_SUPPLIES”
ITEM CODE	string	May include single spaces (e.g., "ITEM A ")
ITEM TYPE	string	Categories like “Beverage”, “Food”, etc.
RETAIL SALES	float	Units sold to retail customers
WAREHOUSE TRANSFER	float	Units transferred between warehouses
YEAR	integer	Four-digit year of sale
MONTH	integer	One- or two-digit month (not zero-padded)
Measures: The three sales columns + derived total_units
Time coverage: Sample rows indicate data for the years 2020 and 2021, across all months
Quality flags:
nulls present in SUPPLIER column
Mixed types (string/numeric) in ITEM CODE
Extreme warehouse values (>100 units)
Exclusions recommended: STR_SUPPLIES and Default supplier rows for product-only analysis
Recommended scripts:
01_profile_csv.py: Row counts, distinct counts, time range, totals by ITEM TYPE, top N SKUs by total_units
02_channel_mix.py: Category-level channel shares; row-level persona label (retail_led, warehouse_led, transfer_heavy, balanced)
03_supplier_concentration.py: Aggregate total_units by SUPPLIER; top 10 suppliers; top1/top5 share of total
04_anomalies.py: Warehouse spike rule, inactive rows, negative quantities, duplicate keys
Next action: Run 01_profile_csv.py to gain insights into the dataset’s overall structure and distribution.

```
{
 "type":"tool_use",
 "id":"toolu_01A09q90qw90lq917835lq8",
 "name":"execute",
 "input":{"command":"python tools_sandbox/01_profile_csv.py --input tools_sandbox/Warehouse_and_Retail_Sales.csv"}
}
```
