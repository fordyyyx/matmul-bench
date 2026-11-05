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
  > **Corrected in Run 9:** with row padding, `naive` runs at 4.2 GFLOP/s at n=1024 (×4.8), so this drop was mainly conflict misses from the exact 4 KB stride, not B outgrowing the cache.
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
  > **Note (Run 8):** with row padding the trend is not steady: 128 is slower than 64 at n=1024 and 2048, and 512 is best.
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
  > **Note (Run 7):** this holds for the unpadded layout only; with row padding, `blocked512` is fastest at n=2048.
- Going larger costs performance. At n=2048, `blocked512` falls to 24.7 GFLOP/s,
  a third below `blocked256`. A 512 x 512 tile is 1 MB, so one tile each of A, B
  and C (3 MB) probably no longer fits in the cache level that 256 x 256 tiles
  benefit from. Not yet measured; cache sizes for this CPU have not been checked.
  > **Corrected in Run 7:** padding removes most of this drop, so it was mainly conflict misses, not cache capacity.
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
  > **Note (Run 7):** over three rounds, padding does help at n=2048 (`blocked256` 37.9 vs 33.7 GFLOP/s, `blocked512` ×1.72), so this single measurement understated it.
- With padding, `blocked256` holds 36-39 GFLOP/s from n=1024 to n=4100, roughly
  flat across a 64x increase in work per multiply.
- This supports the conflict-miss explanation from Run 5: changing only where rows
  start in memory, with identical arithmetic and kernel code, halves the run time
  at n=4096. Cache misses still have not been measured directly.

**Next**

- Rerun `blocked512` vs `blocked1024` at n=2048 (open since Run 4).
- Measure cache misses directly (e.g. `perf stat` under WSL) to confirm the
  conflict-miss explanation, rather than inferring it from timings.

## Run 7: blocked512 vs blocked1024 at n=2048, repeated (2026-11-05)

Repeats the first open question from Run 4, where `blocked1024` (27.5 GFLOP/s)
beat `blocked512` (24.7) at n=2048 against the overall trend. That was a single
measurement on an unpadded build, so this run repeats it three times, on both the
unpadded layout Run 4 used and the padded layout from Run 6.

**Setup**

Same machine and compiler as Run 1.

| | |
|---|---|
| Commit | `825ebcf` (working copy clean) |
| Builds | `build-nopad/` (`MATMUL_PAD_ROWS=OFF`, the layout Run 4 used) and `build/` (`MATMUL_PAD_ROWS=ON`). Both `MATMUL_NATIVE=ON`, Release. |
| Kernels | `blocked512`, `blocked1024`, plus `blocked256` for reference |
| n | 2048 |
| Repeats | 3 rounds. Each round runs every kernel on both builds; each figure is the median of 5 timed runs after 1 warm-up. |

**Results (GFLOP/s, higher is better)**

| Build | Round | blocked256 | blocked512 | blocked1024 |
|---|---|---|---|---|
| unpadded | 1 | 32.68 | 23.86 | 26.14 |
| unpadded | 2 | 33.90 | 23.61 | 25.97 |
| unpadded | 3 | 34.50 | 23.98 | 27.04 |
| **unpadded** | **mean** | **33.69** | **23.82** | **26.38** |
| padded | 1 | 37.73 | 41.44 | 24.84 |
| padded | 2 | 37.95 | 40.71 | 25.24 |
| padded | 3 | 38.13 | 40.41 | 25.00 |
| **padded** | **mean** | **37.94** | **40.85** | **25.03** |

**Median times (ms), mean of the 3 rounds**

| Build | blocked256 | blocked512 | blocked1024 |
|---|---|---|---|
| unpadded | 510.11 | 721.40 | 651.41 |
| padded | 452.89 | 420.58 | 686.48 |

**Observations**

- The Run 4 result is reproducible, not noise. On the unpadded build,
  `blocked1024` beat `blocked512` in all three rounds (26.4 vs 23.8 GFLOP/s on
  average), and each kernel varied by only about 1 GFLOP/s between rounds.
- Padding transforms `blocked512`: 23.8 to 40.9 GFLOP/s (×1.72). With padding it
  is the fastest kernel at n=2048, ahead of `blocked256` (37.9).
- **Correction to Run 4.** Run 4 suggested `blocked512` dropped at n=2048 because
  three 512 x 512 tiles (3 MB) no longer fit in cache. Padding changes only where
  rows start in memory, not how much data each tile holds, yet it removes the
  drop. So the drop was mainly conflict misses (rows of a tile competing for the
  same cache sets), the same effect as at n=4096 in Runs 5 and 6, not a capacity
  limit. It is plausibly worse for 512 than 256 because a taller tile puts more
  rows into the same sets. Inferred from timings; not measured.
- `blocked1024` is the opposite case: about 25 GFLOP/s with or without padding.
  Three 1024 x 1024 tiles come to about 12 MB, so it is limited by cache capacity,
  which padding cannot fix. The two kernels now separate the two causes: 512 was
  limited by conflicts, 1024 by capacity.
- This also explains the Run 6 puzzle of why unpadded n=2048 seemed unaffected.
  It was affected, but only mildly with 256-row tiles: `blocked256` is 37.9
  GFLOP/s padded against 33.7 unpadded here. (Run 6 measured 37.5 vs 36.3 at
  this size, a smaller gap; run-to-run variation on this laptop is a few
  GFLOP/s.)

**Consequence**

The best-tile-size conclusion from Runs 3 and 4 ("256 is best") was measured on
the unpadded layout, where conflict misses penalise tall tiles. It does not
necessarily hold for the padded layout, which is now the default.

**Next**

- Rerun the block-size sweep (32 to 1024 at n = 1024, 2048 and 4096) on the
  padded build to find the real best tile size.
- Measure cache misses directly (e.g. `perf stat` under WSL) to confirm the
  conflict-miss vs capacity explanation.

## Run 8: block-size sweep with row padding (2026-11-05)

Reruns the block-size sweep from Runs 3 and 4 on the padded layout, as Run 7
recommended, to find the best tile size now that conflict misses are removed.

**Setup**

Same machine and compiler as Run 1.

| | |
|---|---|
| Commit | `cc315b5` (only `results/results.md` modified; code unchanged) |
| Build | `build/`: `MATMUL_PAD_ROWS=ON`, `MATMUL_NATIVE=ON`, Release |
| Kernels | `reordered` (reference), `multiply_blocked` with square tiles of 32, 64, 128, 256, 512 and 1024 |
| Sizes | n = 1024, 2048, 4096 |
| Repeats | 5 per point, plus 1 warm-up. One round; Run 7 put round-to-round variation at about 1 GFLOP/s. |

**Results (GFLOP/s, higher is better)**

| n | reordered | blocked32 | blocked64 | blocked128 | blocked256 | blocked512 | blocked1024 |
|---|---|---|---|---|---|---|---|
| 1024 | 31.50 | 24.27 | 37.78 | 34.52 | 38.94 | **41.48** | 29.64 |
| 2048 | 20.00 | 24.63 | 36.44 | 34.04 | 37.80 | **41.14** | 25.87 |
| 4096 | 10.73 | 24.45 | 33.32 | 33.72 | 37.10 | **40.35** | 23.66 |

**Median times (ms)**

| n | reordered | blocked32 | blocked64 | blocked128 | blocked256 | blocked512 | blocked1024 |
|---|---|---|---|---|---|---|---|
| 1024 | 68.18 | 88.49 | 56.85 | 62.21 | 55.15 | 51.78 | 72.45 |
| 2048 | 859.04 | 697.47 | 471.41 | 504.70 | 454.55 | 417.57 | 664.11 |
| 4096 | 12810.20 | 5621.34 | 4124.85 | 4076.30 | 3704.10 | 3406.38 | 5809.36 |

**Observations**

- With padding, the best tile size is 512, at every size tested: 41.5, 41.1 and
  40.3 GFLOP/s. `blocked256` is second at all three (37-39). This replaces the
  "256 is best" result from Runs 3 and 4, which was measured on the unpadded
  layout.
- `blocked512` is nearly flat from n=1024 to n=4096 (41.5 to 40.3 GFLOP/s), a 64x
  increase in work per multiply. At n=4096 it is ×3.76 faster than `reordered`
  (3.4 s against 12.8 s).
- `blocked1024` is the slowest large tile, and falls as n grows (29.6, 25.9,
  23.7). This fits Run 7's finding that it is capacity-limited: three 1024 x 1024
  tiles come to about 12 MB. At n=1024 it is a single tile, and matches
  `reordered` (29.6 vs 31.5) as in Run 4.
- `blocked32` is stuck at about 24 GFLOP/s at every size. Its 32-float inner loop
  (4 AVX2 iterations) seems to cap it at a fixed fraction of the faster kernels,
  independent of n.
- Padding does not help `reordered`: 10.7 GFLOP/s at n=4096 here, against 10.4
  unpadded in Run 5. As Run 5 found, it misses cache on almost every access anyway.

**Open questions**

- `blocked128` is slower than `blocked64` at n=1024 (34.5 vs 37.8) and n=2048
  (34.0 vs 36.4), against the trend either side of it. The gap is larger than the
  ~1 GFLOP/s round-to-round variation seen in Run 7, but this is a single round.
  Rerun before trying to explain it.
- Whether a tile size between 512 and 1024 (e.g. 768), or non-square tiles, would
  beat 512.

**Next**

- Rerun 64 vs 128 at n=1024 and n=2048 over several rounds.
- Measure cache misses directly (e.g. `perf stat` under WSL) to confirm the
  conflict-miss vs capacity explanation.

## Run 9: README sweep, and naive with padding (2026-11-05)

The data behind the README chart, collected by `python/sweep.py`, plus a check
on two unexpected numbers from the sweep's first attempt.

**Setup**

Same machine and compiler as Run 1.

| | |
|---|---|
| Commit | `bd13292` for all C++ code (unchanged). `python/sweep.py` and `python/plot.py` are new and not yet committed. |
| Builds | `build/` (`MATMUL_PAD_ROWS=ON`) and `build-nopad/` (`MATMUL_PAD_ROWS=OFF`), both `MATMUL_NATIVE=ON`, Release |
| Series | `blocked512` on both builds; `reordered` and `naive` on the padded build; `naive` on the unpadded build (table only) |
| Sizes | n = 256, 512, 1024, 1536, 2048, 3072, 4000, 4096 (`naive` up to 1024) |
| Repeats | 3 per point, plus 1 warm-up per point |
| Raw data | `results/readme_sweep.csv` |
| Later rebuild | After this run, both builds were relinked with `-static` (MinGW), so the executables no longer load whichever `libstdc++-6.dll` is first on PATH; Git for Windows' copy crashed `matmul_bench multiply`. The kernels do not call into that library: a spot-check after relinking matched this run to within 2% (`blocked512` n=2048 41.18, `reordered` n=1024 30.00, `naive` n=1024 4.17, unpadded `blocked512` n=4096 23.92). |

**First attempt discarded.** The first version of `sweep.py` ran each series
in full before the next. Its first series, padded `blocked512`, came out at
about 29 GFLOP/s at n=256-1024, against 41.5 at n=1024 in Run 8. A recheck over
3 rounds put padded `blocked512` at 40.7-43.0 (n=512) and 40.1-40.8 (n=1024),
so the low figures were a measurement artefact affecting whichever series ran
first (most likely the CPU not yet at full clock). `sweep.py` now runs a
discarded warm-up benchmark first and interleaves the series at each size. The
results below are from the corrected script.

**Check: naive at n=1024, padded vs unpadded (3 rounds)**

| Build | Round 1 | Round 2 | Round 3 |
|---|---|---|---|
| padded | 4.21 | 4.18 | 4.12 |
| unpadded | 0.88 | 0.84 | 0.88 |

**Results (GFLOP/s, higher is better)**

| n | naive (padded) | naive (unpadded) | reordered | blocked512 (unpadded) | blocked512 (padded) |
|---|---|---|---|---|---|
| 256 | 2.52 | 2.53 | 31.77 | 32.33 | 32.27 |
| 512 | 2.78 | 2.79 | 44.83 | 43.09 | 43.57 |
| 1024 | 4.08 | 0.86 | 30.60 | 39.30 | 41.13 |
| 1536 | | | 30.84 | 40.74 | 39.98 |
| 2048 | | | 21.41 | 23.94 | 40.96 |
| 3072 | | | 11.12 | 38.45 | 40.63 |
| 4000 | | | 10.76 | 40.01 | 39.72 |
| 4096 | | | 10.63 | 23.90 | 40.52 |

**Observations**

- Padding speeds `naive` up ×4.8 at n=1024 (4.1 against 0.86 GFLOP/s,
  consistent across 3 rounds). Its inner loop walks down a column of B; without
  padding each step is exactly 4 KB, so successive reads map to the same cache
  sets. This corrects the Run 1 explanation, which put the drop down to B not
  fitting in cache. With padding, `naive` is faster at n=1024 than at n=512.
- At n=256 and 512, padding makes no difference to `naive` (strides of 256 and
  512 floats are under 4 KB, and padding only rounds them to the same values).
- Padded `blocked512` holds 39.7-43.6 GFLOP/s from n=512 to n=4096, consistent
  with Run 8.
- Unpadded `blocked512` dips only at n=2048 and n=4096 (about 24 GFLOP/s). n=3072
  is also a multiple of 4 KB per row (12 KB) but runs at 38.5: three times a
  power of two spreads rows across sets better than an exact power of two does.
  Inferred, not measured.
- Padding makes no measurable difference to `blocked512` where the unpadded
  stride is already fine (n=1536 and n=4000, within 1 GFLOP/s).
- `reordered` falls from 44.8 GFLOP/s at n=512 to 10.6 at n=4096, consistent
  with Runs 5 and 8.

**Next**

- Confirm vectorisation from compiler reports (`-fopt-info-vec`).
- Measure cache misses with a profiler.
