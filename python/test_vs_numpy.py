"""Check every C++ kernel against NumPy.

The C++ side is driven through `matmul_bench multiply`, using the binary
format from include/matmul/matrix.hpp: two little-endian uint64 values
(rows, cols), then rows * cols float32 values in row-major order.

Build first (see README), or point MATMUL_BENCH at the executable.
"""

from __future__ import annotations

import os
import subprocess
from pathlib import Path

import numpy as np
import pytest

ROOT = Path(__file__).resolve().parent.parent

# Where CMake puts the executable: build/ for Ninja/Makefiles,
# build/Release/ for multi-config generators such as Visual Studio.
CANDIDATES = [
    ROOT / "build" / "matmul_bench",
    ROOT / "build" / "matmul_bench.exe",
    ROOT / "build" / "Release" / "matmul_bench.exe",
]


def find_executable() -> Path | None:
    override = os.environ.get("MATMUL_BENCH")
    if override:
        return Path(override)
    return next((p for p in CANDIDATES if p.is_file()), None)


EXE = find_executable()


def list_kernels() -> list[str]:
    if EXE is None:
        return []
    result = subprocess.run([str(EXE), "list"], capture_output=True, text=True, check=True)
    return result.stdout.split()


def save_matrix(m: np.ndarray, path: Path) -> None:
    with path.open("wb") as f:
        np.array(m.shape, dtype="<u8").tofile(f)
        np.ascontiguousarray(m, dtype="<f4").tofile(f)


def load_matrix(path: Path) -> np.ndarray:
    rows, cols = np.fromfile(path, dtype="<u8", count=2)
    data = np.fromfile(path, dtype="<f4", offset=16)
    return data.reshape(int(rows), int(cols))


def test_executable_was_built() -> None:
    assert EXE is not None, "matmul_bench not found: build the project or set MATMUL_BENCH"


# (m, k, n): A is m x k, B is k x n. Includes non-square and awkward sizes.
SHAPES = [(1, 1, 1), (7, 5, 3), (65, 64, 63), (129, 130, 128)]


@pytest.mark.parametrize("kernel", list_kernels())
@pytest.mark.parametrize("shape", SHAPES, ids=lambda s: "x".join(map(str, s)))
def test_kernel_matches_numpy(kernel: str, shape: tuple[int, int, int], tmp_path: Path) -> None:
    m, k, n = shape
    rng = np.random.default_rng(seed=m * 10_000 + k * 100 + n)
    a = rng.uniform(-1.0, 1.0, size=(m, k)).astype(np.float32)
    b = rng.uniform(-1.0, 1.0, size=(k, n)).astype(np.float32)

    paths = {name: tmp_path / f"{name}.bin" for name in ("a", "b", "c")}
    save_matrix(a, paths["a"])
    save_matrix(b, paths["b"])

    result = subprocess.run(
        [str(EXE), "multiply", kernel, str(paths["a"]), str(paths["b"]), str(paths["c"])],
        capture_output=True,
        text=True,
        timeout=60,  # a hung kernel fails the test instead of freezing pytest
    )
    assert result.returncode == 0, result.stderr

    actual = load_matrix(paths["c"])
    # Reference in float64, so the only error measured is the kernel's own
    # float32 rounding. That error grows with the dot-product length k.
    expected = a.astype(np.float64) @ b.astype(np.float64)
    np.testing.assert_allclose(actual, expected, rtol=1e-4, atol=1e-5 * k)
