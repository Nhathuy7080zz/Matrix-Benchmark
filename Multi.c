#pragma GCC optimize("O3,unroll-loops")
#pragma GCC target("avx2,fma")
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <pthread.h>
#include "sysdetect.h"

typedef struct {
    double *A;
    double *B;
    double *C;
    int N;
    int chunk_size;
    volatile int *shared_row_counter;
} ThreadArgs;

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

// Hàm luồng: Dynamic Load Balancing + 3D Cache Blocking (tiling i-k-j)
// Trong mỗi chunk [rs, re): i_blk sub-tile -> k_blk -> j_blk
// Working set mỗi sub-tile: A[BLOCK x BLOCK] + B[BLOCK x BLOCK] + C[BLOCK x BLOCK] ~ 3 x 128KB → nằm gọn trong L2
void* multiply_matrix_ikj_thread(void *arg) {
    ThreadArgs *args = (ThreadArgs *)arg;
    double *A = args->A, *B = args->B, *C = args->C;
    int N = args->N, chunk = args->chunk_size;
    volatile int *counter = args->shared_row_counter;

    int BLOCK = 128;
    while (1) {
        // Lấy khối hàng tiếp theo bằng atomic, không cần mutex
        int rs = __atomic_fetch_add(counter, chunk, __ATOMIC_RELAXED);
        if (rs >= N) break;
        int re = (rs + chunk > N) ? N : rs + chunk;

        // 3D tiling i-k-j trong phạm vi [rs, re)
        for (int i_blk = rs; i_blk < re; i_blk += BLOCK) {
            int i_end = (i_blk + BLOCK > re) ? re : i_blk + BLOCK;
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

    pthread_exit(NULL);
    return NULL;
}

int main() {
    int N = 10000;

    print_sysinfo();

    char cpu_model[256];
    int cpu_cores, cpu_threads;
    double base_mhz;
    get_cpu_info(cpu_model, &cpu_cores, &cpu_threads, &base_mhz);

    int NUM_THREADS = (cpu_threads > 0) ? cpu_threads : 4;

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

    pthread_t   threads[NUM_THREADS];
    ThreadArgs  thread_args[NUM_THREADS];
    volatile int shared_row_counter = 0;

    // chunk_size: mỗi luồng lấy ~4-8 lần để cân bằng tải, tránh overhead atomic quá nhiều
    int chunk_size = N / (NUM_THREADS * 4);
    if (chunk_size < 1) chunk_size = 1;

    // --- Warmup run (không tính thời gian) ---
    printf(">> Warmup...\n");
    for (int t = 0; t < NUM_THREADS; t++) {
        thread_args[t] = (ThreadArgs){ A, B, C, N, chunk_size, &shared_row_counter };
        if (pthread_create(&threads[t], NULL, multiply_matrix_ikj_thread, &thread_args[t]) != 0) {
            printf("[LOI] Khong the tao luong thu %d\n", t);
            return 1;
        }
    }
    for (int t = 0; t < NUM_THREADS; t++)
        pthread_join(threads[t], NULL);
    zero_matrix(C, N);
    shared_row_counter = 0;
    printf(">> Dang tinh toan...\n\n");

    double start_time = get_time();

    for (int t = 0; t < NUM_THREADS; t++) {
        thread_args[t] = (ThreadArgs){ A, B, C, N, chunk_size, &shared_row_counter };
        if (pthread_create(&threads[t], NULL, multiply_matrix_ikj_thread, &thread_args[t]) != 0) {
            printf("[LOI] Khong the tao luong thu %d\n", t);
            return 1;
        }
    }

    for (int t = 0; t < NUM_THREADS; t++)
        pthread_join(threads[t], NULL);

    double end_time   = get_time();
    double time_spent = end_time - start_time;
    double total_ops = 2.0 * (double)N * N * N;
    double gflops     = (total_ops / time_spent) / 1e9;

    printf("                -KET QUA-\n");
    printf("=========================================\n");
    printf(" - Kich thuoc ma tran   : %d x %d\n",   N, N);
    printf(" - So luong luong       : %d (Da luong)\n", NUM_THREADS);
    printf(" - Thoi gian chay       : %.4f giay (%.2f phut)\n", time_spent, time_spent / 60.0);
    printf(" - Tong so phep tinh    : %.2f ty (N^3 = %.2e)\n",  total_ops / 1e12, total_ops);
    printf(" - Hieu nang            : %.2f GFLOPS\n", gflops);
    printf("=========================================\n");
    // Thay đổi từ: log_to_csv("Multi", NULL, gflops);
    log_to_csv("Multi", NULL, time_spent, gflops);
    free(A);
    free(B);
    free(C);

    printf("Hoan thanh!\n");
    return 0;
}
