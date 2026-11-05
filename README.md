# matmul-bench

Cache-aware C++ matrix multiplication kernels, benchmarked and tested against NumPy.

> **Status:** work in progress. Results and write-up coming once the kernels are in.

## Kernels

| Kernel | Idea | Status |
|--------|------|--------|
| `naive` | Textbook i-j-k triple loop; the correctness baseline | in progress |
| `reordered` | Same arithmetic in i-k-j order, so the inner loop walks memory contiguously | planned |
| `blocked` | Works on cache-sized tiles that are reused while they are still in cache | planned |

## Build and test

Requires CMake 3.20+, a C++17 compiler and Python 3.10+.

```bash
cmake -S . -B build -G Ninja -DMATMUL_NATIVE=ON
cmake --build build
ctest --test-dir build --output-on-failure

python -m venv .venv
source .venv/Scripts/activate     # Git Bash on Windows; .venv/bin/activate on Linux/macOS
pip install -r python/requirements.txt
pytest -v
```

`MATMUL_NATIVE=ON` tunes the build for your own CPU (`-march=native`); leave it off when
benchmark numbers need to be comparable across machines.

## Benchmark

```bash
build/matmul_bench list
build/matmul_bench bench naive 512 5
# -> kernel,n,repeats,median_ms,gflops
```

## Layout

```
include/matmul/   Matrix (row-major, contiguous) and kernel declarations
src/              Matrix implementation and the kernels
bench/            matmul_bench command-line tool
tests/            GoogleTest suite, run against every registered kernel
python/           pytest harness comparing every kernel with NumPy
```

## Licence

MIT
