#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <cstring>
#ifdef _OPENMP
#include <omp.h>
#endif

// Compile:
// g++ -O3 -march=native -fopenmp gemm.cpp -o gemm
//
// Computes: C = A x B
// A: M x K
// B: K x N
// C: M x N
//
// Row-major layout:
// A[i*K + k]
// B[k*N + j]
// C[i*N + j]

static inline int min_int(int a, int b) {
    return (a < b) ? a : b;
}

void gemm_blocked_omp(
    const float* __restrict A,
    const float* __restrict B,
    float* __restrict C,
    int M, int N, int K
) {
    // Tune these for your machine.
    constexpr int BM = 128;
    constexpr int BN = 128;
    constexpr int BK = 64;

    // Zero C
    std::memset(C, 0, sizeof(float) * M * N);

    // Parallelize across output tiles
    #pragma omp parallel for collapse(2) schedule(static)
    for (int ii = 0; ii < M; ii += BM) {
        for (int jj = 0; jj < N; jj += BN) {
            for (int kk = 0; kk < K; kk += BK) {
                int i_max = min_int(ii + BM, M);
                int j_max = min_int(jj + BN, N);
                int k_max = min_int(kk + BK, K);

                for (int i = ii; i < i_max; ++i) {
                    float* __restrict c_row = C + i * N;
                    for (int k = kk; k < k_max; ++k) {
                        const float a_val = A[i * K + k];
                        const float* __restrict b_row = B + k * N;

                        // Vectorization-friendly inner loop
                        #pragma omp simd
                        for (int j = jj; j < j_max; ++j) {
                            c_row[j] += a_val * b_row[j];
                        }
                    }
                }
            }
        }
    }
}

void gemm_naive(
    const float* A,
    const float* B,
    float* C,
    int M, int N, int K
) {
    std::memset(C, 0, sizeof(float) * M * N);

    for (int i = 0; i < M; ++i) {
        for (int k = 0; k < K; ++k) {
            float a = A[i * K + k];
            for (int j = 0; j < N; ++j) {
                C[i * N + j] += a * B[k * N + j];
            }
        }
    }
}

bool verify(
    const float* X,
    const float* Y,
    int size,
    float eps = 1e-2f
) {
    for (int i = 0; i < size; ++i) {
        float diff = std::abs(X[i] - Y[i]);
        if (diff > eps) {
            std::cerr << "Mismatch at " << i
                      << "  X=" << X[i]
                      << "  Y=" << Y[i]
                      << "  diff=" << diff << "\n";
            return false;
        }
    }
    return true;
}

int main() {
    const int M = 1024;
    const int N = 1024;
    const int K = 1024;

    std::vector<float> A(M * K);
    std::vector<float> B(K * N);
    std::vector<float> C_opt(M * N);
    std::vector<float> C_ref(M * N);

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    for (auto& x : A) x = dist(rng);
    for (auto& x : B) x = dist(rng);

    auto t1 = std::chrono::high_resolution_clock::now();
    gemm_blocked_omp(A.data(), B.data(), C_opt.data(), M, N, K);
    auto t2 = std::chrono::high_resolution_clock::now();

    auto opt_us = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();

    // Reference check on smaller size if full check is too slow.
    auto t3 = std::chrono::high_resolution_clock::now();
    gemm_naive(A.data(), B.data(), C_ref.data(), M, N, K);
    auto t4 = std::chrono::high_resolution_clock::now();

    auto ref_us = std::chrono::duration_cast<std::chrono::milliseconds>(t4 - t3).count();

    bool ok = verify(C_opt.data(), C_ref.data(), M * N);

    double flops = 2.0 * M * N * K;
    double gflops = flops / (opt_us * 1e6);

    std::cout << "Optimized GEMM time: " << opt_us << " ms\n";
    std::cout << "Naive GEMM time:     " << ref_us << " ms\n";
    std::cout << "Optimized GFLOPS:    " << gflops << "\n";
    std::cout << "Verification:        " << (ok ? "PASS" : "FAIL") << "\n";

    return 0;
}
