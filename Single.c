#pragma GCC optimize("O3,unroll-loops")
#pragma GCC target("avx2,fma")
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "sysdetect.h"

double* allocate_matrix(int N) {
    return (double*)malloc((size_t)N * N * sizeof(double));
}

void initialize_matrix(double *matrix, int N) {
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++)
            matrix[i * N + j] = (double)(rand() % 100) / 10.0;
}

void zero_matrix(double *matrix, int N) {
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++)
            matrix[i * N + j] = 0.0;
}

// Nhân ma trận ikj với 3D Cache Blocking (tiling i-k-j) để tối ưu Spatial Locality tối đa
// Thứ tự tile: i_blk -> k_blk -> j_blk (đúng kiến trúc Row-major C)
// Working set mỗi tile: A[BLOCK x BLOCK] + B[BLOCK x BLOCK] + C[BLOCK x BLOCK] ~ 3 x 128KB → nằm gọn trong L2
void multiply_matrix_ikj(const double *A, const double *B, double *C, int N) {
    int BLOCK = 128;
    for (int i_blk = 0; i_blk < N; i_blk += BLOCK) {
        int i_end = (i_blk + BLOCK > N) ? N : i_blk + BLOCK;
        for (int k_blk = 0; k_blk < N; k_blk += BLOCK) {
            int k_end = (k_blk + BLOCK > N) ? N : k_blk + BLOCK;
            for (int j_blk = 0; j_blk < N; j_blk += BLOCK) {
                int j_end = (j_blk + BLOCK > N) ? N : j_blk + BLOCK;
                for (int i = i_blk; i < i_end; i++) {
                    for (int k = k_blk; k < k_end; k++) {
                        double a_ik = A[i * N + k];
                        #pragma GCC ivdep
                        for (int j = j_blk; j < j_end; j++)
                            C[i * N + j] += a_ik * B[k * N + j];
                    }
                }
            }
        }
    }
}

int main() {
    int N = 10000;

    print_sysinfo();

    printf("                -RUNNING-\n");
    printf("=========================================\n");

    srand(12345);

    double ram_mb = (3.0 * (size_t)N * N * sizeof(double)) / (1024.0 * 1024.0);
    printf(">> Kich thuoc ma tran   : %d x %d\n", N, N);
    printf(">> Dung luong RAM can   : %.0f MB\n", ram_mb);
    printf(">> Khoi tao du lieu     : ");

    double *A = allocate_matrix(N);
    double *B = allocate_matrix(N);
    double *C = allocate_matrix(N);

    if (A == NULL || B == NULL || C == NULL) {
        printf("LOI (Out of memory)!\n");
        if (A) free(A);
        if (B) free(B);
        if (C) free(C);
        return 1;
    }

    initialize_matrix(A, N);
    initialize_matrix(B, N);
    zero_matrix(C, N);

    printf("Xong\n");
    printf(">> Warmup...\n");
    multiply_matrix_ikj(A, B, C, N);
    zero_matrix(C, N);
    printf(">> Dang tinh toan...\n\n");

    double start_time = get_time();
    multiply_matrix_ikj(A, B, C, N);
    double end_time = get_time();

    double time_spent = end_time - start_time;
    double total_ops = 2.0 * (double)N * N * N;
    double gflops     = (total_ops / time_spent) / 1e9;

    printf("                -KET QUA-\n");
    printf("=========================================\n");
    printf(" - Kich thuoc ma tran   : %d x %d\n",   N, N);
    printf(" - So luong luong       : 1 (Tuan tu)\n");
    printf(" - Thoi gian chay       : %.4f giay (%.2f phut)\n", time_spent, time_spent / 60.0);
    printf(" - Tong so phep tinh    : %.2f ty (N^3 = %.2e)\n",  total_ops / 1e12, total_ops);
    printf(" - Hieu nang            : %.2f GFLOPS\n", gflops);
    printf("=========================================\n");
    // Thay đổi từ: log_to_csv("Single", NULL, gflops);
    log_to_csv("Single", NULL, time_spent, gflops);
    free(A);
    free(B);
    free(C);

    printf("Hoan thanh!\n");
    return 0;
}
