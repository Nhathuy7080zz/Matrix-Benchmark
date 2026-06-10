#pragma GCC optimize("O3,unroll-loops")
#pragma GCC target("avx2,fma")
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef __APPLE__
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>
#endif

#include "sysdetect.h"

// Fix #4: num_tiles dùng ceiling division → không bỏ tile cuối nếu N không chia hết 16
// Fix #5: kiểm tra biên row/col khi ghi kết quả, và a_col/b_row khi load tile
const char *kernelSource = "\n"
"#define TILE_SIZE 16\n"
"__kernel void matrix_mul(__global const float* A, __global const float* B, __global float* C, int N) {\n"
"    int row = get_global_id(0);\n"
"    int col = get_global_id(1);\n"
"    int local_row = get_local_id(0);\n"
"    int local_col = get_local_id(1);\n"
"    \n"
"    if (row >= N || col >= N) return;\n"
"    \n"
"    __local float tileA[TILE_SIZE][TILE_SIZE];\n"
"    __local float tileB[TILE_SIZE][TILE_SIZE];\n"
"    \n"
"    float sum = 0.0f;\n"
"    int num_tiles = (N + TILE_SIZE - 1) / TILE_SIZE;\n"
"    \n"
"    for (int t = 0; t < num_tiles; ++t) {\n"
"        int a_col = t * TILE_SIZE + local_col;\n"
"        int b_row = t * TILE_SIZE + local_row;\n"
"        tileA[local_row][local_col] = (a_col < N) ? A[row * N + a_col] : 0.0f;\n"
"        tileB[local_row][local_col] = (b_row < N) ? B[b_row * N + col] : 0.0f;\n"
"        \n"
"        barrier(CLK_LOCAL_MEM_FENCE);\n"
"        \n"
"        for (int k = 0; k < TILE_SIZE; ++k) {\n"
"            sum += tileA[local_row][k] * tileB[k][local_col];\n"
"        }\n"
"        \n"
"        barrier(CLK_LOCAL_MEM_FENCE);\n"
"    }\n"
"    \n"
"    C[row * N + col] = sum;\n"
"}\n";

void check_err(cl_int err, const char *operation) {
    if (err != CL_SUCCESS) {
        printf("[LOI] %s (Ma loi: %d)\n", operation, err);
        exit(1);
    }
}

// Fix #3: dùng float để khớp với kernel FP32
float* allocate_matrix(int N) {
    return (float*)malloc((size_t)N * N * sizeof(float));
}

void initialize_matrix(float *matrix, int N) {
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++)
            matrix[i * N + j] = (float)(rand() % 100) / 10.0f;
}

void zero_matrix(float *matrix, int N) {
    memset(matrix, 0, (size_t)N * N * sizeof(float));
}

int main() {
    int N = 10000;

    print_sysinfo();

    srand(12345);
    // Fix #8: sizeof(float) thay vì sizeof(double)
    size_t matrix_size_bytes = (size_t)N * N * sizeof(float);
    double ram_mb = (3.0 * matrix_size_bytes) / (1024.0 * 1024.0);

    printf("           -CHUAN BI DU LIEU-\n");
    printf("=========================================\n");
    printf(">> Kich thuoc ma tran   : %d x %d\n", N, N);
    // Fix #8: thêm khoảng trắng cho căn cột đồng đều
    printf(">> Dung luong VRAM      : %.0f MB\n", ram_mb);
    printf(">> Khoi tao du lieu     : ");

    float *A = allocate_matrix(N);
    float *B = allocate_matrix(N);
    float *C = allocate_matrix(N);

    if (A == NULL || B == NULL || C == NULL) {
        printf("LOI (Out of memory tren RAM)!\n");
        if (A) free(A); if (B) free(B); if (C) free(C);
        return 1;
    }

    initialize_matrix(A, N);
    initialize_matrix(B, N);
    zero_matrix(C, N);
    printf("Xong\n\n");

    cl_int err;
    cl_uint num_platforms;
    err = clGetPlatformIDs(0, NULL, &num_platforms);
    if (err != CL_SUCCESS || num_platforms == 0) {
        printf("[LOI] Khong tim thay OpenCL Platform. Vui long cai dat driver GPU.\n");
        free(A); free(B); free(C);
        return 1;
    }

    cl_platform_id *platforms = (cl_platform_id*)malloc(num_platforms * sizeof(cl_platform_id));
    clGetPlatformIDs(num_platforms, platforms, NULL);

    int gpu_count = 0;

    for (cl_uint p = 0; p < num_platforms; p++) {
        cl_uint num_devices;
        err = clGetDeviceIDs(platforms[p], CL_DEVICE_TYPE_GPU, 0, NULL, &num_devices);
        if (err != CL_SUCCESS || num_devices == 0) continue;

        cl_device_id *devices = (cl_device_id*)malloc(num_devices * sizeof(cl_device_id));
        clGetDeviceIDs(platforms[p], CL_DEVICE_TYPE_GPU, num_devices, devices, NULL);

        for (cl_uint d = 0; d < num_devices; d++) {
            gpu_count++;
            char device_name[256];
            clGetDeviceInfo(devices[d], CL_DEVICE_NAME, sizeof(device_name), device_name, NULL);

            printf(">> Running on           : %s\n", device_name);

            cl_context context = clCreateContext(NULL, 1, &devices[d], NULL, NULL, &err);
            cl_command_queue queue = clCreateCommandQueue(context, devices[d], 0, &err);

            // Fix #1: &kernelSource (không phải &kernel_source)
            cl_program program = clCreateProgramWithSource(context, 1, &kernelSource, NULL, &err);
            err = clBuildProgram(program, 1, &devices[d], NULL, NULL, NULL);
            if (err != CL_SUCCESS) {
                size_t log_size;
                clGetProgramBuildInfo(program, devices[d], CL_PROGRAM_BUILD_LOG, 0, NULL, &log_size);
                char *log = (char*)malloc(log_size);
                clGetProgramBuildInfo(program, devices[d], CL_PROGRAM_BUILD_LOG, log_size, log, NULL);
                printf("[LOI] Khong the bien dich Kernel cho %s:\n%s\n", device_name, log);
                free(log);
                // Fix #6: giải phóng program/queue/context trước khi continue, tránh leak
                clReleaseProgram(program);
                clReleaseCommandQueue(queue);
                clReleaseContext(context);
                continue;
            }

            // Fix #2: "matrix_mul" (không phải "matmul")
            cl_kernel kernel = clCreateKernel(program, "matrix_mul", &err);

            cl_mem d_A = clCreateBuffer(context, CL_MEM_READ_ONLY  | CL_MEM_COPY_HOST_PTR, matrix_size_bytes, A, &err);
            cl_mem d_B = clCreateBuffer(context, CL_MEM_READ_ONLY  | CL_MEM_COPY_HOST_PTR, matrix_size_bytes, B, &err);
            cl_mem d_C = clCreateBuffer(context, CL_MEM_READ_WRITE, matrix_size_bytes, NULL, &err);
            if (err != CL_SUCCESS) {
                printf("[LOI] Khong the cap phat %.0f MB VRAM cho thiet bi nay (Out of VRAM).\n\n", ram_mb);
                clReleaseKernel(kernel);
                clReleaseProgram(program);
                clReleaseCommandQueue(queue);
                clReleaseContext(context);
                continue;
            }

            clSetKernelArg(kernel, 0, sizeof(cl_mem), &d_A);
            clSetKernelArg(kernel, 1, sizeof(cl_mem), &d_B);
            clSetKernelArg(kernel, 2, sizeof(cl_mem), &d_C);
            clSetKernelArg(kernel, 3, sizeof(int), &N);

            size_t local_work_size[2]  = {16, 16};
            size_t global_work_size[2] = {
                ((size_t)(N + 15) / 16) * 16,
                ((size_t)(N + 15) / 16) * 16
            };

            // Fix #7: Warmup run giống Single/Multi — JIT compile + cache warm trước khi đo
            printf(">> Warmup...\n");
            clEnqueueNDRangeKernel(queue, kernel, 2, NULL, global_work_size, local_work_size, 0, NULL, NULL);
            clFinish(queue);
            // Reset d_C về 0 (host C vẫn là zero buffer từ zero_matrix ở trên)
            clEnqueueWriteBuffer(queue, d_C, CL_TRUE, 0, matrix_size_bytes, C, 0, NULL, NULL);
            printf(">> Dang tinh toan...\n\n");

            double start_time = get_time();

            err = clEnqueueNDRangeKernel(queue, kernel, 2, NULL, global_work_size, local_work_size, 0, NULL, NULL);
            check_err(err, "Thuc thi kernel");
            clFinish(queue);

            clEnqueueReadBuffer(queue, d_C, CL_TRUE, 0, matrix_size_bytes, C, 0, NULL, NULL);

            double end_time   = get_time();
            double time_spent = end_time - start_time;
            double total_ops  = 2.0 * (double)N * N * N;
            double gflops     = (total_ops / time_spent) / 1e9;

            printf("                -KET QUA-\n");
            printf("=========================================\n");
            printf(" - Kich thuoc ma tran   : %d x %d\n",   N, N);
            printf(" - Thiet bi             : %s\n",         device_name);
            printf(" - Thoi gian chay       : %.4f giay (%.2f phut)\n", time_spent, time_spent / 60.0);
            printf(" - Tong so phep tinh    : %.2f ty (N^3 = %.2e)\n",  total_ops / 1e12, total_ops);
            printf(" - Hieu nang            : %.2f GFLOPS\n", gflops);
            printf("=========================================\n\n");

            // Thay đổi từ: log_to_csv("GPU", device_name, gflops);
            log_to_csv("GPU", device_name, time_spent, gflops);

            clReleaseMemObject(d_A);
            clReleaseMemObject(d_B);
            clReleaseMemObject(d_C);
            clReleaseKernel(kernel);
            clReleaseProgram(program);
            clReleaseCommandQueue(queue);
            clReleaseContext(context);

            // Reset host C về 0 để GPU tiếp theo bắt đầu sạch
            zero_matrix(C, N);
        }
        free(devices);
    }
    free(platforms);

    if (gpu_count == 0) {
        printf("[LOI] Khong tim thay GPU nao ho tro OpenCL tren he thong.\n");
    }

    free(A);
    free(B);
    free(C);

    printf("Hoan thanh!\n");
    return 0;
}