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

## Run 4: larger blocks, and n=4096 (2026-11-05)

**Setup**

Same machine, compiler and build settings as Run 1.

| | |
|---|---|
| Commit | `960beec` plus registry entries for `blocked512` and `blocked1024` in `src/kernels.cpp`, added for this run. Kernel code unchanged from Run 2. |
| Kernels | `reordered` (reference), `multiply_blocked` with square tiles of 256, 512 and 1024 |
| Sizes | n = 1024, 2048, 4096. n=4096 was added so that the large tiles still split the matrix into several tiles. |
| Repeats | 5 per point, plus 1 warm-up |
| Correctness | All block sizes pass the full C++ and Python test suites |

**Results (GFLOP/s, higher is better)**

| n | reordered | blocked256 | blocked512 | blocked1024 |
|---|---|---|---|---|
| 1024 | 30.10 | **40.43** | 38.17 | 29.29 |
| 2048 | 21.39 | **36.82** | 24.70 | 27.53 |
| 4096 | 8.40 | 22.21 | **22.93** | 21.47 |

**Median times (ms)**

| n | reordered | blocked256 | blocked512 | blocked1024 |
|---|---|---|---|---|
| 1024 | 71.34 | 53.12 | 56.27 | 73.33 |
| 2048 | 803.10 | 466.56 | 695.59 | 624.01 |
| 4096 | 16366.10 | 6186.81 | 5992.97 | 6401.58 |

**Observations**

- The best block size for this machine is about 256. It is fastest at n=1024 and
  n=2048. At n=4096, `blocked512` is 3% ahead, which is within run-to-run noise.
  Together with Run 3 (32 to 256 rising steadily), performance peaks at 256 and
  falls off above it.
- Going larger costs performance. At n=2048, `blocked512` falls to 24.7 GFLOP/s,
  a third below `blocked256`. A 512 x 512 tile is 1 MB, so one tile each of A, B
  and C (3 MB) probably no longer fits in the cache level that 256 x 256 tiles
  benefit from. Not yet measured; cache sizes for this CPU have not been checked.
- `blocked1024` at n=1024 is a single tile covering the whole matrix, so it is
  `reordered` with extra loop overhead. As expected, it matches `reordered`
  (29.3 vs 30.1 GFLOP/s).
- At n=4096 blocking matters most: `reordered` collapses to 8.4 GFLOP/s (16.4 s
  per multiply) and every blocked kernel is about ×2.6 faster.
- `reordered` and `blocked256` agree with Run 3 to within about 5% at n=1024 and
  n=2048.

**Open questions**

- At n=2048, `blocked1024` (27.5) beat `blocked512` (24.7), against the overall
  trend. This is a single run on a CPU with mixed performance and efficiency
  cores, so rerun both before drawing conclusions.
- Even `blocked256` drops from 36.8 GFLOP/s at n=2048 to 22.2 at n=4096. A likely
  cause: at n=4096 each row is exactly 16 KB, a power of two, so the rows of a
  tile can map to the same cache sets and evict each other (cache conflict
  misses). Not yet tested. Comparing n=4000 or n=4100 against n=4096 would
  confirm or rule it out.

## Run 5: power-of-two size test, n = 4000 vs 4096 vs 4100 (2026-11-05)

Tests the second open question from Run 4: is the drop at n=4096 caused by the
power-of-two row length rather than by the matrix size?

**Setup**

Same machine, compiler and build settings as Run 1.

| | |
|---|---|
| Commit | `b833c4f` (working copy clean) |
| Kernels | `blocked256` (best tile size from Runs 3 and 4), `reordered` (reference) |
| Sizes | n = 4000, 4096, 4100. Row lengths are 16,000, 16,384 (exactly 2¹⁴) and 16,400 bytes. |
| Repeats | 5 per point, plus 1 warm-up |

GFLOP/s already divides by the work (2n³), so the three sizes compare directly.

**Results**

| n | blocked256 (GFLOP/s) | blocked256 (ms) | reordered (GFLOP/s) | reordered (ms) |
|---|---|---|---|---|
| 4000 | **38.94** | 3286.93 | 10.72 | 11943.60 |
| 4096 | **19.29** | 7126.50 | 10.41 | 13201.40 |
| 4100 | **32.65** | 4222.07 | 10.03 | 13741.80 |

**Observations**

- Confirmed for `blocked256`: n=4096 is a slow outlier. It has only about 5% more
  elements than n=4000 yet takes ×2.2 as long, and GFLOP/s halves (38.9 to 19.3).
  It mostly recovers at n=4100.
- At n=4000, `blocked256` runs at 38.9 GFLOP/s, in line with n=1024 and n=2048
  (37-40 GFLOP/s). The drop to 22 GFLOP/s at n=4096 in Run 4 came from the
  power-of-two size, not from the matrices being large: blocked performance is
  flat up to at least n=4000.
- Likely mechanism: with 16,384-byte rows, the rows of a tile start at addresses
  that map to the same cache sets, so they evict each other even when the rest of
  the cache has room (conflict misses). At n=4000 consecutive rows are spread
  across different sets. n=4100 is only 4 floats off a power of two, which may be
  why it recovers most but not all of the way. This is inferred from timings;
  cache misses have not been measured directly (e.g. with `perf stat` under WSL).
- `reordered` hardly changes (10.0-10.7 GFLOP/s at all three sizes). At these
  sizes it misses cache on nearly every access anyway, so extra conflict misses
  make little difference. The effect only shows once blocking has removed the
  other misses.

**Noise**

- `reordered` at n=4096 ran at 10.41 GFLOP/s here and 8.40 in Run 4, about 20%
  apart on identical code. Long runs on this laptop vary more than short ones,
  probably from thermal throttling or scheduling across performance and
  efficiency cores. The ×2 effect for `blocked256` is well outside this range.

**Next**

- Fix: pad each row so the stride is not a power of two (e.g. 4096 + 16 floats),
  the "leading dimension" approach used by BLAS libraries. Give `Matrix` a row
  stride separate from `cols`, then rerun this comparison to check the n=4096
  slowdown disappears.
- Rerun `blocked512` vs `blocked1024` at n=2048 (the first open question from
  Run 4).

## Run 6: row padding, padded vs unpadded (2026-11-05)

Tests the fix proposed in Run 5: give `Matrix` a row stride separate from `cols`,
and pad rows so the stride is never a multiple of 4 KB.

**Setup**

Same machine and compiler as Run 1. Two builds of the same commit, differing only
in `MATMUL_PAD_ROWS`.

| | |
|---|---|
| Commit | `2dc0520` (working copy clean) |
| Padded build | `build/`: `MATMUL_PAD_ROWS=ON`, `MATMUL_NATIVE=ON`, Release |
| Unpadded build | `build-nopad/`: `MATMUL_PAD_ROWS=OFF`, `MATMUL_NATIVE=ON`, Release |
| Padding rule | Round the stride up to a multiple of 16 floats (one 64-byte cache line); if the result is a multiple of 1024 floats (4 KB), add another 16 |
| Kernel | `blocked256` |
| Repeats | 5 per point, plus 1 warm-up. The two builds were run alternately at each size so drift affects both equally. |
| Correctness | Both builds pass all 50 C++ tests and 33 Python checks |

**Strides used**

| n | padded stride (floats) | unpadded stride (floats) |
|---|---|---|
| 1024 | 1040 | 1024 |
| 2048 | 2064 | 2048 |
| 4000 | 4000 | 4000 |
| 4096 | 4112 | 4096 |
| 4100 | 4112 | 4100 |

**Results (blocked256, GFLOP/s, higher is better)**

| n | padded | unpadded | padded vs unpadded |
|---|---|---|---|
| 1024 | 38.14 | 39.16 | ×0.97 |
| 2048 | 37.53 | 36.34 | ×1.03 |
| 4000 | 36.98 | 37.13 | ×1.00 |
| 4096 | **37.03** | 18.90 | **×1.96** |
| 4100 | **36.27** | 31.75 | **×1.14** |

**Median times (ms)**

| n | padded | unpadded |
|---|---|---|
| 1024 | 56.31 | 54.84 |
| 2048 | 457.76 | 472.81 |
| 4000 | 3461.48 | 3447.26 |
| 4096 | 3711.45 | 7272.46 |
| 4100 | 3800.10 | 4341.37 |

**Observations**

- Padding fixes the n=4096 slowdown. The padded build runs at 37.0 GFLOP/s, against
  18.9 unpadded (×1.96), and takes 3.7 s instead of 7.3 s. It now matches n=4000
  (37.0 GFLOP/s), so the power-of-two outlier from Run 5 is gone.
- n=4100 also improves (31.8 to 36.3 GFLOP/s, ×1.14). Its unpadded stride of 4100
  floats is only 16 bytes off a multiple of 4 KB, so it suffered part of the same
  effect. The padded stride of 4112 floats moves each row by a full cache line.
- Where the unpadded stride was already fine, padding changes nothing: n=4000 gets
  the same stride in both builds and the same speed.
- At n=1024 and n=2048, padding changes the stride but not the speed (within 3%,
  inside run-to-run noise). Their unpadded rows are also multiples of 4 KB apart,
  yet they showed no slowdown in Runs 3 and 4. So the conflict effect depends on
  more than the stride being a multiple of 4 KB, probably how many tile rows map
  to the same cache sets at each size. Not investigated further here.
- With padding, `blocked256` holds 36-39 GFLOP/s from n=1024 to n=4100, roughly
  flat across a 64x increase in work per multiply.
- This supports the conflict-miss explanation from Run 5: changing only where rows
  start in memory, with identical arithmetic and kernel code, halves the run time
  at n=4096. Cache misses still have not been measured directly.

**Next**

- Rerun `blocked512` vs `blocked1024` at n=2048 (open since Run 4).
- Measure cache misses directly (e.g. `perf stat` under WSL) to confirm the
  conflict-miss explanation, rather than inferring it from timings.
