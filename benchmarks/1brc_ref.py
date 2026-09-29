#!/usr/bin/env python3
"""1brc_ref.py — trusted oracle for the HolyPHP/PHP/C/Python 1BRC benchmarks.

Prints the same {Name=min/mean/max, ...} line (without timing suffixes) that
the implementations must produce. Follows the official 1BRC rounding rule:
exact integer-tenths accumulation, mean rounded half-up on the exact value,
so results are byte-identical across languages.

Usage: python benchmarks/1brc_ref.py [measurements.txt]
"""
import sys

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")

path = sys.argv[1] if len(sys.argv) > 1 else "C:/Users/elias/Downloads/1brc-main/1brc-main/measurements.txt"

stats = {}
rows = 0
with open(path, "rb") as fh:
    for raw in fh:
        line = raw.decode("utf-8").strip()
        if not line:
            continue
        rows += 1
        name, temp = line.split(";", 1)
        t10 = int(round(float(temp) * 10))   # exact integer tenths
        st = stats.get(name)
        if st is None:
            stats[name] = [1, t10, t10, t10]
        else:
            st[0] += 1
            if t10 < st[1]:
                st[1] = t10
            if t10 > st[2]:
                st[2] = t10
            st[3] += t10

out = []
for name in sorted(stats):
    c, mn, mx, total = stats[name]
    # mean in exact tenths, rounded half-up (away from zero)
    m10 = (2 * total + c) // (2 * c) if total >= 0 \
        else -((-2 * total + c) // (2 * c))
    out.append(f"{name}={mn / 10:.1f}/{m10 / 10:.1f}/{mx / 10:.1f}")
print("{" + ", ".join(out) + "}")
print(f"rows={rows}", file=sys.stderr)
