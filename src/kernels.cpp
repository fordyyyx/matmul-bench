#include "matmul/kernels.hpp"

#include <stdexcept>
#include <string>
#include <algorithm>

namespace matmul {

namespace {

// Every kernel calls this first: the inner dimensions must agree.
void check_shapes(const Matrix& a, const Matrix& b) {
    if (a.cols() != b.rows()) {
        throw std::invalid_argument(
            "shape mismatch: A is " + std::to_string(a.rows()) + "x" + std::to_string(a.cols()) +
            ", B is " + std::to_string(b.rows()) + "x" + std::to_string(b.cols()));
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// KERNELS
// ---------------------------------------------------------------------------

Matrix multiply_naive(const Matrix& a, const Matrix& b) {
    check_shapes(a, b);
    Matrix c(a.rows(), b.cols());
    for (std::size_t i = 0 ; i < a.rows() ; i++) {
        for (std::size_t j = 0 ; j < b.cols() ; j++) {
            float sum = 0.0f;
            for (std::size_t p = 0 ; p < a.cols() ; p++) {
                sum += a(i, p) * b(p, j);
            }
            c(i, j) = sum;
        }
    }
    return c;
}

Matrix multiply_reordered(const Matrix& a, const Matrix& b) {
    check_shapes(a, b);
    Matrix c(a.rows(), b.cols());
    for (std::size_t i = 0; i < a.rows(); i++) {
        for (std::size_t p = 0; p < a.cols(); p++) {
            for (std::size_t j = 0; j < b.cols(); j++) {
                c(i, j) += a(i, p) * b(p, j);
            }
        }
    }
    return c;
}

Matrix multiply_blocked(const Matrix& a, const Matrix& b, std::size_t block) {
    check_shapes(a, b);
    if (block == 0) {
        throw std::invalid_argument("block must not be 0");
    }
    Matrix c(a.rows(), b.cols());
    for (std::size_t i0 = 0; i0 < a.rows(); i0 += block) {
        std::size_t i1 = std::min(i0 + block, a.rows());
        for (std::size_t p0 = 0; p0 < a.cols(); p0 += block) {
            std::size_t p1 = std::min(p0 + block, a.cols());
            for (std::size_t j0 = 0; j0 < b.cols(); j0 += block) {
                std::size_t j1 = std::min(j0 + block, b.cols());
                for (std::size_t i = i0; i < i1; i++) {
                    for (std::size_t p = p0; p < p1; p++) {
                        for (std::size_t j = j0; j < j1; j++) {
                            c(i, j) += a(i, p) * b(p, j);
                        }
                    }
                }
            }
        }
    }
    return c;
}

// ---------------------------------------------------------------------------
// Registry. Add a line here when a kernel is ready; the C++ tests, the Python
// tests and the benchmark all pick it up automatically.
// ---------------------------------------------------------------------------

const std::vector<Kernel>& kernels() {
    static const std::vector<Kernel> all = {
        {"naive", multiply_naive},
        {"reordered", multiply_reordered},
        // multiply_blocked takes an extra `block` argument, so wrap it in a
        // capture-less lambda, which converts to a plain function pointer:
        {"blocked32", [](const Matrix& a, const Matrix& b) { return multiply_blocked(a, b, 32); }},
        {"blocked64", [](const Matrix& a, const Matrix& b) { return multiply_blocked(a, b, 64); }},
        {"blocked128", [](const Matrix& a, const Matrix& b) { return multiply_blocked(a, b, 128); }},
        {"blocked256", [](const Matrix& a, const Matrix& b) { return multiply_blocked(a, b, 256); }},
    };
    return all;
}

const Kernel* find_kernel(std::string_view name) {
    for (const Kernel& k : kernels()) {
        if (name == k.name) {
            return &k;
        }
    }
    return nullptr;
}

}  // namespace matmul
