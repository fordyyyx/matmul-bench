"""Run the benchmark sweep behind the README chart and write it to one CSV.

Needs both builds (see README): build/ (row padding on, the default) and
build-nopad/ (-DMATMUL_PAD_ROWS=OFF). Takes a few minutes; plot.py then draws
the chart from the CSV without re-running anything.

    python python/sweep.py                      # writes results/readme_sweep.csv
    python python/sweep.py --repeats 5 --out other.csv
"""

from __future__ import annotations

import argparse
import csv
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

SIZES = [256, 512, 1024, 1536, 2048, 3072, 4000, 4096]

# (label shown in the chart, build directory, kernel name, sizes to run)
# naive stops at 1024: at 2048 a single multiply takes ~20 s.
SERIES = [
    ("blocked512 (padded)", "build", "blocked512", SIZES),
    ("blocked512 (unpadded)", "build-nopad", "blocked512", SIZES),
    ("reordered", "build", "reordered", SIZES),
    ("naive", "build", "naive", [256, 512, 1024]),
    # Table only (not plotted): shows how much of naive's n=1024 drop is conflict misses.
    ("naive (unpadded)", "build-nopad", "naive", [256, 512, 1024]),
]

FIELDS = ["label", "build", "kernel", "n", "repeats", "median_ms", "gflops"]


def executable(build: str) -> Path:
    for name in ("matmul_bench.exe", "matmul_bench"):
        path = ROOT / build / name
        if path.is_file():
            return path
    sys.exit(f"no matmul_bench in {ROOT / build}: build it first (see README)")


def bench(exe: Path, kernel: str, n: int, repeats: int) -> dict[str, str]:
    result = subprocess.run(
        [str(exe), "bench", kernel, str(n), str(repeats)],
        capture_output=True, text=True, check=True,
    )
    # matmul_bench prints one CSV row: kernel,n,repeats,median_ms,gflops
    kernel_out, n_out, repeats_out, median_ms, gflops = result.stdout.strip().split(",")
    return {"kernel": kernel_out, "n": n_out, "repeats": repeats_out,
            "median_ms": median_ms, "gflops": gflops}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--out", type=Path, default=ROOT / "results" / "readme_sweep.csv")
    args = parser.parse_args()

    # Check every build exists before spending minutes benchmarking.
    exes = {build: executable(build) for _, build, _, _ in SERIES}

    # Throwaway run first: lets the CPU reach its boost clock before anything is
    # recorded. Without it, whichever series ran first measured ~25% slow.
    bench(exes["build"], "blocked512", 1024, args.repeats)

    # Size in the outer loop, series in the inner loop, so any drift during the
    # sweep (heat, background load) affects every series at a given n equally
    # instead of penalising whichever series happens to run first.
    rows = []
    for n in sorted({n for _, _, _, sizes in SERIES for n in sizes}):
        for label, build, kernel, sizes in SERIES:
            if n not in sizes:
                continue
            row = {"label": label, "build": build, **bench(exes[build], kernel, n, args.repeats)}
            rows.append(row)
            print(f"{label:24} n={n:5}  {float(row['gflops']):6.2f} GFLOP/s", flush=True)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=FIELDS)
        writer.writeheader()
        writer.writerows(rows)
    print(f"wrote {len(rows)} rows to {args.out}")


if __name__ == "__main__":
    main()
