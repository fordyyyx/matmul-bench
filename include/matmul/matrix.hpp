#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace matmul {

// Dense matrix of floats in row-major order: all elements live in ONE
// contiguous block, row after row, so element (i, j) is data[i * stride + j].
// Walking along a row touches neighbouring memory; walking down a column
// jumps `cols` floats at a time. That difference is what the kernels exploit.
class Matrix {
public:
    Matrix(std::size_t rows, std::size_t cols, float fill = 0.0f);

    std::size_t rows() const { return rows_; }
    std::size_t cols() const { return cols_; }

    // m(i, j) reads or writes one element. No bounds checking, for speed.
    float& operator()(std::size_t i, std::size_t j) { return data_[i * stride_ + j]; }
    float operator()(std::size_t i, std::size_t j) const { return data_[i * stride_ + j]; }

    // Raw pointer to the first element, for kernels that index manually.
    float* data() { return data_.data(); }
    const float* data() const { return data_.data(); }

    static Matrix identity(std::size_t n);
    // Uniform values in [-1, 1]; the same seed always gives the same matrix.
    static Matrix random(std::size_t rows, std::size_t cols, unsigned seed);

    std::size_t stride() const { return stride_; }

private:
    std::size_t rows_;
    std::size_t cols_;
    std::size_t stride_;
    std::vector<float> data_;
};

// File format shared with the Python tests: two little-endian uint64 values
// (rows, cols), then rows * cols float32 values in row-major order.
void save_binary(const Matrix& m, const std::string& path);
Matrix load_binary(const std::string& path);

}  // namespace matmul
