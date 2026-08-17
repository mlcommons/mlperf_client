Review of bootstrap_mean_ci
What works well

Empty data, invalid ci_level, and n_iterations < 1 are validated.
Percentile bootstrap with sorted bootstrap means and linear interpolation is standard.
Optional seed gives reproducibility.
Readability / maintainability

Issue	Suggestion
Magic 100 * alpha / 2 in callers
Name tails explicitly, e.g. lower_pct = 100 * (alpha / 2)
Resampling loop inline
Small helper _bootstrap_resample(x, rng)
list(data) copies input
Document that a copy is intentional (resampling does not mutate caller data)
_percentile only used here
Keep private; validate p in [0, 100] for clearer failures
No check on non-finite values
Reject nan/inf if you want strict numerical behavior
Corner cases to add

n_iterations — already >= 1; consider a upper bound only if you care about abuse (optional).
ci_level — already (0, 1); symmetric CIs need alpha/2 at each tail (your math is correct).
Non-finite data — sum/mean can propagate nan; raise ValueError if any value is not finite.
Single observation — valid: bootstrap resamples are all the same value; CI collapses to a point (correct).
_percentile with p outside [0, 100] — internal bug if wrong; assert or validate in _percentile.
Type of elements — Sequence[float] does not stop int in a typed list at runtime; optional float(v) coercion or explicit check.
Minor correctness note

Resampling uses rng.randrange(n) with replacement — correct for the nonparametric bootstrap of the mean.
For n_iterations == 1, percentiles are still defined but very unstable; no need to forbid unless you want to warn.

```
{
  "tool_use": "apply_patch", "input": { 
  "patch":

--- a/input.py
+++ b/input.py
@@ -6,6 +6,7 @@ from __future__ import annotations
 
 import argparse
+import math
 import random
 from typing import Iterable, Sequence
 
@@ -26,6 +27,8 @@ def _mean(values: Sequence[float]) -> float:
 def _percentile(sorted_data: list[float], p: float) -> float:
     """Linear-interpolation percentile; p in [0, 100]. data must be sorted.""
+    if not 0 <= p <= 100:
+        raise ValueError("p must be in [0, 100]")
     n = len(sorted_data)
     if n == 1:
         return sorted_data[0]
@@ -35,6 +38,22 @@ def _percentile(sorted_data: list[float], p: float) -> float:
     return sorted_data[f] + (k - f) * (sorted_data[c] - sorted_data[f])
 
 
+def _require_finite(data: Sequence[float]) -> list[float]:
+    x = list(data)
+    if not x:
+        raise ValueError("data must not be empty")
+    for i, v in enumerate(x):
+        if not math.isfinite(v):
+            raise ValueError(f"data[{i}] must be finite, got {v!r}")
+    return x
+
+
+def _bootstrap_resample(x: list[float], rng: random.Random) -> list[float]:
+    n = len(x)
+    return [x[rng.randrange(n)] for _ in range(n)]
+
+
 def bootstrap_mean_ci(
     data: Sequence[float],
     *,
@@ -48,22 +67,19 @@ def bootstrap_mean_ci(
 
     Returns (point_estimate, lower, upper).
     ""
-    x = list(data)
-    if not x:
-        raise ValueError("data must not be empty")
     if not 0 < ci_level < 1:
         raise ValueError("ci_level must be in (0, 1)")
     if n_iterations < 1:
         raise ValueError("n_iterations must be >= 1")
 
+    x = _require_finite(data)
     rng = random.Random(seed)
-    n = len(x)
-    boot_means: list[float] = []
-    for _ in range(n_iterations):
-        sample = [x[rng.randrange(n)] for _ in range(n)]
-        boot_means.append(_mean(sample))
-
+    boot_means = [_mean(_bootstrap_resample(x, rng)) for _ in range(n_iterations)]
     boot_means.sort()
+
     alpha = 1.0 - ci_level
-    lower = _percentile(boot_means, 100 * alpha / 2)
-    upper = _percentile(boot_means, 100 * (1 - alpha / 2))
-    return _mean(x), lower, upper
+    lower = _percentile(boot_means, 100.0 * (alpha / 2.0))
+    upper = _percentile(boot_means, 100.0 * (1.0 - alpha / 2.0))
+    return _mean(x), lower, upper


  }
}
```

Compile the code

```
{
 "type":"tool_use",
 "id":"toolu_01A09q90qw90lq917835lq8",
 "name":"execute",
 "input":{"command":"python -m py_compile tools_sandbox/bootstrap_mean_ci.py"}
}
```

Run unittest

```
{
 "type":"tool_use",
 "id":"toolu_01A09q90qw90lq917835lq8",
 "name":"execute",
 "input":{"command":"python tools_sandbox/bootstrap_test.py"}
}
```
