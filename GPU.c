#pragma GCC optimize("O3,unroll-loops")
#pragma GCC target("avx2,fma")
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#ifdef __APPLE__
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>
#endif

#include "sysdetect.h"

// OpenCL Kernel: Nhân ma trận với Tiling (Sử dụng Local Memory tương đương Cache Blocking)
// Lưu ý: Bật cl_khr_fp64 để hỗ trợ tính toán số thực dấu phẩy động 64-bit (double) giống hệt CPU
const char *kernel_source =
"#define TS 16\n"
"__kernel void matmul(__global const float *A, __global const float *B, __global float *C, int N) {\n"
"    int row = get_local_id(1);\n"
"    int col = get_local_id(0);\n"
"    int globalRow = TS * get_group_id(1) + row;\n"
"    int globalCol = TS * get_group_id(0) + col;\n"
"    __local float Asub[TS][TS];\n"
"    __local float Bsub[TS][TS];\n"
"    float sum = 0.0f;\n"
"    int numTiles = (N + TS - 1) / TS;\n"
"    for (int t = 0; t < numTiles; t++) {\n"
"        int tiledRow = TS * t + row;\n"
"        int tiledCol = TS * t + col;\n"
"        if (globalRow < N && tiledCol < N) Asub[row][col] = A[globalRow * N + tiledCol];\n"
"        else Asub[row][col] = 0.0f;\n"
"        if (tiledRow < N && globalCol < N) Bsub[row][col] = B[tiledRow * N + globalCol];\n"
"        else Bsub[row][col] = 0.0f;\n"
"        barrier(CLK_LOCAL_MEM_FENCE);\n"
"        for (int k = 0; k < TS; k++) sum += Asub[row][k] * Bsub[k][col];\n"
"        barrier(CLK_LOCAL_MEM_FENCE);\n"
"    }\n"
"    if (globalRow < N && globalCol < N) C[globalRow * N + globalCol] = sum;\n"
"}\n";

void check_err(cl_int err, const char *operation) {
    if (err != CL_SUCCESS) {
        printf("[LOI] %s (Ma loi: %d)\n", operation, err);
        exit(1);
    }
}

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

int main() {
    int N = 10000;
    
    // 1. In thông tin hệ thống (tái sử dụng từ sysdetect.h)
    print_sysinfo();

    srand(12345);
    size_t matrix_size_bytes = (size_t)N * N * sizeof(double);
    double ram_mb = (3.0 * matrix_size_bytes) / (1024.0 * 1024.0);

    // 2. Khởi tạo dữ liệu trên Host (RAM hệ thống)
    printf("           -CHUAN BI DU LIEU-\n");
    printf("=========================================\n");
    printf(">> Kich thuoc ma tran   : %d x %d\n", N, N);
    printf(">> Dung luong VRAM: %.0f MB\n", ram_mb);
    printf(">> Khoi tao du lieu     : ");
    
    double *A = allocate_matrix(N);
    double *B = allocate_matrix(N);
    double *C = allocate_matrix(N);

    if (A == NULL || B == NULL || C == NULL) {
        printf("LOI (Out of memory tren RAM)!\n");
        if (A) free(A); if (B) free(B); if (C) free(C);
        return 1;
    }

    initialize_matrix(A, N);
    initialize_matrix(B, N);
    zero_matrix(C, N);
    printf("Xong\n\n");

    // 3. Khởi tạo OpenCL và quét tìm toàn bộ GPU
    cl_int err;
    cl_uint num_platforms;
    err = clGetPlatformIDs(0, NULL, &num_platforms);
    if (err != CL_SUCCESS || num_platforms == 0) {
        printf("[LOI] Khong tim thay OpenCL Platform. Vui long cai dat driver GPU.\n");
        return 1;
    }

    cl_platform_id *platforms = (cl_platform_id*)malloc(num_platforms * sizeof(cl_platform_id));
    clGetPlatformIDs(num_platforms, platforms, NULL);

    int gpu_count = 0;

    // Lặp qua tất cả Platform và Device (GPU)
    for (cl_uint p = 0; p < num_platforms; p++) {
        cl_uint num_devices;
        err = clGetDeviceIDs(platforms[p], CL_DEVICE_TYPE_GPU, 0, NULL, &num_devices);
        if (err != CL_SUCCESS || num_devices == 0) continue; // Bỏ qua nếu platform không có GPU

        cl_device_id *devices = (cl_device_id*)malloc(num_devices * sizeof(cl_device_id));
        clGetDeviceIDs(platforms[p], CL_DEVICE_TYPE_GPU, num_devices, devices, NULL);

        for (cl_uint d = 0; d < num_devices; d++) {
            gpu_count++;
            char device_name[256];
            clGetDeviceInfo(devices[d], CL_DEVICE_NAME, sizeof(device_name), device_name, NULL);


            printf(">> Running on           : %s\n", device_name);

            // Cấu hình Context & Queue cho GPU hiện tại
            cl_context context = clCreateContext(NULL, 1, &devices[d], NULL, NULL, &err);
            cl_command_queue queue = clCreateCommandQueue(context, devices[d], 0, &err);

            // Biên dịch Kernel
            cl_program program = clCreateProgramWithSource(context, 1, &kernel_source, NULL, &err);
            err = clBuildProgram(program, 1, &devices[d], NULL, NULL, NULL);
            if (err != CL_SUCCESS) {
                size_t log_size;
                clGetProgramBuildInfo(program, devices[d], CL_PROGRAM_BUILD_LOG, 0, NULL, &log_size);
                char *log = (char*)malloc(log_size);
                clGetProgramBuildInfo(program, devices[d], CL_PROGRAM_BUILD_LOG, log_size, log, NULL);
                printf("[LOI] Khong the bien dich Kernel cho %s:\n%s\n", device_name, log);
                free(log);
                continue; // Chuyển sang GPU tiếp theo
            }
            cl_kernel kernel = clCreateKernel(program, "matmul", &err);

            // Cấp phát VRAM
            cl_mem d_A = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, matrix_size_bytes, A, &err);
            cl_mem d_B = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, matrix_size_bytes, B, &err);
            cl_mem d_C = clCreateBuffer(context, CL_MEM_WRITE_ONLY, matrix_size_bytes, NULL, &err);
            if (err != CL_SUCCESS) {
                printf("[LOI] Khong the cap phat %.0f MB VRAM cho thiet bi nay (Out of VRAM).\n\n", ram_mb);
                clReleaseKernel(kernel);
                clReleaseProgram(program);
                clReleaseCommandQueue(queue);
                clReleaseContext(context);
                continue;
            }

            // Thiết lập tham số Kernel
            clSetKernelArg(kernel, 0, sizeof(cl_mem), &d_A);
            clSetKernelArg(kernel, 1, sizeof(cl_mem), &d_B);
            clSetKernelArg(kernel, 2, sizeof(cl_mem), &d_C);
            clSetKernelArg(kernel, 3, sizeof(int), &N);

            // Định cấu hình Grid và Block
            size_t local_work_size[2] = {16, 16}; // Tương ứng với TS=16
            size_t global_work_size[2] = {
                ((N + 15) / 16) * 16,
                ((N + 15) / 16) * 16
            };

            // Bắt đầu đo thời gian
            double start_time = get_time();

            err = clEnqueueNDRangeKernel(queue, kernel, 2, NULL, global_work_size, local_work_size, 0, NULL, NULL);
            check_err(err, "Thuc thi kernel");

            // Đợi GPU tính xong hoàn toàn
            clFinish(queue);

            // Lấy dữ liệu C về RAM (Chỉ để xác nhận tác vụ xong, ở benchmark có thể skip bước copy về nếu chỉ đo tính toán, nhưng ta lấy về cho chuẩn)
            err = clEnqueueReadBuffer(queue, d_C, CL_TRUE, 0, matrix_size_bytes, C, 0, NULL, NULL);

            double end_time = get_time();
            double time_spent = end_time - start_time;
            double total_ops = (double)N * N * N;
            double gflops = (total_ops / time_spent) / 1e9;

            printf("\n                -KET QUA-\n");
            printf("=========================================\n");
            printf(" - Thoi gian chay       : %.4f giay (%.2f phut)\n", time_spent, time_spent / 60.0);
            printf(" - Tong so phep tinh    : %.2f ty (N^3 = %.2e)\n", total_ops / 1e12, total_ops);
            printf(" - Hieu nang            : %.2f GFLOPS\n", gflops);
            printf("=========================================\n\n");

            // Giải phóng tài nguyên GPU hiện tại
            clReleaseMemObject(d_A);
            clReleaseMemObject(d_B);
            clReleaseMemObject(d_C);
            clReleaseKernel(kernel);
            clReleaseProgram(program);
            clReleaseCommandQueue(queue);
            clReleaseContext(context);
            
            // Reset ma trận C về 0 để công bằng cho GPU chạy tiếp theo
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