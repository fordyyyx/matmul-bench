#include "matmul/matrix.hpp"

#include <cstdint>
#include <fstream>
#include <random>
#include <stdexcept>

namespace matmul {

namespace {
    std::size_t padded_stride(std::size_t cols) {
        std::size_t stride = (cols + 15) / 16 * 16;
        if (stride % 1024 == 0) {
            return stride + 16;
        }
        return stride;
    }
} // namespace

Matrix::Matrix(std::size_t rows, std::size_t cols, float fill)
    : rows_(rows), cols_(cols), stride_(padded_stride(cols)), data_(rows * stride_, fill) {}

Matrix Matrix::identity(std::size_t n) {
    Matrix m(n, n);
    for (std::size_t i = 0; i < n; ++i) {
        m(i, i) = 1.0f;
    }
    return m;
}

Matrix Matrix::random(std::size_t rows, std::size_t cols, unsigned seed) {
    std::mt19937 gen(seed);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    Matrix m(rows, cols);
    for (std::size_t i = 0; i < rows; ++i) {
        for (std::size_t j = 0; j < cols; j++) {
            m(i, j) = dist(gen);
        }
    }
    return m;
}

// Raw byte I/O assumes a little-endian machine (x86 and ARM both are),
// matching the '<u8' / '<f4' dtypes the Python side uses.
void save_binary(const Matrix& m, const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("cannot open for writing: " + path);
    }
    const std::uint64_t shape[2] = {m.rows(), m.cols()};
    out.write(reinterpret_cast<const char*>(shape), sizeof(shape));
    for (std::size_t i = 0; i < m.rows(); i++) {
        out.write(reinterpret_cast<const char*>(m.data() + i * m.stride()),
            static_cast<std::streamsize>(m.cols() * sizeof(float)));
    }
}

Matrix load_binary(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open for reading: " + path);
    }
    std::uint64_t shape[2] = {0, 0};
    in.read(reinterpret_cast<char*>(shape), sizeof(shape));
    Matrix m(static_cast<std::size_t>(shape[0]), static_cast<std::size_t>(shape[1]));
    for (std::size_t i = 0; i < m.rows(); i++) {
        in.read(reinterpret_cast<char*>(m.data()),
            static_cast<std::streamsize>(m.rows() * m.cols() * sizeof(float)));
    }
    if (!in) {
        throw std::runtime_error("file too short for its header: " + path);
    }
    return m;
}

}  // namespace matmul
