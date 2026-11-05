# Benchmark results

Each figure is the median of the timed runs after one warm-up run, from
`matmul_bench bench <kernel> <n> <repeats>`. Matrices are square n x n float32.
GFLOP/s = 2n³ / median time.

## Run 1: naive vs reordered (2026-11-05)

**Setup**

| | |
|---|---|
| Commit | `6ad88b6` (feat: i-k-j reordered kernel) |
| CPU | Intel Core Ultra 7 165H (hybrid P/E cores, AVX2: 8 floats per vector instruction) |
| RAM | 16 GB |
| OS | Windows 11 |
| Compiler | g++ 16.2.0 (MSYS2 UCRT64) |
| Build | CMake Release (`-O3`), `MATMUL_NATIVE=ON` (`-march=native`) |
| Repeats | 3 per size, plus 1 warm-up |

**Results**

| n | naive (ms) | naive (GFLOP/s) | reordered (ms) | reordered (GFLOP/s) | Speed-up |
|---|---|---|---|---|---|
| 256 | 13.33 | 2.52 | 1.09 | 30.89 | ×12.3 |
| 512 | 96.06 | 2.79 | 6.24 | 43.04 | ×15.4 |
| 1024 | 2516.62 | 0.85 | 71.72 | 29.94 | ×35.1 |

**Observations**

- `naive` falls from 2.8 to 0.85 GFLOP/s between n=512 and n=1024. Its inner loop
  reads B down a column, a 4 KB jump per step at n=1024, so once B no longer fits
  in cache almost every read waits on main memory.
- `reordered` (i-k-j) walks rows of B and C contiguously, so each cache line it
  loads is fully used, and the compiler can vectorise the inner loop (AVX2).
  `naive`'s inner loop adds into a single running total, which the compiler may
  not reorder without fast-math flags, so it stays scalar.
- `reordered` also drops from 43 to 30 GFLOP/s at n=1024, as the working set
  outgrows the cache. This is what the blocked kernel is meant to address.

**Caveats**

- Only 3 repeats on a laptop. Expect a few percent of run-to-run noise.
- The 165H mixes performance and efficiency cores, and Windows decides which one
  runs the benchmark. Until runs are pinned to a core, treat small differences
  as noise.
- Vectorisation is inferred from the size of the speed-up and not yet confirmed.
  A `-fopt-info-vec` build or a run with `MATMUL_NATIVE=OFF` would confirm it.
