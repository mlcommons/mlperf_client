```
=== Schema summary === input: Warehouse_and_Retail_Sales.csv delimiter: ‘,’ encoding: utf-8-sig period: ALL product-only: False top_n_skus: 10 output: stdout only columns: YEAR, MONTH, SUPPLIER, ITEM CODE, ITEM DESCRIPTION, ITEM TYPE, RETAIL SALES, RETAIL TRANSFERS, WAREHOUSE SALES

=== Row counts === rows_scanned (after filters): 307645 distinct_suppliers: 397 distinct_item_codes: 34056 distinct_item_types: 9 year_range: 2017 … 2020 month_range: 1 … 12 zero_activity_rows: 2154 single_channel_rows: 172215 parse_or_negative_flags: 1839

=== Total units by ITEM TYPE === BEER 7668171.04 WINE 2638101.54 LIQUOR 1692333.41 KEGS 118430.00 NON-ALCOHOL 86900.28 STR_SUPPLIES 13587.46 (blank) 1.00 REF -19446.45 DUNNAGE -121454.00

=== Top 10 SKUs by total_units === 23445 352574.83 CORONA EXTRA LOOSE NR - 12OZ 96750 266992.08 CORONA EXTRA 2/12 NR - 12OZ 23886 206675.17 HEINEKEN LOOSE NR - 12OZ 96970 169564.90 HEINEKEN 2/12 NR - 12OZ 90590 162971.40 MILLER LITE 30PK CAN - 12OZ 96741 140151.32 CORONA EXTRA 4/6 NR - 12OZ 25883 126634.80 MODELO ESPECIAL 24 LOOSE NR - 12OZ 90468 120735.97 BUD LIGHT 30PK CAN 96083 110962.92 HEINEKEN 4/6 NR - 12OZ 23314 100913.44 CORONA EXTRA 18PK NR - 12OZ
``
