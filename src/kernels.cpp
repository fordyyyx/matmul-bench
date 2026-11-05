#include "matmul/kernels.hpp"

#include <stdexcept>
#include <string>

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
// YOUR KERNELS. Replace each `throw` with a real implementation.
// ---------------------------------------------------------------------------

Matrix multiply_naive(const Matrix& a, const Matrix& b) {
    check_shapes(a, b);
    // TODO(Will): create an a.rows() x b.cols() result, then the i-j-k loop:
    //   for each row i of A, for each column j of B, sum over p of A(i,p) * B(p,j).
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
    // TODO(Will): same result as naive, but loop i, then p, then j.
    // Then add {"reordered", multiply_reordered} to kernels() below.
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
    (void)block;  // remove this line once you use `block`
    // TODO(Will): loop over tiles of i, p and j (step `block`), and within each
    // tile run the i-p-j loop. Watch the edges: the last tile may be smaller.
    // Then add a blocked entry to kernels() below (see the note there).
    throw std::logic_error("multiply_blocked is not implemented yet");
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
        // {"blocked64", [](const Matrix& a, const Matrix& b) { return multiply_blocked(a, b, 64); }},
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
