// matmul_bench: command-line front end for the kernels.
//
//   matmul_bench list                               print kernel names, one per line
//   matmul_bench bench <kernel> <n> [repeats]       time n x n multiply, print one CSV row:
//                                                   kernel,n,repeats,median_ms,gflops
//   matmul_bench multiply <kernel> <a> <b> <out>    C = A * B on binary matrix files
//                                                   (used by the Python tests)

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "matmul/kernels.hpp"
#include "matmul/matrix.hpp"

using matmul::Kernel;
using matmul::Matrix;

namespace {

// Writing to a volatile counts as an observable side effect, so the
// optimiser cannot decide the multiply's result is unused and delete it.
volatile float g_sink = 0.0f;

int usage() {
    std::cerr << "usage:\n"
                 "  matmul_bench list\n"
                 "  matmul_bench bench <kernel> <n> [repeats=5]\n"
                 "  matmul_bench multiply <kernel> <a.bin> <b.bin> <out.bin>\n";
    return 2;
}

const Kernel& require_kernel(const std::string& name) {
    const Kernel* k = matmul::find_kernel(name);
    if (k == nullptr) {
        throw std::invalid_argument("unknown kernel '" + name + "' (try: matmul_bench list)");
    }
    return *k;
}

double time_once_ms(const Kernel& k, const Matrix& a, const Matrix& b) {
    const auto start = std::chrono::steady_clock::now();
    const Matrix c = k.fn(a, b);
    const auto end = std::chrono::steady_clock::now();
    g_sink = c(0, 0);
    return std::chrono::duration<double, std::milli>(end - start).count();
}

int run_bench(const Kernel& k, std::size_t n, std::size_t repeats) {
    const Matrix a = Matrix::random(n, n, 1);
    const Matrix b = Matrix::random(n, n, 2);

    time_once_ms(k, a, b);  // warm-up: page in memory, fill caches; not counted

    std::vector<double> times;
    for (std::size_t r = 0; r < repeats; ++r) {
        times.push_back(time_once_ms(k, a, b));
    }
    // Median, not mean: one slow run (OS interruption) should not skew the result.
    std::sort(times.begin(), times.end());
    const double median_ms = times[times.size() / 2];

    // An n x n multiply does n^3 multiply-adds = 2 n^3 floating-point operations.
    const double flops = 2.0 * static_cast<double>(n) * static_cast<double>(n) * static_cast<double>(n);
    const double gflops = flops / (median_ms / 1000.0) / 1e9;

    std::cout << k.name << ',' << n << ',' << repeats << ',' << median_ms << ',' << gflops << '\n';
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const std::vector<std::string> args(argv + 1, argv + argc);
        if (args.empty()) {
            return usage();
        }
        const std::string& cmd = args[0];

        if (cmd == "list" && args.size() == 1) {
            for (const Kernel& k : matmul::kernels()) {
                std::cout << k.name << '\n';
            }
            return 0;
        }
        if (cmd == "bench" && (args.size() == 3 || args.size() == 4)) {
            const std::size_t n = std::stoul(args[2]);
            const std::size_t repeats = args.size() == 4 ? std::stoul(args[3]) : 5;
            if (n == 0 || repeats == 0) {
                throw std::invalid_argument("n and repeats must be at least 1");
            }
            return run_bench(require_kernel(args[1]), n, repeats);
        }
        if (cmd == "multiply" && args.size() == 5) {
            const Kernel& k = require_kernel(args[1]);
            const Matrix c = k.fn(matmul::load_binary(args[2]), matmul::load_binary(args[3]));
            matmul::save_binary(c, args[4]);
            return 0;
        }
        return usage();
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
