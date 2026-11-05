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

## Run 3: block-size sweep (2026-11-05)

**Setup**

Same machine, compiler and build settings as Run 1.

| | |
|---|---|
| Commit | `0517064` plus registry entries for `blocked32`, `blocked128` and `blocked256` in `src/kernels.cpp`, added for this run. Kernel code unchanged from Run 2. |
| Kernels | `reordered` (reference), `multiply_blocked` with square tiles of 32, 64, 128 and 256 |
| Repeats | **5** per point, plus 1 warm-up (Runs 1 and 2 used 3) |
| Correctness | All four block sizes pass the full C++ and Python test suites, including the 1, 7, 65 and 129 sizes that leave partial edge tiles |

**Results (GFLOP/s, higher is better)**

| n | reordered | blocked32 | blocked64 | blocked128 | blocked256 |
|---|---|---|---|---|---|
| 512 | 42.96 | 25.32 | 32.06 | 39.15 | **44.60** |
| 1024 | 28.97 | 24.68 | 31.92 | 35.44 | **38.45** |
| 2048 | 20.49 | 24.31 | 28.88 | 32.82 | **37.19** |

**Median times (ms)**

| n | reordered | blocked32 | blocked64 | blocked128 | blocked256 |
|---|---|---|---|---|---|
| 512 | 6.25 | 10.60 | 8.37 | 6.86 | 6.02 |
| 1024 | 74.13 | 87.03 | 67.27 | 60.59 | 55.85 |
| 2048 | 838.43 | 706.62 | 594.97 | 523.48 | 461.99 |

**Observations**

- Performance rises steadily with block size at every n. At n=2048, `blocked256`
  is ×1.81 faster than `reordered`, against ×1.42 for `blocked64` in Run 2.
- At n=512, `blocked256` (44.6) and `reordered` (43.0) are within noise of each
  other. With only two tiles per dimension, blocking changes little at that size.
- Small tiles lose. `blocked32` is slower than `reordered` at n=512 and n=1024 and
  only pulls ahead at n=2048. Its inner `j` loop is 32 floats, or 4 AVX2
  iterations, so the vector loop's setup and remainder cost a large share of each
  pass.
- A likely explanation for larger tiles winning: a 256 x 256 tile is 256 KB, so
  one tile each of A, B and C (768 KB) probably fits in the CPU's mid-level cache,
  and the longer inner loop amortises loop overhead. The cache sizes of this CPU
  have not been checked, so this is a hypothesis, not a measurement.
- `reordered` and `blocked64` agree with Run 2 to within about 5%, so the runs are
  consistent with each other.

**Next**

- The trend had not peaked at 256. Try 512 and 1024 to find where it turns over.
- Try non-square tiles, for example small in i and p but the full width in j, to
  give the inner loop the longest possible contiguous run.
