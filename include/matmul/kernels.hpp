#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include "matmul/matrix.hpp"

namespace matmul {

// Every kernel computes C = A * B, where A is m x k and B is k x n,
// returning a new m x n matrix. Each one throws std::invalid_argument
// if a.cols() != b.rows().

// Textbook i-j-k triple loop. The correctness baseline.
Matrix multiply_naive(const Matrix& a, const Matrix& b);

// Same arithmetic, loops in i-k-j order so the innermost loop walks
// rows of B and C contiguously.
Matrix multiply_reordered(const Matrix& a, const Matrix& b);

// Works on block x block tiles so each tile stays in cache while it is reused.
// Must handle sizes that are not a multiple of `block`.
Matrix multiply_blocked(const Matrix& a, const Matrix& b, std::size_t block = 64);

// A named kernel. The registry below is what the tests and benchmark loop over.
using KernelFn = Matrix (*)(const Matrix&, const Matrix&);

struct Kernel {
    const char* name;
    KernelFn fn;
};

// All kernels that are implemented and ready to be tested and benchmarked.
const std::vector<Kernel>& kernels();

// Look a kernel up by name; returns nullptr if there is no such kernel.
const Kernel* find_kernel(std::string_view name);

}  // namespace matmul
