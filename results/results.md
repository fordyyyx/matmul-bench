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

## Run 2: adding the blocked kernel (2026-11-05)

**Setup**

Same machine, compiler and build settings as Run 1.

| | |
|---|---|
| Commit | `9829877`. The run used the same kernel loops just before the `block == 0` input check was added; that check runs once per call and does not affect timing. |
| Kernels | `naive`, `reordered`, `blocked64` (`multiply_blocked` with 64 x 64 tiles) |
| Repeats | 3 per size, plus 1 warm-up |
| Note | `naive` skipped at n=2048 (about 10 s per run) |

**Results (GFLOP/s, higher is better)**

| n | naive | reordered | blocked64 | blocked64 vs reordered |
|---|---|---|---|---|
| 256 | 2.53 | **31.26** | 26.83 | ×0.86 |
| 512 | 2.78 | **40.96** | 31.84 | ×0.78 |
| 1024 | 0.84 | 28.45 | **31.04** | ×1.09 |
| 2048 | n/a | 20.51 | **29.13** | ×1.42 |

**Median times (ms)**

| n | naive | reordered | blocked64 |
|---|---|---|---|
| 256 | 13.24 | 1.07 | 1.25 |
| 512 | 96.43 | 6.55 | 8.43 |
| 1024 | 2559.71 | 75.47 | 69.18 |
| 2048 | n/a | 837.53 | 589.76 |

**Observations**

- Blocking is slower on small matrices. At n=256 and n=512 all three matrices
  already fit in cache, so `reordered` is not waiting on memory and blocking has
  nothing to fix. Its only effect is overhead: three extra loops, and an inner `j`
  loop cut to 64 elements, so the vectorised loop's setup and remainder handling
  run once per tile instead of once per row.
- Blocking wins once the data outgrows the cache. `reordered` falls from 41 to
  20.5 GFLOP/s between n=512 and n=2048, while `blocked64` stays roughly flat at
  29-31 GFLOP/s from n=512 to n=2048: each 64 x 64 tile (16 KB) is reused while it
  is still in cache, whatever the total matrix size.
- `naive` and `reordered` numbers agree with Run 1 to within a few percent,
  which gives a rough idea of run-to-run noise on this laptop.

**Next**

- 64 was a first guess. Sweep block sizes (e.g. 32, 128, 256) at n=1024 and
  n=2048 to find the best tile size for this CPU.
