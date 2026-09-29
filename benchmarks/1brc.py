# 1brc.py — naive pure-Python 1BRC twin (CPython speed reference).
# Same algorithm as the oracle, but timed. Usage: python benchmarks/1brc.py FILE
import sys
import time

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")

path = sys.argv[1] if len(sys.argv) > 1 else "C:/Users/elias/Downloads/1brc-main/1brc-main/measurements.txt"

t0 = time.perf_counter()

stats = {}
rows = 0
with open(path, "rb") as fh:
    for raw in fh:
        name, temp = raw.split(b";", 1)
        t10 = int(round(float(temp) * 10))
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
        rows += 1

t1 = time.perf_counter()

out = []
for name in sorted(stats):
    c, mn, mx, total = stats[name]
    m10 = (2 * total + c) // (2 * c) if total >= 0 \
        else -((-2 * total + c) // (2 * c))
    out.append(f"{name.decode('utf-8')}={mn / 10:.1f}/{m10 / 10:.1f}/{mx / 10:.1f}")
print("{" + ", ".join(out) + "}")

ms = (t1 - t0) * 1000.0
print(f"1brc(py): {rows} rows in {ms:.1f} ms ({rows / (ms / 1000.0):.0f} rows/s)")
if rows < 1000000000:
    proj = ms / rows * 1000000000.0
    print(f"1brc(py): projected for 1e9 rows: {proj / 1000.0:.0f} s")
