02_channel_mix.pu output:

```
=== Schema summary ===
input: Warehouse_and_Retail_Sales.csv
period: ALL
thresholds: persona_dominance=0.6, transfer_heavy=0.5
output: stdout only

rows_scanned: 307645

=== Channel totals by ITEM TYPE ===
ITEM TYPE                  retail    transfers    warehouse        total      %R      %T      %W
(blank)                      0.00         0.00         1.00         1.00   0.00%   0.00% 100.00%
BEER                    574220.53    566714.00   6527236.51   7668171.04   7.49%   7.39%  85.12%
DUNNAGE                      0.00         0.00   -121454.00   -121454.00   0.00%   0.00%   0.00%
KEGS                         0.00        -1.00    118431.00    118430.00   0.00%  -0.00% 100.00%
LIQUOR                  802691.43    794735.71     94906.27   1692333.41  47.43%  46.96%   5.61%
NON-ALCOHOL              34084.31     26666.38     26149.59     86900.28  39.22%  30.69%  30.09%
REF                        663.63       388.92    -20499.00    -19446.45   0.00%   0.00%   0.00%
STR_SUPPLIES              2740.88     10846.58         0.00     13587.46  20.17%  79.83%   0.00%
WINE                    746498.59    734618.04   1156984.91   2638101.54  28.30%  27.85%  43.86%

=== Persona counts by ITEM TYPE ===
  (blank): warehouse_led=1
  BEER: balanced=4222, inactive=235, retail_led=2347, transfer_heavy=547, warehouse_led=35062
  DUNNAGE: inactive=95
  KEGS: inactive=248, warehouse_led=9898
  LIQUOR: balanced=15375, inactive=241, retail_led=28746, transfer_heavy=18663, warehouse_led=1885
  NON-ALCOHOL: balanced=701, inactive=15, retail_led=378, transfer_heavy=375, warehouse_led=439
  REF: balanced=5, inactive=49, retail_led=63, transfer_heavy=9, warehouse_led=1
  STR_SUPPLIES: balanced=34, retail_led=150, transfer_heavy=221
  WINE: balanced=39297, inactive=1271, retail_led=30487, transfer_heavy=12131, warehouse_led=104454
```

03_supplier_concentration.py output:

```
=== Schema summary ===
input: Warehouse_and_Retail_Sales.csv
product-only: False
top_n: 10
output: stdout only
rows_scanned: 307645
total_units: 12076624.28

concentration:
  share_top1: 15.06%
  share_top5: 53.29%

=== Top 10 suppliers by total_units ===
SUPPLIER                                                    units   share%
CROWN IMPORTS                                          1819141.93   15.06%
MILLER BREWING COMPANY                                 1597808.94   13.23%
ANHEUSER BUSCH INC                                     1549362.68   12.83%
HEINEKEN USA                                            940796.79    7.79%
E & J GALLO WINERY                                      528656.13    4.38%
DIAGEO NORTH AMERICA INC                                459921.54    3.81%
CONSTELLATION BRANDS                                    380826.10    3.15%
BOSTON BEER CORPORATION                                 271825.23    2.25%
THE WINE GROUP                                          200316.64    1.66%
JIM BEAM BRANDS CO                                      199336.22    1.65%

=== ITEM TYPE mix for top 5 suppliers ===

--- CROWN IMPORTS (total 1819141.93) ---
  BEER                   1815074.93  (99.8%)
  KEGS                      4067.00  (0.2%)

--- MILLER BREWING COMPANY (total 1597808.94) ---
  BEER                   1575269.24  (98.6%)
  KEGS                     21779.00  (1.4%)
  NON-ALCOHOL                760.70  (0.0%)

--- ANHEUSER BUSCH INC (total 1549362.68) ---
  BEER                   1558021.95  (100.6%)
  KEGS                     33024.00  (2.1%)
  NON-ALCOHOL              25841.73  (1.7%)
  DUNNAGE                 -67525.00  (-4.4%)

--- HEINEKEN USA (total 940796.79) ---
  BEER                    934506.26  (99.3%)
  NON-ALCOHOL               3634.53  (0.4%)
  KEGS                      2656.00  (0.3%)

--- E & J GALLO WINERY (total 528656.13) ---
  WINE                    462401.81  (87.5%)
  LIQUOR                   66215.02  (12.5%)
  BEER                        39.30  (0.0%)
```
