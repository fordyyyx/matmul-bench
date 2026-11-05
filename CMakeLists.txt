cmake_minimum_required(VERSION 3.20)
project(matmul_bench LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Benchmarks are meaningless without optimisation, so default to Release.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(MATMUL_NATIVE "Tune for this machine's CPU (-march=native). Off in CI." OFF)
option(MATMUL_BUILD_TESTS "Build the GoogleTest suite" ON)
option(MATMUL_PAD_ROWS "Pad matrix rows so the row stride is never a multiple of 4 KB" ON)

# MinGW: link the C++ runtime statically, so the executables don't load whichever
# libstdc++-6.dll comes first on PATH. Git for Windows ships an incompatible copy
# in /mingw64/bin, which crashes file I/O when it shadows the compiler's own.
if(MINGW)
  add_link_options(-static)
endif()

function(matmul_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

# The library: Matrix plus every kernel. Tests and the benchmark both link it.
add_library(matmul src/matrix.cpp src/kernels.cpp)
target_include_directories(matmul PUBLIC include)
matmul_warnings(matmul)
if(MATMUL_NATIVE AND NOT MSVC)
  target_compile_options(matmul PRIVATE -march=native)
endif()

if(MATMUL_PAD_ROWS)
  target_compile_definitions(matmul PUBLIC MATMUL_PAD_ROWS)
endif()

# Command-line tool: list kernels, benchmark one, or multiply matrices from files.
add_executable(matmul_bench bench/bench.cpp)
target_link_libraries(matmul_bench PRIVATE matmul)
matmul_warnings(matmul_bench)

if(MATMUL_BUILD_TESTS)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  enable_testing()
  add_executable(matmul_tests tests/test_kernels.cpp)
  target_link_libraries(matmul_tests PRIVATE matmul GTest::gtest_main)
  matmul_warnings(matmul_tests)

  include(GoogleTest)
  # A kernel bug such as an infinite loop then fails the test after 30 s
  # instead of hanging ctest forever.
  gtest_discover_tests(matmul_tests PROPERTIES TIMEOUT 30)
endif()
