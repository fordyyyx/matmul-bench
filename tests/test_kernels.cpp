#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>

#include "matmul/kernels.hpp"
#include "matmul/matrix.hpp"

using matmul::Kernel;
using matmul::Matrix;

namespace {

// Largest |actual - expected| over all elements. Shapes must already match.
float max_abs_diff(const Matrix& actual, const Matrix& expected) {
    float worst = 0.0f;
    for (std::size_t i = 0; i < actual.rows(); ++i) {
        for (std::size_t j = 0; j < actual.cols(); ++j) {
            worst = std::fmax(worst, std::fabs(actual(i, j) - expected(i, j)));
        }
    }
    return worst;
}

void expect_same_shape(const Matrix& m, std::size_t rows, std::size_t cols) {
    ASSERT_EQ(m.rows(), rows);
    ASSERT_EQ(m.cols(), cols);
}

}  // namespace

// Runs every TEST_P below once per kernel in the registry.
class KernelTest : public ::testing::TestWithParam<Kernel> {};

TEST_P(KernelTest, KnownSmallProduct) {
    // [1 2 3]   [ 7  8]   [ 58  64]
    // [4 5 6] x [ 9 10] = [139 154]
    //           [11 12]
    Matrix a(2, 3);
    Matrix b(3, 2);
    const float av[] = {1, 2, 3, 4, 5, 6};
    const float bv[] = {7, 8, 9, 10, 11, 12};
    for (std::size_t i = 0; i < 2; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            a(i, j) = av[i * a.cols() + j];
        }
    }
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 2; ++j) {
            b(i, j) = bv[i * b.cols() + j];
        }
    }
    const Matrix c = GetParam().fn(a, b);
    expect_same_shape(c, 2, 2);
    EXPECT_FLOAT_EQ(c(0, 0), 58.0f);
    EXPECT_FLOAT_EQ(c(0, 1), 64.0f);
    EXPECT_FLOAT_EQ(c(1, 0), 139.0f);
    EXPECT_FLOAT_EQ(c(1, 1), 154.0f);
}

TEST_P(KernelTest, IdentityLeavesMatrixUnchanged) {
    const Matrix a = Matrix::random(9, 9, 1);
    const Matrix c = GetParam().fn(a, Matrix::identity(9));
    expect_same_shape(c, 9, 9);
    EXPECT_EQ(max_abs_diff(c, a), 0.0f);  // x*1 + y*0 + ... is exact in floating point
}

TEST_P(KernelTest, ZeroMatrixGivesZero) {
    const Matrix a = Matrix::random(6, 4, 2);
    const Matrix c = GetParam().fn(a, Matrix(4, 5));
    expect_same_shape(c, 6, 5);
    EXPECT_EQ(max_abs_diff(c, Matrix(6, 5)), 0.0f);
}

TEST_P(KernelTest, NonSquareShapes) {
    const Matrix c = GetParam().fn(Matrix::random(3, 5, 3), Matrix::random(5, 2, 4));
    expect_same_shape(c, 3, 2);
}

TEST_P(KernelTest, ShapeMismatchThrows) {
    EXPECT_THROW(GetParam().fn(Matrix(2, 3), Matrix(4, 2)), std::invalid_argument);
}

TEST_P(KernelTest, MatchesNaiveOnAwkwardSizes) {
    if (std::string(GetParam().name) == "naive") {
        GTEST_SKIP() << "naive is the reference itself";
    }
    // Sizes chosen to not divide evenly into typical tile sizes.
    const std::size_t sizes[] = {1, 7, 65, 129};
    for (std::size_t n : sizes) {
        const Matrix a = Matrix::random(n, n + 1, 10);
        const Matrix b = Matrix::random(n + 1, n, 11);
        const Matrix expected = matmul::multiply_naive(a, b);
        const Matrix actual = GetParam().fn(a, b);
        expect_same_shape(actual, n, n);
        // Summing in a different order rounds differently, so allow a small
        // error that grows with the length of each dot product (k = n + 1).
        const float tolerance = 1e-5f * static_cast<float>(n + 1);
        EXPECT_LE(max_abs_diff(actual, expected), tolerance) << "n = " << n;
    }
}

INSTANTIATE_TEST_SUITE_P(AllKernels, KernelTest, ::testing::ValuesIn(matmul::kernels()),
                         [](const ::testing::TestParamInfo<Kernel>& info) {
                             return std::string(info.param.name);
                         });

TEST(MatrixTest, BinaryRoundTrip) {
    const Matrix m = Matrix::random(3, 4, 5);
    const std::string path = ::testing::TempDir() + "roundtrip.bin";
    matmul::save_binary(m, path);
    const Matrix back = matmul::load_binary(path);
    expect_same_shape(back, 3, 4);
    EXPECT_EQ(max_abs_diff(back, m), 0.0f);
}
